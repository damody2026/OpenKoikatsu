#include "OKPaintSprayer.h"
#include "OKPaintDemo.h"
#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Engine/Texture2D.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "SkeletalMeshAttributes.h"
#include "Animation/AttributesRuntime.h"
#include "BoneWeights.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScript.h"
#include "NiagaraGraph.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessEmitterTemplate.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "NiagaraSpriteRendererProperties.h"
#include "UObject/UnrealType.h"
#include "UObject/SavePackage.h"
#include "UObject/GCObject.h"
#include "Misc/PackageName.h"
#include "AssetCompilingManager.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"

namespace OKPaintAssets {
TArray<TObjectPtr<UObject>> Assets;
class FAssetGuard:public FGCObject{public:virtual void AddReferencedObjects(FReferenceCollector& C) override {C.AddReferencedObjects(Assets);}virtual FString GetReferencerName() const override{return TEXT("OKPaintAssetBuilder");}};
template<class T>T* Make(const TCHAR* Name){const FString Path=TEXT("/Game/PaintDemo/")+FString(Name);if(auto* Existing=LoadObject<T>(nullptr,*(Path+TEXT(".")+Name),nullptr,LOAD_NoWarn))return Existing;auto* O=NewObject<T>(CreatePackage(*Path),Name,RF_Public|RF_Standalone);Assets.Add(O);return O;}
template<class T>T* Expr(UMaterial* M){auto* E=NewObject<T>(M);M->GetExpressionCollection().AddExpression(E);return E;}
UMaterial* Material(const TCHAR* Name,bool Film,bool Particle=false)
{
 auto* M=Make<UMaterial>(Name);const bool New=M->GetExpressions().IsEmpty();if(Particle){M->SetMaterialUsage(MATUSAGE_NiagaraSprites);Assets.AddUnique(M);}if(FCString::Strcmp(Name,TEXT("M_TestRig"))==0){M->SetMaterialUsage(MATUSAGE_SkeletalMesh);Assets.AddUnique(M);}if(Particle){M->SetShadingModel(MSM_Unlit);if(!New){M->GetEditorOnlyData()->EmissiveColor.Connect(0,M->GetEditorOnlyData()->BaseColor.Expression);M->PostEditChange();}}// Toon preview lights do not illuminate translucent surface shading reliably.
 // Keep lit reflections/roughness and add the pigment as emissive color, so
 // the same surface mask remains visibly colored under the preview lighting.
 if(Film||FCString::Strcmp(Name,TEXT("M_PaintDrop"))==0){Assets.AddUnique(M);if(!New){M->GetEditorOnlyData()->EmissiveColor.Connect(0,M->GetEditorOnlyData()->BaseColor.Expression);M->PostEditChange();}}
 if(!New)return M;M->TwoSided=true;M->BlendMode=Film||Particle?BLEND_Translucent:BLEND_Opaque;M->TranslucencyLightingMode=TLM_Surface;
 auto* Color=Expr<UMaterialExpressionVectorParameter>(M);Color->ParameterName=TEXT("PaintColor");Color->DefaultValue=FLinearColor::White;M->GetEditorOnlyData()->BaseColor.Connect(0,Color);
 auto* Roughness=Expr<UMaterialExpressionConstant>(M);Roughness->R=.1f;M->GetEditorOnlyData()->Roughness.Connect(0,Roughness);
 if(Film||FCString::Strcmp(Name,TEXT("M_PaintDrop"))==0)M->GetEditorOnlyData()->EmissiveColor.Connect(0,Color);
 if(Film){auto* Default=Make<UTexture2D>(TEXT("T_EmptyFilm"));const uint8 Pixel[]={0,0,0,0};Default->Source.Init(1,1,1,1,TSF_BGRA8,Pixel);Default->SRGB=false;Default->CompressionSettings=TC_VectorDisplacementmap;Default->PostEditChange();auto* Mask=Expr<UMaterialExpressionTextureSampleParameter2D>(M);Mask->ParameterName=TEXT("Film");Mask->Texture=Default;Mask->SamplerType=SAMPLERTYPE_LinearColor;M->GetEditorOnlyData()->Opacity.Connect(1,Mask);}
 if(Particle||FCString::Strcmp(Name,TEXT("M_PaintDrop"))==0){M->BlendMode=BLEND_Translucent;auto* UV=Expr<UMaterialExpressionTextureCoordinate>(M);auto* Disc=Expr<UMaterialExpressionCustom>(M);Disc->OutputType=CMOT_Float1;Disc->Code=TEXT("float2 p=UV*2-1;return saturate((1-dot(p,p))*12);");FCustomInput Input;Input.InputName=TEXT("UV");Input.Input.Connect(0,UV);Disc->Inputs.Add(Input);M->GetEditorOnlyData()->Opacity.Connect(0,Disc);
 if(Particle){auto* C=Expr<UMaterialExpressionParticleColor>(M);auto* Alpha=Expr<UMaterialExpressionMultiply>(M);Alpha->A.Connect(0,Disc);Alpha->B.Connect(4,C);M->GetEditorOnlyData()->BaseColor.Connect(0,C);M->GetEditorOnlyData()->EmissiveColor.Connect(0,C);M->GetEditorOnlyData()->Opacity.Connect(0,Alpha);}}
 M->PostEditChange();return M;
}
UNiagaraSystem* Effect(const TCHAR* Name,bool Jet,bool Single=false)
{
 auto* System=Make<UNiagaraSystem>(Name);if(!Assets.Contains(System))return System;
 UNiagaraSystemFactoryNew::InitializeSystem(System,true);
 System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.JetSpeed")));System->GetExposedParameters().SetParameterValue(1600.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.JetSpeed")));
 System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Flow")));System->GetExposedParameters().SetParameterValue(2400.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Flow")));
 System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.DropletFlow")));System->GetExposedParameters().SetParameterValue(240.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.DropletFlow")));
 System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.JetLife")));System->GetExposedParameters().SetParameterValue(.5f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.JetLife")));
 System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.PaintColor")));System->GetExposedParameters().SetParameterValue(FLinearColor::White,FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.PaintColor")));
 const int32 Emitters=Jet?2:1;
 for(int32 I=0;I<Emitters;++I){auto* E=NewObject<UNiagaraStatelessEmitter>(System);auto* TemplateClass=LoadClass<UNiagaraStatelessEmitterTemplate>(nullptr,TEXT("/Script/Niagara.NiagaraStatelessEmitterDefault"));if(!TemplateClass)return nullptr;E->SetEmitterTemplate(Cast<UNiagaraStatelessEmitterTemplate>(TemplateClass->GetDefaultObject()));
  for(auto Module:E->GetModules())if(Module->CanDisableModule())Module->SetIsModuleEnabled(Module->IsA<UNiagaraStatelessModule_AddVelocity>()||Module->IsA<UNiagaraStatelessModule_GravityForce>());
  auto* State=FindFProperty<FStructProperty>(E->GetClass(),TEXT("EmitterState"));State->ContainerPtrToValuePtr<FNiagaraEmitterStateData>(E)->LoopBehavior=Jet?ENiagaraLoopBehavior::Infinite:ENiagaraLoopBehavior::Once;
  auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(E->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));check(Init);Init->LifetimeDistribution.InitConstant(Jet?.5f:1.5f);Init->SpriteSizeDistribution.InitConstant(FVector2f(Jet?(I?.25f:.35f):Single?.18f:.15f,Jet?(I?.7f:12.f):Single?.18f:.15f));
  if(Jet){Init->LifetimeDistribution.Mode=ENiagaraDistributionMode::Binding;Init->LifetimeDistribution.ParameterBinding=FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.JetLife"));}
  Init->ColorDistribution.Mode=ENiagaraDistributionMode::Binding;Init->ColorDistribution.ParameterBinding=FNiagaraVariableBase(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.PaintColor"));
  auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(E->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));check(Velocity);Velocity->VelocityType=ENSM_VelocityType::InCone;Velocity->ConeRotationType=ENSM_ConeRotationType::Direction;Velocity->ConeDirection.InitConstant(FVector3f::XAxisVector);Velocity->ConeVelocityDistribution.InitConstant(Jet?1600.f:Single?10.f:150.f);Velocity->ConeAngle=Jet?2.f:Single?0.f:65.f;
  if(Jet){Velocity->ConeAngle=0;Velocity->ConeVelocityDistribution.Mode=ENiagaraDistributionMode::Binding;Velocity->ConeVelocityDistribution.ParameterBinding=FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.JetSpeed"));}
  auto& Spawn=E->AddSpawnInfo();Spawn.Type=Jet?ENiagaraStatelessSpawnInfoType::Rate:ENiagaraStatelessSpawnInfoType::Burst;Spawn.Amount.InitConstant(Single?1:12);Spawn.Rate.InitConstant(Jet?(I?100.f:600.f):0.f);
  if(Jet){Spawn.Rate.Mode=ENiagaraDistributionMode::Binding;Spawn.Rate.ParameterBinding=FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(),I?TEXT("User.DropletFlow"):TEXT("User.Flow"));}
  auto* Renderer=NewObject<UNiagaraSpriteRendererProperties>(E);Renderer->Material=Material(TEXT("M_PaintParticle"),false,true);Renderer->Alignment=Jet&&I==0?ENiagaraSpriteAlignment::VelocityAligned:ENiagaraSpriteAlignment::Unaligned;E->AddRenderer(Renderer,FGuid());
  auto* Bounds=FindFProperty<FStructProperty>(E->GetClass(),TEXT("FixedBounds"));*Bounds->ContainerPtrToValuePtr<FBox>(E)=FBox(FVector(-5000),FVector(5000));
  E->PostEditChange();FNiagaraEmitterHandle Handle(*E);System->AddEmitterHandleDirect(Handle);
 }
 System->RequestCompile(true);System->WaitForCompilationComplete(true,false);System->CacheFromCompiledData();
 UE_LOG(LogTemp,Display,TEXT("Paint Niagara %s ready=%d valid=%d"),Name,System->IsReadyToRun(),System->IsValid());return System;
}
USkeletalMesh* Rig()
{
 auto* Mesh=Make<USkeletalMesh>(TEXT("SK_TestRig"));if(!Assets.Contains(Mesh))return Mesh;auto* Skeleton=Make<USkeleton>(TEXT("S_TestRig"));Mesh->SetSkeleton(Skeleton);FReferenceSkeleton Ref;
 const TCHAR* Names[]={TEXT("Root"),TEXT("Upper"),TEXT("ArmL"),TEXT("ArmR"),TEXT("LegL"),TEXT("LegR")};const int32 Parent[]={-1,0,1,1,0,0};const FVector Rest[]={FVector(0),FVector(0,0,80),FVector(0,-25,50),FVector(0,25,50),FVector(0,-13,70),FVector(0,13,70)};
 {FReferenceSkeletonModifier M(Ref,Skeleton);for(int32 I=0;I<6;++I)M.Add(FMeshBoneInfo(Names[I],Names[I],Parent[I]),FTransform(Rest[I]));}
 Mesh->SetRefSkeleton(Ref);Mesh->CalculateInvRefMatrices();Mesh->AddLODInfo();Mesh->GetLODInfo(0)->bAllowCPUAccess=true;Mesh->GetLODInfo(0)->ReductionSettings.NumOfTrianglesPercentage=.5f;Mesh->GetLODInfo(0)->ReductionSettings.NumOfVertPercentage=.5f;
 Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());
 Mesh->SetImportedBounds(FBoxSphereBounds(FBox(FVector(-35,-90,0),FVector(35,90,190))));
 FMeshDescription Desc;FSkeletalMeshAttributes A(Desc);A.Register(true);A.GetVertexInstanceUVs().SetNumChannels(1);
 for(int32 I=0;I<6;++I){auto B=A.CreateBone();A.GetBoneNames()[B]=Names[I];A.GetBoneParentIndices()[B]=Parent[I];A.GetBonePoses()[B]=FTransform(Rest[I]);}
 const auto Group=Desc.CreatePolygonGroup();A.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("Teal");
 auto Ellipsoid=[&](FVector Center,FVector Radius,int32 Bone){TArray<FVertexID> IDs;const int32 Rings=24,Sides=24;
  for(int32 J=0;J<=Rings;++J)for(int32 I=0;I<Sides;++I){const double Phi=PI*(.001+.998*J/Rings),Theta=2*PI*I/Sides;const FVector N(FMath::Sin(Phi)*FMath::Cos(Theta),FMath::Sin(Phi)*FMath::Sin(Theta),FMath::Cos(Phi));auto V=Desc.CreateVertex();A.GetVertexPositions()[V]=FVector3f(Center+Radius*N);A.GetVertexSkinWeights().Set(V,UE::AnimationCore::FBoneWeights::Create({UE::AnimationCore::FBoneWeight(Bone,1.f)}));IDs.Add(V);}
  for(int32 J=0;J<Rings;++J)for(int32 I=0;I<Sides;++I){const int32 K=(I+1)%Sides;const int32 Quads[]={J*Sides+I,(J+1)*Sides+I,(J+1)*Sides+K,J*Sides+K};for(int32 T=0;T<2;++T){TArray<FVertexInstanceID> Instances;for(int32 Q:{0,T?2:1,T?3:2}){auto V=IDs[Quads[Q]];auto W=Desc.CreateVertexInstance(V);const FVector3f P=A.GetVertexPositions()[V];A.GetVertexInstanceNormals()[W]=FVector3f((FVector(P)-Center)/Radius).GetSafeNormal();A.GetVertexInstanceTangents()[W]=FVector3f(1,0,0);A.GetVertexInstanceBinormalSigns()[W]=1;A.GetVertexInstanceUVs().Set(W,0,FVector2f(float(Quads[Q]%Sides)/Sides,float(Quads[Q]/Sides)/Rings));Instances.Add(W);}Desc.CreatePolygon(Group,Instances);}}
 };
 Ellipsoid(FVector(0,0,110),FVector(17,25,40),1);Ellipsoid(FVector(0,0,166),FVector(16,16,20),1);Ellipsoid(FVector(0,-47,130),FVector(8,27,8),2);Ellipsoid(FVector(0,47,130),FVector(8,27,8),3);Ellipsoid(FVector(0,-13,40),FVector(10,10,32),4);Ellipsoid(FVector(0,13,40),FVector(10,10,32),5);
 auto* BodyMaterial=Material(TEXT("M_TestRig"),false);for(UMaterialExpression* E:BodyMaterial->GetExpressions())if(auto* C=Cast<UMaterialExpressionVectorParameter>(E))C->DefaultValue=FLinearColor(.02,.35,.4,1);BodyMaterial->PostEditChange();
 Mesh->GetMaterials().Add(FSkeletalMaterial(BodyMaterial,true,false,TEXT("Teal")));Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Skeleton->MergeAllBonesToBoneTree(Mesh);return Mesh;
}
}
#endif
UOKBuildPaintDemoCommandlet::UOKBuildPaintDemoCommandlet(){IsEditor=true;IsClient=true;IsServer=false;LogToConsole=true;}
int32 UOKBuildPaintDemoCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
 using namespace OKPaintAssets;Assets.Reset();FAssetGuard Guard;Material(TEXT("M_PaintFilm"),true);Material(TEXT("M_PaintDrop"),false);Material(TEXT("M_PaintParticle"),false,true);Rig();Material(TEXT("M_TestRig"),false);auto* Jet=Effect(TEXT("NS_PaintJet"),true);auto* Splash=Effect(TEXT("NS_PaintSplash"),false);auto* Drip=Effect(TEXT("NS_PaintDrip"),false,true);if(!Jet||!Splash||!Drip)return 1;
 FAssetCompilingManager::Get().FinishAllCompilation();for(auto O:Assets){O->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(O->GetOutermost(),O,*FPackageName::LongPackageNameToFilename(O->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Save))return 1;}
 const FString Path=TEXT("/Game/PaintDemo/PaintDemo");if(!FPackageName::DoesPackageExist(Path)){auto* Package=CreatePackage(*Path);auto* World=UWorld::CreateWorld(EWorldType::Editor,false,TEXT("PaintDemo"),Package);World->SetFlags(RF_Public|RF_Standalone);World->GetWorldSettings()->DefaultGameMode=AOKPaintDemoGameMode::StaticClass();
  auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));auto Block=[&](FVector P,FVector Scale){auto* A=World->SpawnActor<AStaticMeshActor>(P,FRotator::ZeroRotator);A->GetStaticMeshComponent()->SetStaticMesh(Cube);A->SetActorScale3D(Scale);};Block(FVector(0,0,-5),FVector(12,12,.1));Block(FVector(200,0,160),FVector(.1,12,3.2));
  World->SpawnActor<AOKPaintDemoCharacter>();World->SpawnActor<APlayerStart>(FVector(-260,0,110),FRotator(0,0,0));auto* Light=World->SpawnActor<ADirectionalLight>(FVector(0,0,300),FRotator(-40,0,0));Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);Light->GetLightComponent()->SetIntensity(5);auto* Sky=World->SpawnActor<ASkyLight>();Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);Sky->GetLightComponent()->SetIntensity(.7f);Sky->GetLightComponent()->bLowerHemisphereIsBlack=false;
  FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Package,World,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetMapPackageExtension()),Save))return 1;World->DestroyWorld(false);
 }
 UE_LOG(LogTemp,Display,TEXT("Paint demo assets=%d complete jet=%d splash=%d drip=%d"),Assets.Num(),Jet->IsReadyToRun(),Splash->IsReadyToRun(),Drip->IsReadyToRun());return Jet->IsReadyToRun()&&Splash->IsReadyToRun()&&Drip->IsReadyToRun()?0:1;
#else
 return 1;
#endif
}
