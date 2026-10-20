#include "KKImportedCharacter.h"
#include "Misc/AutomationTest.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimCompressionTypes.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "AssetCompilingManager.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKDetachedAnimationTest,"OpenKoikatsu.ImportedAssets.OriginalAnimations",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKDetachedAnimationTest::RunTest(const FString& Parameters){
 if(!TestFalse(TEXT("Parser absent"),FModuleManager::Get().IsModuleLoaded(TEXT("KKUnityBundle"))))return false;
 if(!TestFalse(TEXT("Source pack absent"),IFileManager::Get().DirectoryExists(*(FPaths::ProjectContentDir()/TEXT("IndependentResources")))))return false;
 TArray<FString> Libraries;FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIMATION_LIBRARIES")).ParseIntoArray(Libraries,TEXT(";"),true);
 if(!TestTrue(TEXT("Animation libraries supplied"),Libraries.Num()>0))return false;
 TSet<FString> UniqueLibraries;for(const auto& Path:Libraries){if(!TestFalse(TEXT("Duplicate animation library"),UniqueLibraries.Contains(Path)))return false;UniqueLibraries.Add(Path);}
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AKKImportedCharacterActor>();Actor->PlaySkeletalAnimation=true;int32 Clips=0,ExpectedClips=0,Skins=0,Vertices=0;double Motion=0.;
 for(const auto& Path:Libraries){auto* Library=LoadObject<UKKImportedAnimationLibrary>(nullptr,*Path);if(!TestNotNull(TEXT("Saved library"),Library))break;
  if(!TestNotNull(TEXT("Saved character"),Library->Character.Get()))break;FAssetCompilingManager::Get().FinishAllCompilation();Actor->Character=Library->Character;Actor->AnimationLibrary=Library;
  if(!TestTrue(TEXT("Library has original clips"),Library->Animations.Num()>0))break;ExpectedClips+=Library->Animations.Num();
  for(int32 I=0;I<Library->Animations.Num();++I){const auto& Entry=Library->Animations[I];if(!TestNotNull(TEXT("Saved sequence"),Entry.Sequence.Get()))break;Entry.Sequence->CacheDerivedDataForCurrentPlatform();Entry.Sequence->WaitOnExistingCompression();{const auto Data=Entry.Sequence->GetCompressedData();TestTrue(TEXT("Saved compressed animation data valid"),Data.Get().IsValid(Entry.Sequence));}
   Actor->Outfit=Entry.Outfit;Actor->AnimationIndex=I;Actor->RebuildCharacter();
   if(!TestTrue(TEXT("Start saved animation"),Actor->PlayImportedAnimation(I)))break;
   TestFalse(TEXT("Invalid index rejected"),Actor->PlayImportedAnimation(-1));
   TestFalse(TEXT("Nonfinite seek rejected"),Actor->SetAnimationPosition(std::numeric_limits<float>::infinity()));
   TestTrue(TEXT("Seek initial pose"),Actor->SetAnimationPosition(0.f));
   TArray<USkeletalMeshComponent*> Components;Actor->GetComponents(Components);TestTrue(TEXT("Saved skins exist"),Components.Num()>0);
   if(Library->Character->Schema==2){const auto* Outfit=Library->Character->Outfits.FindByPredicate([&](const auto& O){return O.Index==Entry.Outfit;});if(TestNotNull(TEXT("Complete animated outfit"),Outfit))TestEqual(TEXT("Every imported renderer animates"),Components.Num(),Outfit->Parts.Num());}
   for(auto* Component:Components){Component->TickAnimation(.05f,false);Component->RefreshBoneTransforms();TestTrue(TEXT("Unreal animation clock advances"),Component->GetSingleNodeInstance() && Component->GetSingleNodeInstance()->GetCurrentTime()>.04f);}
   TestTrue(TEXT("Reset after clock playback"),Actor->SetAnimationPosition(0.f));
   TArray<TArray<FFinalSkinVertex>> Baselines;Baselines.SetNum(Components.Num());
   for(int32 K=0;K<Components.Num();++K){Components[K]->GetCPUSkinnedVertices(Baselines[K],0);Vertices+=Baselines[K].Num();++Skins;}
   double ClipMotion=0.;
   for(int32 F=1;F<=4;++F){const double Time=Entry.Sequence->GetPlayLength()*F/5.;TestTrue(TEXT("Seek animated pose"),Actor->SetAnimationPosition(Time));
    for(int32 K=0;K<Components.Num();++K){const auto Local=Components[K]->GetBoneSpaceTransforms();const auto& Ref=Entry.Sequence->GetSkeleton()->GetReferenceSkeleton();
     TestEqual(TEXT("Complete pose"),Local.Num(),Ref.GetNum());
     for(FBoneIndexType B:Components[K]->RequiredBones){const int32 Slot=Ref.FindBoneIndex(Components[K]->GetSkeletalMeshAsset()->GetRefSkeleton().GetBoneName(B));if(!TestTrue(TEXT("Mesh bone maps to animation skeleton"),Slot!=INDEX_NONE))break;FTransform Expected=Ref.GetRefBonePose()[Slot];FAnimExtractContext Context(Time);Context.bEnforceCompressedDataSampling=true;Entry.Sequence->GetBoneTransform(Expected,FSkeletonPoseBoneIndex(Slot),Context,false);
      if(!Local[B].Equals(Expected,0.005)){AddError(TEXT("Unreal evaluated pose differs from saved tracks"));break;}}
     TArray<FFinalSkinVertex> Current;Components[K]->GetCPUSkinnedVertices(Current,0);TestEqual(TEXT("Stable vertex count"),Current.Num(),Baselines[K].Num());
     if(Current.Num()==Baselines[K].Num())for(int32 V=0;V<Current.Num();++V){if(Current[V].Position.ContainsNaN()){AddError(TEXT("Nonfinite animated vertex"));break;}ClipMotion=FMath::Max(ClipMotion,double((Current[V].Position-Baselines[K][V].Position).Size()));}
    }
   }
   TestTrue(TEXT("Original clip deforms saved skin"),ClipMotion>0.001);Motion=FMath::Max(Motion,ClipMotion);++Clips;
  }
 }
 Actor->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 TestEqual(TEXT("Every saved clip/outfit evaluated"),Clips,ExpectedClips);
 UE_LOG(LogTemp,Display,TEXT("Detached original animations clips=%d skin_instances=%d vertices=%d max_motion=%.6f parser=absent pack=absent compressed=valid clock=advances"),Clips,Skins,Vertices,Motion);return !HasAnyErrors();
}
#endif
