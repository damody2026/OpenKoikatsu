// Editor authoring only. The saved map contains ordinary engine components;
// room decoration adds no runtime tick or dependency on the importer.
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "ShaderCompiler.h"

static int32 OKBuildCrowdRoomEnvironment(const FString& Params,UWorld* ExistingWorld=nullptr)
{
 FString Destination=TEXT("/Game/Scenes/CrowdRoom");FParse::Value(*Params,TEXT("Destination="),Destination);
 if(!Destination.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(Destination))return 1;
 const FString MapPath=Destination/TEXT("CrowdRoom"),AssetRoot=Destination/TEXT("EnvironmentR1");
 const FString MapFile=FPackageName::LongPackageNameToFilename(MapPath,FPackageName::GetMapPackageExtension());
 TStrongObjectPtr<UWorld> World(ExistingWorld?ExistingWorld:LoadObject<UWorld>(nullptr,*(MapPath+TEXT(".CrowdRoom"))));
 if(!World){UE_LOG(LogTemp,Error,TEXT("Room environment requires an existing crowd map"));return 1;}
 // Preserve the previous map before replacing its environment, including on
 // subsequent runs. Character selection and gameplay actors are left intact.
 FString Tag;FParse::Value(*Params,TEXT("BackupTag="),Tag);
 if(Tag.IsEmpty())return 1;for(TCHAR C:Tag)if(!FChar::IsAlnum(C)&&C!=TEXT('_'))return 1;
 const FString Backup=FPaths::ProjectSavedDir()/TEXT("Crowd/RoomBackups")/Tag/TEXT("CrowdRoom.umap");
 if(IFileManager::Get().FileExists(*Backup))return 1;
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);
 if(IFileManager::Get().Copy(*Backup,*MapFile)!=COPY_OK)return 1;
 TArray<UObject*> Assets;
 auto Save=[](UObject* O){FSavePackageArgs A;A.TopLevelFlags=RF_Public|RF_Standalone;O->MarkPackageDirty();return UPackage::SavePackage(O->GetOutermost(),O,*FPackageName::LongPackageNameToFilename(O->GetOutermost()->GetName(),O->IsA<UWorld>()?FPackageName::GetMapPackageExtension():FPackageName::GetAssetPackageExtension()),A);};
 auto Texture=[&](const TCHAR* Name,int32 Kind){
  const FString Path=AssetRoot/Name;auto* T=LoadObject<UTexture2D>(nullptr,*(Path+TEXT(".")+Name),nullptr,LOAD_NoWarn);
  if(!T)T=NewObject<UTexture2D>(CreatePackage(*Path),Name,RF_Public|RF_Standalone);
  constexpr int32 Size=512;TArray<FColor> Pixels;Pixels.SetNumUninitialized(Size*Size);
  for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X){
   const float U=float(X)/Size,V=float(Y)/Size;
   // Periodic grain avoids visible seams; stone is restrained so it does not
   // compete with the character silhouettes at third-person viewing height.
   const float Grain=FMath::Sin(2*PI*(U*3+FMath::Sin(V*2*PI)*.16f))+FMath::Sin(2*PI*(U*13+V*2))*.22f;
   FLinearColor C;
   if(Kind==0){const float Vein=FMath::Pow(FMath::Abs(FMath::Sin(2*PI*(U+V*2+.10f*FMath::Sin(U*6*PI)))),22.f);C=FLinearColor(.72f,.69f,.60f)*(1-.12f*Vein)+FLinearColor(.02f,.016f,.01f)*Grain;const float Edge=FMath::Min(FMath::Min(U,1-U),FMath::Min(V,1-V));if(Edge<.005f)C=FLinearColor(.28f,.29f,.27f);}
   else if(Kind==1){C=FLinearColor(.29f,.14f,.065f)*(1+.13f*Grain+.045f*FMath::Sin(U*140*PI));if(FMath::Frac(V*4)<.012f)C*=.55f;}
   else {C=FLinearColor(.40f,.69f,.78f);float Ridge=.26f+.06f*FMath::Sin(U*PI*6)+.035f*FMath::Sin(U*PI*14);if(V>.58f-Ridge)C=FLinearColor(.22f,.44f,.43f);if(V>.78f-Ridge*.4f)C=FLinearColor(.10f,.28f,.25f);if(V<.3f&&FMath::Sin(U*19+V*4)>.80f)C=FLinearColor(.73f,.84f,.83f);}
   Pixels[Y*Size+X]=C.ToFColorSRGB();
  }
  T->Source.Init(Size,Size,1,1,TSF_BGRA8,reinterpret_cast<const uint8*>(Pixels.GetData()));T->SRGB=true;T->NeverStream=false;T->LODGroup=TEXTUREGROUP_World;T->PostEditChange();Assets.Add(T);return T;
 };
 auto* StoneTexture=Texture(TEXT("T_Limestone"),0);auto* WoodTexture=Texture(TEXT("T_Walnut"),1);auto* ViewTexture=Texture(TEXT("T_GardenView"),2);
 auto Material=[&](const TCHAR* Name,FLinearColor Color,UTexture2D* Tex,bool Floor=false,bool WorldUV=false){
  const FString Path=AssetRoot/Name;auto* M=LoadObject<UMaterial>(nullptr,*(Path+TEXT(".")+Name),nullptr,LOAD_NoWarn);if(!M)M=NewObject<UMaterial>(CreatePackage(*Path),Name,RF_Public|RF_Standalone);
  M->GetExpressionCollection().Empty();M->SetShadingModel(Floor?MSM_DefaultLit:MSM_Unlit);M->TwoSided=false;
  // Commandlet worlds do not run the interactive editor's automatic usage
  // detection. Explicitly cook the HISM permutation before saving materials.
  M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes,true);
  auto Add=[&](UMaterialExpression* E){M->GetExpressionCollection().AddExpression(E);};
  UMaterialExpression* ColorExpr=nullptr;
  if(Tex){auto* T=NewObject<UMaterialExpressionTextureSample>(M);T->Texture=Tex;T->SamplerType=SAMPLERTYPE_Color;Add(T);ColorExpr=T;
   if(WorldUV){auto* P=NewObject<UMaterialExpressionWorldPosition>(M);Add(P);auto* UV=NewObject<UMaterialExpressionCustom>(M);UV->Code=TEXT("return P.xy / 200.0;");UV->OutputType=CMOT_Float2;FCustomInput I;I.InputName=TEXT("P");I.Input.Connect(0,P);UV->Inputs.Add(I);Add(UV);T->Coordinates.Connect(0,UV);}
  }else{auto* C=NewObject<UMaterialExpressionConstant3Vector>(M);C->Constant=Color;Add(C);ColorExpr=C;}
  if(Floor){M->GetEditorOnlyData()->BaseColor.Connect(0,ColorExpr);auto* Ambient=NewObject<UMaterialExpressionMultiply>(M);Ambient->A.Connect(0,ColorExpr);Ambient->ConstB=.24f;Add(Ambient);M->GetEditorOnlyData()->EmissiveColor.Connect(0,Ambient);auto* R=NewObject<UMaterialExpressionConstant>(M);R->R=.82f;Add(R);M->GetEditorOnlyData()->Roughness.Connect(0,R);}
  else M->GetEditorOnlyData()->EmissiveColor.Connect(0,ColorExpr);
  M->PostEditChange();Assets.Add(M);return M;
 };
 auto* Stone=Material(TEXT("M_LimestoneFloor"),FLinearColor::White,StoneTexture,true,true);
 auto* Wood=Material(TEXT("M_Walnut"),FLinearColor::White,WoodTexture);
 auto* View=Material(TEXT("M_GardenView"),FLinearColor::White,ViewTexture);
 auto* Plaster=Material(TEXT("M_WarmPlaster"),FLinearColor(.64f,.61f,.52f),nullptr);
 auto* Teal=Material(TEXT("M_DeepTeal"),FLinearColor(.035f,.14f,.15f),nullptr);
 auto* Brass=Material(TEXT("M_Brass"),FLinearColor(.48f,.32f,.12f),nullptr);
 auto* Light=Material(TEXT("M_LuminousIvory"),FLinearColor(.94f,.87f,.68f),nullptr);
 auto* Leaf=Material(TEXT("M_Foliage"),FLinearColor(.075f,.24f,.10f),nullptr);
 auto* Ceiling=Material(TEXT("M_Ceiling"),FLinearColor(.43f,.47f,.44f),nullptr);
 // Inactive maps loaded by a commandlet are excluded by the default actor
 // iterator's OnlyActiveLevels filter. Iterate the serialized level directly
 // or the old gray floor and earlier generations remain coplanar underneath.
 TArray<AActor*> Old;for(AActor* A:World->PersistentLevel->Actors)if(A&&(A->IsA<AStaticMeshActor>()||A->IsA<ADirectionalLight>()||A->IsA<ASkyLight>()||A->ActorHasTag(TEXT("CrowdRoomEnvironment"))))Old.Add(A);
 for(auto* A:Old)if(!World->DestroyActor(A)){UE_LOG(LogTemp,Error,TEXT("Cannot replace room actor %s"),*A->GetName());return 1;}
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));auto* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));if(!Cube||!Sphere||!Cylinder)return 1;
 TMap<FString,UHierarchicalInstancedStaticMeshComponent*> Groups;int32 Instances=0;
 auto Shape=[&](UStaticMesh* Mesh,UMaterial* Mat,FVector Position,FVector Size,FRotator Rot=FRotator::ZeroRotator){const FString Key=Mesh->GetName()+Mat->GetName();auto*& C=Groups.FindOrAdd(Key);if(!C){auto* A=World->SpawnActor<AActor>();A->Tags.Add(TEXT("CrowdRoomEnvironment"));A->SetActorLabel(TEXT("Atrium_")+Key);C=NewObject<UHierarchicalInstancedStaticMeshComponent>(A,*Key);A->SetRootComponent(C);A->AddInstanceComponent(C);C->SetMobility(EComponentMobility::Static);C->SetStaticMesh(Mesh);C->SetMaterial(0,Mat);C->SetCastShadow(false);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->RegisterComponent();}C->AddInstance(FTransform(Rot,Position,Size/100.f));++Instances;};
 auto Box=[&](UMaterial* Mat,FVector P,FVector S){Shape(Cube,Mat,P,S);};
 // 50 x 50 m unobstructed floor, 12 m high. Existing walking bounds stay
 // unchanged; benches and planting remain in the perimeter decoration band.
 Box(Stone,FVector(0,0,-25),FVector(5100,5100,50));
 for(int Side=0;Side<4;++Side){const FRotator Rot(0,Side*90.f,0);auto B=[&](UMaterial* M,FVector P,FVector S){Shape(Cube,M,Rot.RotateVector(P),S,Rot);};
  B(Plaster,FVector(0,2525,600),FVector(5100,50,1200));
  B(Wood,FVector(0,2488,90),FVector(5000,26,180));B(Brass,FVector(0,2472,186),FVector(5000,8,9));
  B(Teal,FVector(0,2480,1090),FVector(5000,30,170));
  B(Light,FVector(0,2456,1000),FVector(5000,12,8));
  for(int Bay=-2;Bay<=2;++Bay){float X=Bay*930.f;
   B(View,FVector(X,2488,595),FVector(780,8,740));
   for(float DX:{-402.f,402.f})B(Wood,FVector(X+DX,2474,595),FVector(24,28,792));
   for(float Z:{199.f,991.f})B(Wood,FVector(X,2474,Z),FVector(828,28,24));
   for(float DX:{-260.f,0.f,260.f})B(Brass,FVector(X+DX,2468,595),FVector(7,18,770));
   B(Brass,FVector(X,2468,540),FVector(805,18,7));
   B(Plaster,FVector(X+465,2466,595),FVector(62,100,870));
   // low window seat with a teal cushion
   B(Wood,FVector(X,2455,28),FVector(640,64,56));B(Teal,FVector(X,2450,60),FVector(640,72,12));
  }
 }
 Box(Ceiling,FVector(0,0,1215),FVector(5100,5100,30));
 // Large luminous skylight and timber coffer grid give depth even with the
 // character-friendly exposure fixed; no additional dynamic shadowed lights.
 Box(Light,FVector(0,0,1197),FVector(2100,2100,8));
 for(int I=-5;I<=5;++I){Box(Wood,FVector(I*450,0,1168),FVector(24,5000,52));Box(Wood,FVector(0,I*450,1168),FVector(5000,24,52));}
 for(int X=-2;X<=2;++X)for(int Y=-2;Y<=2;++Y){if(FMath::Abs(X)<2&&FMath::Abs(Y)<2)continue;Box(Brass,FVector(X*900,Y*900,1080),FVector(180,180,12));Box(Light,FVector(X*900,Y*900,1071),FVector(168,168,7));Box(Brass,FVector(X*900,Y*900,1137),FVector(5,5,105));}
 // Brass floor border and inset timber edging; all outside NPC orbit centers.
 for(int Side=0;Side<4;++Side){FRotator R(0,Side*90.f,0);Shape(Cube,Wood,R.RotateVector(FVector(0,2375,.2f)),FVector(4750,120,.3f),R);Shape(Cube,Brass,R.RotateVector(FVector(0,2308,.4f)),FVector(4650,7,.3f),R);}
 for(int X:{-1,1})for(int Y:{-1,1}){FVector P(X*2420,Y*2420,0);Shape(Cylinder,Plaster,P+FVector(0,0,40),FVector(85,85,80));Shape(Cylinder,Wood,P+FVector(0,0,105),FVector(12,12,120));for(int I=0;I<5;++I){float A=I*2.399f;Shape(Sphere,Leaf,P+FVector(FMath::Cos(A)*26,FMath::Sin(A)*26,165+I*13),FVector(84,84,90));}}
 auto* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,1100),FRotator(-62,-28,0));Sun->Tags.Add(TEXT("CrowdRoomEnvironment"));Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);Sun->GetLightComponent()->SetIntensity(1.5f);Sun->GetLightComponent()->SetLightColor(FLinearColor(1.f,.94f,.85f));
 FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
 for(auto* O:Assets)if(!Save(O))return 1;

 int32 SavedGroups=0,SunCount=0;for(AActor* A:World->PersistentLevel->Actors)if(A){if(A->IsA<AStaticMeshActor>()){UE_LOG(LogTemp,Error,TEXT("Legacy room mesh remained"));return 1;}if(A->IsA<ADirectionalLight>())++SunCount;if(A->ActorHasTag(TEXT("CrowdRoomEnvironment"))&&!A->IsA<ADirectionalLight>())++SavedGroups;}
 if(SavedGroups!=Groups.Num()||SunCount!=1)return 1;
 if(!Save(World.Get()))return 1;
 UE_LOG(LogTemp,Display,TEXT("Atrium room saved: floor=50x50m height=12m groups=%d instances=%d textures=512 streamed dynamicLights=1 removed=%d backup=%s"),Groups.Num(),Instances,Old.Num(),*Backup);
 return 0;
}
