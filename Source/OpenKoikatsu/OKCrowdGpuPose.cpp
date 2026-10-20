#include "OKCrowdGpuPose.h"
#if WITH_EDITOR
#include "KKImportedCharacter.h"
#include "OKCrowdCompact.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimBank.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "AssetCompilingManager.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "SkeletalMeshAttributes.h"
#include "Json.h"
namespace {
bool SaveGpuAsset(UObject* Asset){
 const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
 if(IFileManager::Get().FileExists(*File))return false;
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
 FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);
}
UMaterialInterface* CopyGpuMaterial(UMaterialInterface* Source,const FString& Root,TMap<UMaterialInterface*,UMaterialInterface*>& Copies,TArray<TObjectPtr<UObject>>& Assets){
 if(!Source)return nullptr;if(auto** Existing=Copies.Find(Source))return *Existing;
 const FString Name=FString::Printf(TEXT("Material_%03d"),Copies.Num()),Path=Root/Name;
 auto* Copy=DuplicateObject<UMaterialInterface>(Source,CreatePackage(*Path),*Name);Copy->ClearFlags(RF_Transient);Copy->SetFlags(RF_Public|RF_Standalone);Copies.Add(Source,Copy);Assets.Add(Copy);
 if(auto* Instance=Cast<UMaterialInstanceConstant>(Copy)){auto* Parent=CopyGpuMaterial(Instance->Parent,Root,Copies,Assets);if(!Parent)return nullptr;Instance->SetParentEditorOnly(Parent);}
 else if(auto* Material=Cast<UMaterial>(Copy))Material->SetMaterialUsage(MATUSAGE_InstancedSkinnedMesh);
 else return nullptr;
 Copy->PostEditChange();return Copy;
}
// A controlled build-setting experiment. CPU pose, materials and sections stay identical.
bool VerifyInstancingMesh(USkeletalMesh* Before,USkeletalMesh* After,int64& BeforeMaps,int64& AfterMaps,int64& Vertices){
 if(!Before||!After||Before->GetMaterials()!=After->GetMaterials()||Before->GetRefSkeleton().GetNum()!=After->GetRefSkeleton().GetNum()||Before->GetLODNum()!=After->GetLODNum()||Before->GetMorphTargets().Num()!=After->GetMorphTargets().Num())return false;
 for(int32 L=0;L<Before->GetLODNum();++L){
  if(!After->GetLODInfo(L)->BuildSettings.bOptimizeForInstancing)return false;
  const auto& A=Before->GetResourceForRendering()->LODRenderData[L];const auto& B=After->GetResourceForRendering()->LODRenderData[L];
  if(A.GetNumVertices()!=B.GetNumVertices()||A.RenderSections.Num()!=B.RenderSections.Num()||A.SkinWeightVertexBuffer.GetMaxBoneInfluences()!=B.SkinWeightVertexBuffer.GetMaxBoneInfluences())return false;
  for(int32 S=0;S<A.RenderSections.Num();++S){const auto& X=A.RenderSections[S];const auto& Y=B.RenderSections[S];if(X.MaterialIndex!=Y.MaterialIndex||X.NumTriangles!=Y.NumTriangles||X.NumVertices!=Y.NumVertices||X.BaseVertexIndex!=Y.BaseVertexIndex)return false;BeforeMaps+=X.BoneMap.Num();AfterMaps+=Y.BoneMap.Num();
   for(uint32 V=X.BaseVertexIndex;V<X.BaseVertexIndex+X.NumVertices;++V){
    if(A.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V)!=B.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V)||A.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(V)!=B.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(V)||A.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(V)!=B.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(V))return false;
    for(uint32 I=0;I<A.SkinWeightVertexBuffer.GetMaxBoneInfluences();++I){const uint16 W=A.SkinWeightVertexBuffer.GetBoneWeight(V,I);if(W!=B.SkinWeightVertexBuffer.GetBoneWeight(V,I))return false;if(W){const uint32 P=A.SkinWeightVertexBuffer.GetBoneIndex(V,I),Q=B.SkinWeightVertexBuffer.GetBoneIndex(V,I);if(!X.BoneMap.IsValidIndex(P)||!Y.BoneMap.IsValidIndex(Q)||X.BoneMap[P]!=Y.BoneMap[Q])return false;}}
    ++Vertices;
   }
  }
 }
 return true;
}
int32 PrepareInstancingOnly(int32 Start,int32 End,const FString& Output){
 TArray<TSharedPtr<FJsonValue>> Results;bool Complete=true;int64 BeforeMaps=0,AfterMaps=0,Vertices=0;
 for(int32 Index=Start;Index<End;++Index){bool Good=true;int32 MeshCount=0,Sections=0;
  {const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Root=Base/TEXT("InstancingBonesR1");
   TStrongObjectPtr<UKKImportedCharacter> Before(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("PackedRig/Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("PackedRig/Walk/Animations.Animations"))));
   Good=Before&&Walk&&Walk->Character==Before.Get();TStrongObjectPtr<UKKImportedCharacter> Card;TStrongObjectPtr<UKKImportedAnimationLibrary> Library;
   if(Good){const FString CardPath=Root/TEXT("Character");if(FPackageName::DoesPackageExist(CardPath)){Card.Reset(LoadObject<UKKImportedCharacter>(nullptr,*(CardPath+TEXT(".Character"))));Library.Reset(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Root/TEXT("Walk/Animations.Animations"))));Good=Card&&Library&&Card->SourceHash==Before->SourceHash&&Library->Character==Card.Get();}
    else{Card.Reset(DuplicateObject<UKKImportedCharacter>(Before.Get(),CreatePackage(*CardPath),TEXT("Character")));Library.Reset(DuplicateObject<UKKImportedAnimationLibrary>(Walk.Get(),CreatePackage(*(Root/TEXT("Walk/Animations"))),TEXT("Animations")));Library->Character=Card.Get();TMap<USkeletalMesh*,USkeletalMesh*> Copies;
     for(auto& Outfit:Card->Outfits)for(auto& Part:Outfit.Parts){if(!Part.SkinnedModel)continue;auto* Source=Part.SkinnedModel.Get();if(auto** Known=Copies.Find(Source)){Part.SkinnedModel=*Known;continue;}const FString Name=FString::Printf(TEXT("Skin_%03d"),Copies.Num()),Path=Root/Name;const bool Existing=FPackageName::DoesPackageExist(Path);auto* Mesh=Existing?LoadObject<USkeletalMesh>(nullptr,*(Path+TEXT(".")+Name)):DuplicateObject<USkeletalMesh>(Source,CreatePackage(*Path),*Name);if(!Mesh){Good=false;break;}if(!Existing){Mesh->ClearFlags(RF_Transient);Mesh->SetFlags(RF_Public|RF_Standalone);for(int32 L=0;L<Mesh->GetLODNum();++L)Mesh->GetLODInfo(L)->BuildSettings.bOptimizeForInstancing=true;Mesh->Build();Mesh->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();}int64 CheckA=0,CheckB=0,CheckV=0;if(!VerifyInstancingMesh(Source,Mesh,CheckA,CheckB,CheckV)||(!Existing&&!SaveGpuAsset(Mesh))){Good=false;break;}Copies.Add(Source,Mesh);Part.SkinnedModel=Mesh;}
     if(Good)Good=(FPackageName::DoesPackageExist(Root/TEXT("Walk/Animations"))||SaveGpuAsset(Library.Get()))&&SaveGpuAsset(Card.Get());
    }
    if(Good){Good=Before->Outfits.Num()==Card->Outfits.Num();TSet<USkeletalMesh*> Seen;for(int32 O=0;Good&&O<Card->Outfits.Num();++O){Good=Before->Outfits[O].Parts.Num()==Card->Outfits[O].Parts.Num();for(int32 P=0;Good&&P<Card->Outfits[O].Parts.Num();++P){auto* Source=Before->Outfits[O].Parts[P].SkinnedModel.Get();auto* Mesh=Card->Outfits[O].Parts[P].SkinnedModel.Get();if(Source&&!Seen.Contains(Mesh)){int64 A=0,B=0,V=0;Good=VerifyInstancingMesh(Source,Mesh,A,B,V);BeforeMaps+=A;AfterMaps+=B;Vertices+=V;Seen.Add(Mesh);++MeshCount;Sections+=Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections.Num();}}}}
   }
  }auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetNumberField(TEXT("meshes"),MeshCount);Row->SetNumberField(TEXT("sections"),Sections);Row->SetBoolField(TEXT("complete"),Good);Results.Add(MakeShared<FJsonValueObject>(Row));Complete&=Good;UE_LOG(LogTemp,Display,TEXT("Crowd instancing-only index=%d complete=%d meshes=%d sections=%d"),Index,Good,MeshCount,Sections);CollectGarbage(RF_NoFlags);auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("complete"),Complete);Report->SetArrayField(TEXT("cards"),Results);Report->SetNumberField(TEXT("before_section_bone_references"),double(BeforeMaps));Report->SetNumberField(TEXT("after_section_bone_references"),double(AfterMaps));Report->SetNumberField(TEXT("verified_render_vertices"),double(Vertices));FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Output);if(!Good)return 1;
 }
 return Complete?0:1;
}
}
#endif
int32 OKPrepareCrowdGpuPose(int32 Start,int32 End,const FString& Params){
#if WITH_EDITOR
 FString Tag;FParse::Value(*Params,TEXT("GpuTag="),Tag);if(Tag.IsEmpty())return 1;
 for(TCHAR C:Tag)if(!FChar::IsAlnum(C)&&C!=TEXT('_')&&C!=TEXT('-'))return 1;
 const FString Output=FPaths::ProjectSavedDir()/TEXT("Crowd")/(Tag+TEXT(".json"));if(IFileManager::Get().FileExists(*Output))return 1;
 if(FParse::Param(*Params,TEXT("OptimizeInstancingOnly")))return PrepareInstancingOnly(Start,End,Output);
 if(FParse::Param(*Params,TEXT("AuditGpuPose"))){
  FString AuditVariant=TEXT("GpuMatrixR3");FParse::Value(*Params,TEXT("GpuVariant="),AuditVariant);for(TCHAR C:AuditVariant)if(!FChar::IsAlnum(C)&&C!=TEXT('_'))return 1;
  double MaximumPositionError=0;int64 Vertices=0;
  for(int32 Index=Start;Index<End;++Index){const FString Path=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d/%s/Pose.Pose"),Index,*AuditVariant);TStrongObjectPtr<UOKCrowdGpuPose> Pose(LoadObject<UOKCrowdGpuPose>(nullptr,*Path));if(!Pose)return 1;
   for(const auto& Part:Pose->Parts){auto* Data=Cast<UOKCrowdMatrixData>(Part.Mesh->GetAssetUserDataOfClass(UOKCrowdMatrixData::StaticClass()));if(!Data)return 1;int32 Bad=0;float Maximum=0;for(float V:Data->CrowdMatrixRows){if(!FMath::IsFinite(V))++Bad;Maximum=FMath::Max(Maximum,FMath::Abs(V));}UE_LOG(LogTemp,Display,TEXT("Crowd matrix serialized audit mesh=%s bones=%d frames=%d rows=%d invalid=%d maximum=%f first=%f,%f,%f,%f"),*Part.Mesh->GetName(),Part.Mesh->GetRefSkeleton().GetNum(),Data->CrowdMatrixFrames,Data->CrowdMatrixRows.Num(),Bad,Maximum,Data->CrowdMatrixRows[0],Data->CrowdMatrixRows[4],Data->CrowdMatrixRows[8],Data->CrowdMatrixRows[9]);if(Bad)return 1;
    auto* Bank=Cast<UAnimBankData>(Part.Provider.Get());if(!Bank||Bank->AnimBankItems.IsEmpty())return 1;auto* Sequence=Bank->AnimBankItems[0].BankAsset->Sequences[0].Sequence.Get();
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);auto* Owner=World->SpawnActor<AActor>();auto* Skin=NewObject<USkeletalMeshComponent>(Owner);Owner->AddInstanceComponent(Skin);Skin->SetSkeletalMeshAsset(Part.Mesh);Skin->RegisterComponent();Skin->PlayAnimation(Sequence,true);
    const int32 Bones=Part.Mesh->GetRefSkeleton().GetNum();FSkeletalMeshConstAttributes Attributes(*Part.Mesh->GetMeshDescription(0));const auto Weights=Attributes.GetVertexSkinWeights();const auto Positions=Attributes.GetVertexPositions();TArray<FMatrix44f> Current,Interpolated;Current.SetNum(Bones);Interpolated.SetNum(Bones);
    for(int32 Sample=0;Sample<13;++Sample){const float Time=FMath::Min(Data->CrowdMatrixDuration,Data->CrowdMatrixDuration*Sample/12.f+.003f);Skin->SetPosition(Time,false);Skin->TickAnimation(0,false);Skin->RefreshBoneTransforms();const auto& PoseTransforms=Skin->GetComponentSpaceTransforms();const float Key=Time/Data->CrowdMatrixDuration*(Data->CrowdMatrixFrames-1);const int32 A=FMath::Clamp(FMath::FloorToInt(Key),0,Data->CrowdMatrixFrames-1),B=FMath::Min(A+1,Data->CrowdMatrixFrames-1);const float Alpha=Key-A;
     for(int32 Bone=0;Bone<Bones;++Bone){Current[Bone]=Part.Mesh->GetRefBasesInvMatrix()[Bone]*FMatrix44f(PoseTransforms[Bone].ToMatrixWithScale());Interpolated[Bone]=FMatrix44f::Identity;const float* X=Data->CrowdMatrixRows.GetData()+(A*Bones+Bone)*12;const float* Y=Data->CrowdMatrixRows.GetData()+(B*Bones+Bone)*12;for(int32 R=0;R<4;++R)for(int32 C=0;C<3;++C)Interpolated[Bone].M[R][C]=FMath::Lerp(X[R*3+C],Y[R*3+C],Alpha);}
     for(auto Vertex:Part.Mesh->GetMeshDescription(0)->Vertices().GetElementIDs()){FVector3f CPU=FVector3f::ZeroVector,GPU=FVector3f::ZeroVector;for(const auto Weight:Weights.Get(Vertex)){const float W=float(Weight.GetRawWeight())/65535.f;const int32 Bone=Weight.GetBoneIndex();CPU+=FVector3f(Current[Bone].TransformPosition(Positions[Vertex]))*W;GPU+=FVector3f(Interpolated[Bone].TransformPosition(Positions[Vertex]))*W;}MaximumPositionError=FMath::Max(MaximumPositionError,double(FVector3f::Distance(CPU,GPU)));++Vertices;}
    }Owner->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
   }
  }auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("samples_per_part"),13);Report->SetNumberField(TEXT("vertices"),double(Vertices));Report->SetNumberField(TEXT("max_position_error_cm"),MaximumPositionError);Report->SetBoolField(TEXT("complete"),MaximumPositionError<=.1);FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Output);return MaximumPositionError<=.1?0:1;
 }
 TArray<TSharedPtr<FJsonValue>> Results;bool All=true;
 for(int32 Index=Start;Index<End;++Index){
  const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Root=Base/TEXT("GpuMatrixR3");
  auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);bool Good=true;FString Error;
  {
   TStrongObjectPtr<UKKImportedCharacter> Card(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Character.Character"))));
   TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("Walk/Animations.Animations"))));
   Good=Card&&Walk&&!Card->Outfits.IsEmpty()&&!Walk->Animations.IsEmpty()&&Walk->Animations[0].Sequence;
   const FString AssetPath=Root/TEXT("Pose");
   if(FPackageName::DoesPackageExist(AssetPath)){Row->SetBoolField(TEXT("existing"),true);Good=false;Error=TEXT("Output exists; use verified receipt instead of overwriting");}
   TStrongObjectPtr<UOKCrowdGpuPose> Pose(Good?NewObject<UOKCrowdGpuPose>(CreatePackage(*AssetPath),TEXT("Pose"),RF_Public|RF_Standalone):nullptr);
   if(Good)Pose->SourceHash=Card->SourceHash;
   int32 Sections=0,MaxBones=0;TMap<UMaterialInterface*,UMaterialInterface*> MaterialCopies;TArray<TObjectPtr<UObject>> MaterialAssets;
   if(Good)for(const auto& Part:Card->Outfits[0].Parts){
    if(!Part.SkinnedModel||!Part.MouthDefaultVisible)continue;
    const FString Name=FString::Printf(TEXT("Skin_%03d"),Pose->Parts.Num()),Path=Root/Name;
    auto* Mesh=OKCompactCrowdMesh(Part.SkinnedModel,Path,Error,{});
    if(!Mesh){Good=false;break;}
    for(auto& Material:Mesh->GetMaterials()){Material.MaterialInterface=CopyGpuMaterial(Material.MaterialInterface,Root/TEXT("Materials"),MaterialCopies,MaterialAssets);if(!Material.MaterialInterface){Good=false;break;}}
    if(!Good){Error=TEXT("GPU material usage derivative failed");break;}
    // Only layout changes required by GPU skinning; authored geometry/materials stay intact.
    // AnimBank supports section palettes directly; ASTP's unified mapping is unnecessary.
    for(int32 LOD=0;LOD<Mesh->GetLODNum();++LOD)Mesh->GetLODInfo(LOD)->BuildSettings.bOptimizeForInstancing=true;
    Mesh->Build();FAssetCompilingManager::Get().FinishAllCompilation();
    auto* Data=Mesh->GetResourceForRendering();Good=Data&&Mesh->GetMaterials().Num()==Part.SkinnedModel->GetMaterials().Num();
    if(Good)for(const auto& LOD:Data->LODRenderData)MaxBones=FMath::Max(MaxBones,LOD.RequiredBones.Num());
    if(!Good){Error=TEXT("Matrix bank unified mapping/material check failed");break;}
    Sections+=Data->LODRenderData[0].RenderSections.Num();
    // The bank only describes this mesh's own baked walk; no cross-character pose reuse.
    const FString BankName=Name+TEXT("_Bank"),BankPath=Root/BankName;
    TStrongObjectPtr<UAnimBank> Bank(NewObject<UAnimBank>(CreatePackage(*BankPath),*BankName,RF_Public|RF_Standalone));Bank->Asset=Mesh;FAnimBankSequence Entry;Entry.Sequence=Walk->Animations[0].Sequence;Entry.Position=Entry.Sequence->GetPlayLength()*FMath::Frac(Index*.618034f);Bank->Sequences.Add(Entry);Bank->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();
    auto* Matrices=NewObject<UOKCrowdMatrixData>(Mesh);Mesh->AddAssetUserData(Matrices);Matrices->CrowdMatrixDuration=Entry.Sequence->GetPlayLength();Matrices->CrowdMatrixFrames=FMath::CeilToInt(Matrices->CrowdMatrixDuration*240)+1;
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Owner=World->SpawnActor<AActor>();auto* Sampler=NewObject<USkeletalMeshComponent>(Owner);Owner->AddInstanceComponent(Sampler);Sampler->SetSkeletalMeshAsset(Mesh);Sampler->RegisterComponent();Sampler->PlayAnimation(Entry.Sequence,true);
    const int32 Bones=Mesh->GetRefSkeleton().GetNum();Matrices->CrowdMatrixRows.SetNumUninitialized(Matrices->CrowdMatrixFrames*Bones*12);
    for(int32 Frame=0;Frame<Matrices->CrowdMatrixFrames;++Frame){Sampler->SetPosition(Matrices->CrowdMatrixDuration*Frame/(Matrices->CrowdMatrixFrames-1),false);Sampler->TickAnimation(0,false);Sampler->RefreshBoneTransforms();
     const auto& PoseTransforms=Sampler->GetComponentSpaceTransforms();if(PoseTransforms.Num()!=Bones){Good=false;Error=TEXT("Matrix bake private bone count changed");break;}
     for(int32 Bone=0;Bone<Bones;++Bone){const FMatrix44f Matrix=Mesh->GetRefBasesInvMatrix()[Bone]*FMatrix44f(PoseTransforms[Bone].ToMatrixWithScale());float* Rows=Matrices->CrowdMatrixRows.GetData()+(Frame*Bones+Bone)*12;for(int32 R=0;R<4;++R)for(int32 C=0;C<3;++C)Rows[R*3+C]=Matrix.M[R][C];}
    }
    Owner->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);if(!Good)break;
    const FString ProviderName=Name+TEXT("_Provider"),ProviderPath=Root/ProviderName;auto* Provider=NewObject<UAnimBankData>(CreatePackage(*ProviderPath),*ProviderName,RF_Public|RF_Standalone);FAnimBankItem Item;Item.BankAsset=Bank.Get();Provider->AnimBankItems.Add(Item);
    FOKCrowdGpuPart NewPart;NewPart.Mesh=Mesh;NewPart.Provider=Provider;NewPart.Transform=Part.Transform;Pose->Parts.Add(NewPart);
    Provider->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();
    Good=Provider->IsEnabled()&&!Provider->IsCompiling()&&SaveGpuAsset(Mesh)&&SaveGpuAsset(Bank.Get())&&SaveGpuAsset(Provider);
    if(!Good){Error=TEXT("Provider compilation or asset save failed");break;}
   }
   if(Good)for(const auto& Material:MaterialAssets)if(!SaveGpuAsset(Material)){Good=false;Error=TEXT("GPU material save failed");break;}
   if(Good)Good=!Pose->Parts.IsEmpty()&&SaveGpuAsset(Pose.Get());
   Row->SetNumberField(TEXT("parts"),Pose?Pose->Parts.Num():0);Row->SetNumberField(TEXT("sections"),Sections);Row->SetNumberField(TEXT("maximum_required_bones"),MaxBones);
  }
  Row->SetBoolField(TEXT("complete"),Good);Row->SetStringField(TEXT("error"),Error);Results.Add(MakeShared<FJsonValueObject>(Row));All&=Good;
  UE_LOG(LogTemp,Display,TEXT("Crowd GPU preparation index=%d good=%d error=%s"),Index,Good,*Error);
  CollectGarbage(RF_NoFlags);
  auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("complete"),All);Report->SetArrayField(TEXT("cards"),Results);FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Output);
  if(!Good)break;
 }
 return All?0:1;
#else
 return 1;
#endif
}
