#include "KKSharedAnimation.h"
#include "KKImportedCharacter.h"
#include "KKImportedPoseComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Curves/CurveFloat.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSharedDetachedTest,"OpenKoikatsu.Import.SharedDetached",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSharedDetachedTest::RunTest(const FString& Parameters){
 const FString First=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_SHARED_CHARACTER"));
 const FString Second=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_SHARED_SECOND_CHARACTER"));
 const FString Clip=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_SHARED_ANIMATION"));
 if(!TestFalse(TEXT("Detached fixture contains no built-in pack"),IFileManager::Get().DirectoryExists(*(FPaths::ProjectContentDir()/TEXT("IndependentResources"))))||
    !TestFalse(TEXT("Detached fixture contains no extraction tools"),IFileManager::Get().DirectoryExists(*(FPaths::ProjectDir()/TEXT("Tools")))))return false;
 TStrongObjectPtr<UKKImportedCharacter> CA(LoadObject<UKKImportedCharacter>(nullptr,*First)),CB(LoadObject<UKKImportedCharacter>(nullptr,*Second));
 TStrongObjectPtr<UKKSharedAnimation> Animation(LoadObject<UKKSharedAnimation>(nullptr,*Clip));
 if(!TestNotNull(TEXT("First saved card"),CA.Get())||!TestNotNull(TEXT("Second saved card"),CB.Get())||!TestNotNull(TEXT("Shared curve asset"),Animation.Get()))return false;
 if(!TestTrue(TEXT("Two different character assets"),CA.Get()!=CB.Get()&&CA->SourceHash!=CB->SourceHash)||CA->Outfits.IsEmpty()||CB->Outfits.IsEmpty())return false;
 uint8 Before[20],After[20];FSHA1::HashBuffer(Animation->AuthoredCurves.GetData(),Animation->AuthoredCurves.Num(),Before);
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* A=World->SpawnActor<AKKImportedCharacterActor>();auto* B=World->SpawnActor<AKKImportedCharacterActor>();
 A->Character=CA.Get();B->Character=CB.Get();A->Outfit=CA->Outfits[0].Index;B->Outfit=CB->Outfits[0].Index;
 bool Good=TestTrue(TEXT("Different rig A plays shared asset without source files"),A->PlaySharedAnimation(Animation.Get()));
 Good=TestTrue(TEXT("Different rig B plays SAME shared asset without source files"),B->PlaySharedAnimation(Animation.Get()))&&Good;
 AddInfo(A->AnimationError+TEXT(" / ")+B->AnimationError);
 Good=TestTrue(TEXT("A independent mouth control"),A->SetMouthControl(0,.25f))&&Good;
 Good=TestTrue(TEXT("B independent mouth control"),B->SetMouthControl(0,.75f))&&Good;
 const int32 Expected=CA->Outfits[0].Parts.Num()+CB->Outfits[0].Parts.Num();int32 Skins=0;
 if(Good)for(int32 F=0;F<30;++F){A->Tick(1.f/60.f);B->Tick(1.f/30.f);}
 Good=TestTrue(TEXT("Separate clocks on shared resource"),FMath::IsNearlyEqual(A->SharedAnimationTime,.5f,1e-5f)&&FMath::IsNearlyEqual(B->SharedAnimationTime,1.f,1e-5f))&&Good;
 Good=TestTrue(TEXT("Forward loop crossing"),A->SetAnimationPosition(Animation->Duration+.2f))&&Good;
 Good=TestTrue(TEXT("Backward seek"),B->SetAnimationPosition(.1f))&&Good;
 auto* Curve=NewObject<UCurveFloat>(A);Curve->FloatCurve.AddKey(0.f,0.f);Curve->FloatCurve.AddKey(.2f,1.f);Curve->FloatCurve.AddKey(.4f,0.f);A->MouthOpenCurve=Curve;
 Good=TestTrue(TEXT("Curve-driven open during seek"),A->SetAnimationPosition(.2f))&&Good;
 int32 CurveParts=0;TArray<UKKImportedPoseComponent*> CurveComponents;A->GetComponents(CurveComponents);
 for(auto* C:CurveComponents){const auto* Part=CA->Outfits[0].Parts.FindByPredicate([&](const auto& P){return P.SkinnedModel==C->GetSkeletalMeshAsset();});if(!Part||Part->MouthPatterns.IsEmpty())continue;
  const auto& Pair=Part->MouthPatterns[0];if(!Pair.Open.IsNone()){++CurveParts;Good=TestTrue(TEXT("Curve reaches native open morph during shared animation"),FMath::IsNearlyEqual(C->GetMorphTarget(Pair.Open),(100.f-Part->MouthBaseline.FindRef(Pair.Open))/100.f,1e-6f))&&Good;}
 }
 Good=TestTrue(TEXT("Curve actually reaches rendered mouth channels"),CurveParts>0)&&Good;
 for(auto* Actor:{A,B}){TArray<UKKImportedPoseComponent*> Components;Actor->GetComponents(Components);Skins+=Components.Num();
  for(const auto* C:Components)for(const auto& T:C->GetComponentSpaceTransforms())if(T.ContainsNaN())Good=false;
 }
 Good=TestEqual(TEXT("Both complete rigs survive simultaneous playback"),Skins,Expected)&&Good;
 Good=TestEqual(TEXT("B manual mouth not replaced by A curve"),B->MouthOpen,.75f)&&Good;
 A->SharedAnimationPlaying=false;const float Time=A->SharedAnimationTime;A->Tick(.3f);Good=TestEqual(TEXT("Independent pause"),A->SharedAnimationTime,Time)&&Good;
 TStrongObjectPtr<UKKSharedAnimation> OneShot(NewObject<UKKSharedAnimation>());
 OneShot->Source=Animation->Source;OneShot->Clip=Animation->Clip;OneShot->Duration=Animation->Duration;
 OneShot->Loop=false;OneShot->AuthoredCurves=Animation->AuthoredCurves;
 Good=TestTrue(TEXT("Non-loop raw resource plays on same rig"),A->PlaySharedAnimation(OneShot.Get()))&&Good;
 auto* EndCurve=NewObject<UCurveFloat>(A);EndCurve->FloatCurve.AddKey(0.f,0.f);EndCurve->FloatCurve.AddKey(OneShot->Duration,.2f);
 EndCurve->FloatCurve.AddKey(OneShot->Duration+.2f,1.f);A->MouthOpenCurve=EndCurve;
 Good=TestTrue(TEXT("Non-loop seek beyond end"),A->SetAnimationPosition(OneShot->Duration+.2f))&&Good;
 Good=TestEqual(TEXT("Non-loop skeleton and mouth use same end phase"),OneShot->GetPhase(A->SharedAnimationTime),OneShot->Duration)&&Good;
 int32 EndParts=0;TArray<UKKImportedPoseComponent*> EndComponents;A->GetComponents(EndComponents);
 for(auto* C:EndComponents){const auto* Part=CA->Outfits[0].Parts.FindByPredicate([&](const auto& P){return P.SkinnedModel==C->GetSkeletalMeshAsset();});if(!Part||Part->MouthPatterns.IsEmpty())continue;
  const auto& Pair=Part->MouthPatterns[0];if(!Pair.Open.IsNone()){++EndParts;Good=TestTrue(TEXT("Non-loop mouth holds actual end morph after duration"),FMath::IsNearlyEqual(C->GetMorphTarget(Pair.Open),(20.f-Part->MouthBaseline.FindRef(Pair.Open))/100.f,1e-6f))&&Good;}
 }
 Good=TestTrue(TEXT("Non-loop geometry channels present"),EndParts>0)&&Good;
 TStrongObjectPtr<UKKSharedAnimation> Invalid(NewObject<UKKSharedAnimation>());Invalid->Duration=Animation->Duration+.5f;Invalid->AuthoredCurves=Animation->AuthoredCurves;
 const float BeforeReject=A->SharedAnimationTime;
 Good=TestFalse(TEXT("Mismatched metadata rejected before replacing actor"),A->PlaySharedAnimation(Invalid.Get()))&&Good;
 Good=TestTrue(TEXT("Rejected animation retains prior resource"),A->SharedAnimation==OneShot.Get())&&Good;
 Good=TestEqual(TEXT("Rejected animation retains clock"),A->SharedAnimationTime,BeforeReject)&&Good;
 Good=TestTrue(TEXT("Prior resource still seeks after rejection"),A->SetAnimationPosition(OneShot->Duration+.1f))&&Good;
 UE_LOG(LogTemp,Display,TEXT("Shared one-shot audit valid=%d end_mouth_parts=%d phase_clamped=1 duration_mismatch=rejected prior_player=preserved"),Good?1:0,EndParts);
 // A structurally valid curve can initialize while the saved renderer contract
 // fails at first evaluation. Reject it without destroying the previous player.
 auto& MutableParts=CA->Outfits[0].Parts;
 auto* Broken=MutableParts.FindByPredicate([](const auto& P){return P.RigidFollow.IsEmpty()&&!P.PaletteSlots.IsEmpty();});
 Good=TestNotNull(TEXT("Palette failure fixture"),Broken)&&Good;
 if(Broken){
  TArray<UKKImportedPoseComponent*> PreviousComponents;A->GetComponents(PreviousComponents);
  TArray<TArray<FTransform>> PreviousPoses;TArray<TMap<FName,float>> PreviousMouth;
  for(auto* C:PreviousComponents){PreviousPoses.Add(C->GetComponentSpaceTransforms());TMap<FName,float> Weights;
   const auto* Part=MutableParts.FindByPredicate([&](const auto& P){return P.SkinnedModel==C->GetSkeletalMeshAsset();});
   if(Part)for(const auto& Pair:Part->MouthPatterns){if(!Pair.Close.IsNone())Weights.Add(Pair.Close,C->GetMorphTarget(Pair.Close));if(!Pair.Open.IsNone())Weights.Add(Pair.Open,C->GetMorphTarget(Pair.Open));}PreviousMouth.Add(MoveTemp(Weights));}
  const float PreviousTime=A->SharedAnimationTime;const bool PreviousPlaying=A->SharedAnimationPlaying;
  const int32 OriginalSlot=Broken->PaletteSlots[0];Broken->PaletteSlots[0]=1000000;
  const bool Accepted=A->PlaySharedAnimation(Animation.Get());Broken->PaletteSlots[0]=OriginalSlot;
  Good=TestFalse(TEXT("First-pose failure rejects candidate"),Accepted)&&Good;
  Good=TestFalse(TEXT("First-pose failure reports reason"),A->AnimationError.IsEmpty())&&Good;
  Good=TestTrue(TEXT("Failed candidate preserves previous resource and curve"),A->SharedAnimation==OneShot.Get()&&A->MouthOpenCurve==EndCurve)&&Good;
  Good=TestEqual(TEXT("Failed candidate preserves clock"),A->SharedAnimationTime,PreviousTime)&&Good;
  Good=TestEqual(TEXT("Failed candidate preserves playing state"),A->SharedAnimationPlaying,PreviousPlaying)&&Good;
  TArray<UKKImportedPoseComponent*> Retained;A->GetComponents(Retained);
  Good=TestEqual(TEXT("Failed candidate leaves no staged components"),Retained.Num(),PreviousComponents.Num())&&Good;
  for(int32 I=0;I<PreviousComponents.Num();++I){auto* C=PreviousComponents[I];
   const bool Present=Retained.Contains(C)&&C->IsRegistered()&&C->ComponentHasTag(TEXT("ImportedCard"));
   Good=TestTrue(TEXT("Failed candidate retains exact renderer objects"),Present)&&Good;
   if(Present){for(const auto& Weight:PreviousMouth[I])Good=TestEqual(TEXT("Retained mouth channel unchanged"),C->GetMorphTarget(Weight.Key),Weight.Value)&&Good;const auto& Pose=C->GetComponentSpaceTransforms();Good=TestEqual(TEXT("Retained pose size"),Pose.Num(),PreviousPoses[I].Num())&&Good;
    for(int32 J=0;J<FMath::Min(Pose.Num(),PreviousPoses[I].Num());++J)Good=TestTrue(TEXT("Retained pose unchanged"),Pose[J].Equals(PreviousPoses[I][J],0.f))&&Good;}
  }
  Good=TestTrue(TEXT("Previous solver still seeks after first-pose rejection"),A->SetAnimationPosition(.1f))&&Good;
  const FString SwitchPath=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_SHARED_SWITCH_ANIMATION"));
  TStrongObjectPtr<UKKSharedAnimation> SwitchAnimation(SwitchPath.IsEmpty()?Animation.Get():LoadObject<UKKSharedAnimation>(nullptr,*SwitchPath));
  Good=TestNotNull(TEXT("Healthy switch animation"),SwitchAnimation.Get())&&Good;
  if(!SwitchPath.IsEmpty())Good=TestTrue(TEXT("Switch uses different authored curves"),SwitchAnimation.Get()&&SwitchAnimation->AuthoredCurves!=Animation->AuthoredCurves)&&Good;
  Good=TestTrue(TEXT("Healthy candidate commits after rejection"),A->PlaySharedAnimation(SwitchAnimation.Get()))&&Good;
  Good=TestEqual(TEXT("Committed candidate starts at zero"),A->SharedAnimationTime,0.f)&&Good;
  Good=TestTrue(TEXT("Committed candidate retains independent mouth control"),A->MouthControlEnabled&&A->MouthOpenCurve==EndCurve)&&Good;
  Good=TestTrue(TEXT("Committed candidate advances"),A->SetAnimationPosition(.2f))&&Good;
 }
 UE_LOG(LogTemp,Display,TEXT("Shared switch audit valid=%d first_pose_failure=rejected previous_components=preserved clock=preserved mouth=preserved healthy_commit=1"),Good?1:0);
 FSHA1::HashBuffer(Animation->AuthoredCurves.GetData(),Animation->AuthoredCurves.Num(),After);Good=TestTrue(TEXT("Shared authored asset stays immutable"),FMemory::Memcmp(Before,After,20)==0)&&Good;
 UE_LOG(LogTemp,Display,TEXT("Shared detached audit valid=%d different_cards=2 same_curve_asset=1 skins=%d no_pack=1 no_tools=1 independent_mouth=1 loop=1 seek=1 pause=1"),Good?1:0,Skins);
 A->Destroy();B->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return Good&&!HasAnyErrors();
}
#endif
