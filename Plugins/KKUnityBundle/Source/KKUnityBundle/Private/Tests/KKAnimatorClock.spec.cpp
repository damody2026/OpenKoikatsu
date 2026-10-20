#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatorClockTest,"OpenKoikatsu.AnimationCatalog.AnimatorStateClock",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatorClockTest::RunTest(const FString& Parameters)
{
    const FString OldSkip=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NODB"));
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore {FString Skip,External;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),Skip.IsEmpty()?nullptr:*Skip);FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),External.IsEmpty()?nullptr:*External);}} Restore{OldSkip,OldExternal};
    int32 Cards=0,Responses=0,Frames=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),nullptr);
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),TEXT("1"));
        if(!TestTrue(TEXT("actual exported skeleton"),Session.SkelBoneCount()>0)) return false;
        auto Snapshot=[&Session]() {
            TArray<FTransform> Result;
            for(int32 Bone=0;Bone<Session.SkelBoneCount();++Bone) {
                OkBoneView V{};if(!Session.SkelBoneAt(Bone,V)) return TArray<FTransform>();
                Result.Add(FTransform(FQuat(V.pose_rot[0],V.pose_rot[1],V.pose_rot[2],V.pose_rot[3]),
                    FVector(V.pose_pos[0],V.pose_pos[1],V.pose_pos[2]),FVector(V.pose_scl[0],V.pose_scl[1],V.pose_scl[2])));
            }
            return Result;
        };
        for(const TCHAR* State:{TEXT("Base Layer.Main.Idle"),TEXT("Base Layer.Main.Locomotion_Anger"),TEXT("05:Te_mune_D_momu_L.Te_mune_D_momu_L")}) {
            const bool bLayer=FString(State).StartsWith(TEXT("05:"));
            const FString ClockName=bLayer?TEXT("speedHand"):TEXT("MotionSpeed");
            const float DefaultMultiplier=bLayer?.5f:1.f;
            const FString Prefix=bLayer?TEXT("states:studio/anime/00.unity3d|khs3_f_base3 / "):TEXT("states:action/animator/00.unity3d|base / ");
            if(!Session.SetAnimClip(Prefix+State)) return false;
            if(FString(State).Contains(TEXT("Locomotion"))&&!Session.SetAnimationFloat(TEXT("Speed"),.37f)) return false;
            if(bLayer&&(!Session.SetAnimationFloat(TEXT("height"),.37f)||!Session.SetAnimationFloat(TEXT("Breast"),.61f)||
                !Session.SetAnimationFloat(TEXT("muneR_X"),.31f)||!Session.SetAnimationFloat(TEXT("muneR_Y"),.79f))) return false;
            if(!Session.SetAnimationFloat(ClockName,DefaultMultiplier)||!Session.ApplyAnimFrame(.43f,0.f)) return false;
            const float Duration=Session.AnimDuration();const auto Baseline=Snapshot();
            int32 Changed=0;
            for(float Value:{.4f,1.f,1.6f}) {
                if(!Session.SetAnimationFloat(ClockName,Value*DefaultMultiplier)) return false;
                if(!TestTrue(TEXT("effective state duration includes live multiplier"),FMath::IsNearlyEqual(Session.AnimDuration(),Duration/Value,1.e-4f))) return false;
                for(float Time:{0.f,.17f,.43f,.91f}) {
                    if(!Session.ApplyAnimFrame(Time,0.f)) return false;
                    const auto Pose=Snapshot();if(!TestEqual(TEXT("complete pose"),Pose.Num(),Baseline.Num())) return false;
                    for(int32 Bone=0;Bone<Pose.Num();++Bone) {
                        if(!TestFalse(TEXT("finite pose"),Pose[Bone].ContainsNaN())) return false;
                        if(Value==1.6f&&Time==.43f) Changed+=!Baseline[Bone].Equals(Pose[Bone],1.e-5);
                    }
                    ++Frames;
                }
            }
            if(!TestTrue(TEXT("same elapsed time changes actual pose"),Changed>0)) return false;
            if(!Session.SetAnimationFloat(ClockName,DefaultMultiplier)||!Session.ApplyAnimFrame(.43f,0.f)) return false;
            const auto Restored=Snapshot();
            for(int32 Bone=0;Bone<Baseline.Num();++Bone) if(!TestTrue(TEXT("restore source pose"),Baseline[Bone].Equals(Restored[Bone],1.e-6))) return false;
            for(float Bad:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::denorm_min()}) {
                if(!TestFalse(TEXT("invalid effective clock rejected"),Session.SetAnimationFloat(ClockName,Bad))) return false;
                if(!TestTrue(TEXT("duration unchanged after rejection"),FMath::IsNearlyEqual(Session.AnimDuration(),Duration,1.e-6f))) return false;
            }
            if(!Session.ApplyAnimFrame(.43f,0.f)) return false;
            const auto Atomic=Snapshot();
            for(int32 Bone=0;Bone<Baseline.Num();++Bone) if(!TestTrue(TEXT("invalid clock leaves pose unchanged"),Baseline[Bone].Equals(Atomic[Bone],1.e-6))) return false;
            AddInfo(FString::Printf(TEXT("AnimatorClock card=%s state=%s frames=12 changed_bones=%d restored=1 invalid_atomic=5"),Card,State,Changed));++Responses;
        }
        ++Cards;
    }
    return TestEqual(TEXT("five cards"),Cards,5)&&TestEqual(TEXT("single clip, blend and layer states"),Responses,15)&&TestEqual(TEXT("source frames"),Frames,180);
}
#endif
