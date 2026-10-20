#include "KKImportedCharacter.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "AssetCompilingManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKDetachedAssetTest,"OpenKoikatsu.ImportedAssets.DetachedProject",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKDetachedAssetTest::RunTest(const FString& Parameters){
 if(!TestFalse(TEXT("Card parser module absent"),FModuleManager::Get().IsModuleLoaded(TEXT("KKUnityBundle"))))return false;
 if(!TestFalse(TEXT("Independent source resource pack absent"),IFileManager::Get().DirectoryExists(*(FPaths::ProjectContentDir()/TEXT("IndependentResources")))))return false;
 const FString Path=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ASSET_SMOKE_MANIFEST"));auto* Card=LoadObject<UKKImportedCharacter>(nullptr,*Path);
 if(!TestNotNull(TEXT("Saved character loaded"),Card))return false;
 FAssetCompilingManager::Get().FinishAllCompilation();
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AKKImportedCharacterActor>();Actor->Character=Card;int32 Models=0,Skins=0;
 const FTransform Placement(FRotator(10.,35.,-8.),FVector(123.,-456.,78.),FVector(1.3,.9,1.1));Actor->SetActorTransform(Placement);
 for(const auto& Outfit:Card->Outfits){TestNotNull(TEXT("Skeleton"),Outfit.Skeleton.Get());Actor->Outfit=Outfit.Index;Actor->SetActorTransform(Placement);
  Actor->OnConstruction(FTransform::Identity);Actor->OnConstruction(FTransform::Identity);
  TArray<UStaticMeshComponent*> Components;Actor->GetComponents(Components);TestEqual(TEXT("No duplicate assembly components"),Components.Num(),Outfit.Parts.Num());
  for(auto* Component:Components){TestTrue(TEXT("Component attached to actor root"),Component->GetAttachParent()==Actor->GetRootComponent());const auto* Part=Outfit.Parts.FindByPredicate([Component](const FKKImportedPart& P){return P.Model==Component->GetStaticMesh();});if(TestNotNull(TEXT("Assembly model belongs to outfit"),Part))TestTrue(TEXT("World placement follows actor"),Component->GetComponentTransform().Equals(Part->Transform*Placement,1.e-3));}
  const FTransform Moved(FRotator(-21.,77.,14.),FVector(-321.,123.,901.),FVector(.7,1.5,.8));TestTrue(TEXT("Registered actor can move"),Actor->SetActorTransform(Moved));
  for(auto* Component:Components){const auto* Part=Outfit.Parts.FindByPredicate([Component](const FKKImportedPart& P){return P.Model==Component->GetStaticMesh();});if(Part)TestTrue(TEXT("Registered parts follow later movement"),Component->GetComponentTransform().Equals(Part->Transform*Moved,1.e-3));}
  for(const auto& Part:Outfit.Parts){if(TestNotNull(TEXT("Static model"),Part.Model.Get())){TestTrue(TEXT("Saved source geometry"),Part.Model->IsMeshDescriptionValid(0));TestTrue(TEXT("Finite mesh bounds"),!Part.Model->GetBounds().Origin.ContainsNaN());
   for(const auto& Material:Part.Model->GetStaticMaterials())TestNotNull(TEXT("Saved material"),Material.MaterialInterface.Get());++Models;}
   if(Part.SkinnedModel){TestTrue(TEXT("Skin source geometry"),Part.SkinnedModel->GetMeshDescription(0)!=nullptr);TestTrue(TEXT("Skin skeleton matches"),Part.SkinnedModel->GetSkeleton()==Outfit.Skeleton);++Skins;}}
 }
 Actor->Outfit=INDEX_NONE;Actor->RebuildCharacter();TArray<UStaticMeshComponent*> Cleared;Actor->GetComponents(Cleared);TestEqual(TEXT("Invalid outfit removes stale assembly"),Cleared.Num(),0);
 Actor->Outfit=Card->Outfits[0].Index;Actor->RebuildCharacter();Actor->Character=nullptr;Actor->RebuildCharacter();Cleared.Reset();Actor->GetComponents(Cleared);TestEqual(TEXT("Cleared card removes stale assembly"),Cleared.Num(),0);
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 TestEqual(TEXT("All outfits"),Card->Outfits.Num(),9);TestEqual(TEXT("All static models"),Models,367);TestEqual(TEXT("All skinned models"),Skins,99);
 UE_LOG(LogTemp,Display,TEXT("Detached imported assets outfits=%d models=%d skins=%d parser=absent pack=absent post_registration_movement=passed stale_assembly_cleanup=passed"),Card->Outfits.Num(),Models,Skins);return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSkeletalUsageTest,"OpenKoikatsu.ImportedAssets.SkeletalUsage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSkeletalUsageTest::RunTest(const FString& Parameters){
 if(!TestFalse(TEXT("Card parser absent"),FModuleManager::Get().IsModuleLoaded(TEXT("KKUnityBundle"))))return false;
 const FString Path=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ASSET_SMOKE_MANIFEST"));auto* Card=LoadObject<UKKImportedCharacter>(nullptr,*Path);if(!TestNotNull(TEXT("Saved character"),Card))return false;
 FAssetCompilingManager::Get().FinishAllCompilation();int32 Models=0;
 for(const auto& Outfit:Card->Outfits)for(const auto& Part:Outfit.Parts)if(Part.SkinnedModel){++Models;
  for(const auto& Slot:Part.SkinnedModel->GetMaterials()){auto* Base=Slot.MaterialInterface?Slot.MaterialInterface->GetMaterial():nullptr;if(!TestNotNull(TEXT("Skin material"),Base))return false;
   if(!TestTrue(FString::Printf(TEXT("Saved SkeletalMesh usage %s"),*Base->GetPathName()),Base->GetUsageByFlag(MATUSAGE_SkeletalMesh)))return false;
   if(!Part.SkinnedModel->GetMorphTargets().IsEmpty()&&!TestTrue(FString::Printf(TEXT("Saved MorphTargets usage %s"),*Base->GetPathName()),Base->GetUsageByFlag(MATUSAGE_MorphTargets)))return false;
  }
 }
 TestTrue(TEXT("Skin models exist"),Models>0);
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);auto* Owner=World->SpawnActor<AActor>();int32 Deformed=0;
 for(const auto& Outfit:Card->Outfits)for(const auto& Part:Outfit.Parts)if(Part.SkinnedModel){auto* Comp=NewObject<USkeletalMeshComponent>(Owner);Comp->SetSkeletalMeshAsset(Part.SkinnedModel);Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);Comp->RegisterComponent();
  TArray<FFinalSkinVertex> Base;Comp->TickAnimation(0.f,false);Comp->GetCPUSkinnedVertices(Base,0);TestEqual(TEXT("Registered skin vertex count"),Base.Num(),int32(Part.SkinnedModel->GetResourceForRendering()->LODRenderData[0].GetNumVertices()));
  for(const auto& Vertex:Base)if(Vertex.Position.ContainsNaN()){AddError(TEXT("Non-finite registered skin"));break;}
  UMorphTarget* Selected=nullptr;float Largest=0.f;for(const auto& Morph:Part.SkinnedModel->GetMorphTargets())if(!Morph->GetMorphLODModels().IsEmpty())for(const auto& Delta:Morph->GetMorphLODModels()[0].Vertices)if(Delta.PositionDelta.SizeSquared()>Largest){Largest=Delta.PositionDelta.SizeSquared();Selected=Morph.Get();}
  if(Selected){Comp->SetMorphTarget(Selected->GetFName(),1.f);Comp->TickAnimation(0.f,false);TArray<FFinalSkinVertex> Changed;Comp->GetCPUSkinnedVertices(Changed,0);TestEqual(TEXT("Morph keeps vertex count"),Changed.Num(),Base.Num());float Motion=0.f;
   if(Changed.Num()==Base.Num())for(int32 I=0;I<Base.Num();++I){if(Changed[I].Position.ContainsNaN()){AddError(TEXT("Non-finite active morph"));break;}Motion=FMath::Max(Motion,(Changed[I].Position-Base[I].Position).SizeSquared());}
   TestTrue(TEXT("Registered morph changes positions"),Motion>0.f);Comp->SetMorphTarget(Selected->GetFName(),0.f);Comp->TickAnimation(0.f,false);TArray<FFinalSkinVertex> Restored;Comp->GetCPUSkinnedVertices(Restored,0);TestEqual(TEXT("Cleared morph keeps vertex count"),Restored.Num(),Base.Num());
   if(Restored.Num()==Base.Num())for(int32 I=0;I<Base.Num();++I)if(Restored[I].Position!=Base[I].Position){AddError(TEXT("Cleared morph does not restore baseline"));break;}++Deformed;
  }Comp->DestroyComponent();
  auto* Pose=NewObject<UPoseableMeshComponent>(Owner);Pose->SetSkinnedAssetAndUpdate(Part.SkinnedModel);Pose->SetCollisionEnabled(ECollisionEnabled::NoCollision);Pose->RegisterComponent();TArray<FFinalSkinVertex> PoseBase;Pose->GetCPUSkinnedVertices(PoseBase,0);
  const FName RootBone=Part.SkinnedModel->GetRefSkeleton().GetBoneName(0);const FTransform Original=Pose->GetBoneTransformByName(RootBone,EBoneSpaces::ComponentSpace);FTransform Translated=Original;Translated.AddToTranslation(FVector(10.,0.,0.));Pose->SetBoneTransformByName(RootBone,Translated,EBoneSpaces::ComponentSpace);TArray<FFinalSkinVertex> PoseMoved;Pose->GetCPUSkinnedVertices(PoseMoved,0);
  TestEqual(TEXT("Bone pose keeps vertex count"),PoseMoved.Num(),PoseBase.Num());float BoneMotion=0.f;if(PoseMoved.Num()==PoseBase.Num())for(int32 I=0;I<PoseBase.Num();++I){if(PoseMoved[I].Position.ContainsNaN()){AddError(TEXT("Non-finite bone pose"));break;}BoneMotion=FMath::Max(BoneMotion,(PoseMoved[I].Position-PoseBase[I].Position).SizeSquared());}TestTrue(TEXT("Saved bones deform skin"),BoneMotion>0.f);
  Pose->SetBoneTransformByName(RootBone,Original,EBoneSpaces::ComponentSpace);TArray<FFinalSkinVertex> PoseRestored;Pose->GetCPUSkinnedVertices(PoseRestored,0);TestEqual(TEXT("Restored pose vertex count"),PoseRestored.Num(),PoseBase.Num());if(PoseRestored.Num()==PoseBase.Num())for(int32 I=0;I<PoseBase.Num();++I)if(PoseRestored[I].Position!=PoseBase[I].Position){AddError(TEXT("Restored bone pose differs from baseline"));break;}Pose->DestroyComponent();
 }
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);TestTrue(TEXT("Morph models exercised"),Deformed>0);
 UE_LOG(LogTemp,Display,TEXT("Skeletal imported assets models=%d morph_models=%d material_usage=passed registered_skinning=passed morph_on_off=passed bone_pose_restore=passed parser=absent"),Models,Deformed);return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKJointUsageTest,"OpenKoikatsu.ImportedAssets.JointUsage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKJointUsageTest::RunTest(const FString& Parameters){
 if(!TestFalse(TEXT("Parser absent"),FModuleManager::Get().IsModuleLoaded(TEXT("KKUnityBundle"))))return false;
 if(!TestFalse(TEXT("Pack absent"),IFileManager::Get().DirectoryExists(*(FPaths::ProjectContentDir()/TEXT("IndependentResources")))))return false;
 auto* Card=LoadObject<UKKImportedCharacter>(nullptr,*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ASSET_SMOKE_MANIFEST")));if(!TestNotNull(TEXT("Saved card"),Card))return false;
 FAssetCompilingManager::Get().FinishAllCompilation();
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);auto* Owner=World->SpawnActor<AActor>();int32 Models=0,Skipped=0;int64 Samples=0;float MaxError=0.f;
 for(const auto& Outfit:Card->Outfits)for(const auto& Part:Outfit.Parts)if(Part.SkinnedModel){auto* Mesh=Part.SkinnedModel.Get();const auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];const auto* Weights=LOD.GetSkinWeightVertexBuffer();const auto& Ref=Mesh->GetRefSkeleton();TArray<uint64> Totals;Totals.Init(0,Ref.GetNum());
  for(const auto& Section:LOD.RenderSections)for(uint32 V=Section.BaseVertexIndex;V<Section.BaseVertexIndex+Section.NumVertices;++V)for(uint32 I=0;I<Weights->GetMaxBoneInfluences();++I){const uint16 W=Weights->GetBoneWeight(V,I);if(!W)continue;const uint32 B=Weights->GetBoneIndex(V,I);if(!Section.BoneMap.IsValidIndex(B)){AddError(TEXT("Invalid section bone map"));continue;}const int32 Bone=Section.BoneMap[B];if(!Totals.IsValidIndex(Bone)){AddError(TEXT("Invalid mesh bone index"));continue;}Totals[Bone]+=W;}
  int32 Selected=INDEX_NONE;for(int32 B=1;B<Totals.Num();++B)if(Totals[B]&&(Selected==INDEX_NONE||Totals[B]>Totals[Selected]))Selected=B;
  if(Selected==INDEX_NONE){++Skipped;continue;}
  auto* Pose=NewObject<UPoseableMeshComponent>(Owner);Pose->SetSkinnedAssetAndUpdate(Mesh);Pose->SetCollisionEnabled(ECollisionEnabled::NoCollision);Pose->RegisterComponent();TArray<FFinalSkinVertex> Base;Pose->GetCPUSkinnedVertices(Base,0);
  const FName BoneName=Ref.GetBoneName(Selected);const FTransform Original=Pose->GetBoneTransformByName(BoneName,EBoneSpaces::ComponentSpace);FTransform Rotated=Original;Rotated.SetRotation(FQuat(FVector(0.,0.,1.),.35)*Original.GetRotation());Pose->SetBoneTransformByName(BoneName,Rotated,EBoneSpaces::ComponentSpace);TArray<FFinalSkinVertex> Changed;Pose->GetCPUSkinnedVertices(Changed,0);TestEqual(TEXT("Joint vertex count"),Changed.Num(),Base.Num());float Motion=0.f;
  const auto& Transforms=Pose->GetComponentSpaceTransforms();TArray<FMatrix44f> Matrices;for(int32 B=0;B<Ref.GetNum();++B)Matrices.Add(Mesh->GetRefBasesInvMatrix()[B]*FMatrix44f(Transforms[B].ToMatrixWithScale()));
  if(Changed.Num()==Base.Num())for(const auto& Section:LOD.RenderSections)for(uint32 V=Section.BaseVertexIndex;V<Section.BaseVertexIndex+Section.NumVertices;++V){FVector3f Expected(0.f);float Magnitude=1.f;
   for(uint32 I=0;I<Weights->GetMaxBoneInfluences();++I){const uint16 W=Weights->GetBoneWeight(V,I);if(!W)continue;const uint32 Map=Weights->GetBoneIndex(V,I);if(!Section.BoneMap.IsValidIndex(Map))continue;const int32 B=Section.BoneMap[Map];if(!Matrices.IsValidIndex(B))continue;const FVector3f Term=FVector3f(Matrices[B].TransformPosition(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V)))*(float(W)/65535.f);Expected+=Term;Magnitude+=Term.GetAbsMax();}
   const float Error=(Expected-Changed[V].Position).GetAbsMax();MaxError=FMath::Max(MaxError,Error);++Samples;
   // Scalar weighted sum versus CPU SIMD: fixed float rounding budget, not a visual tolerance.
   if(Changed[V].Position.ContainsNaN()||!FMath::IsFinite(Error)||Error>64.f*FLT_EPSILON*Magnitude){AddError(FString::Printf(TEXT("Joint skin formula mismatch mesh=%s vertex=%u error=%g budget=%g"),*Mesh->GetName(),V,Error,64.f*FLT_EPSILON*Magnitude));break;}
   Motion=FMath::Max(Motion,(Changed[V].Position-Base[V].Position).SizeSquared());
  }
  TestTrue(TEXT("Weighted non-root rotation moves skin"),Motion>0.f);Pose->SetBoneTransformByName(BoneName,Original,EBoneSpaces::ComponentSpace);TArray<FFinalSkinVertex> Restored;Pose->GetCPUSkinnedVertices(Restored,0);TestEqual(TEXT("Restored joint count"),Restored.Num(),Base.Num());if(Restored.Num()==Base.Num())for(int32 V=0;V<Base.Num();++V)if(Restored[V].Position!=Base[V].Position){AddError(TEXT("Joint restore differs from baseline"));break;}Pose->DestroyComponent();++Models;
 }
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);TestTrue(TEXT("Non-root weighted models exercised"),Models>0);
 UE_LOG(LogTemp,Display,TEXT("Joint imported assets models=%d root_only_models=%d samples=%lld max_error_cm=%g weighted_formula=checked restore=checked parser=absent pack=absent"),Models,Skipped,Samples,MaxError);return !HasAnyErrors();
}

#endif
