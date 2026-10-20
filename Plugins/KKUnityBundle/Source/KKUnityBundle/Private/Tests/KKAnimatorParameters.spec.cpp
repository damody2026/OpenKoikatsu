#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatorParametersTest,"OpenKoikatsu.AnimationCatalog.AnimatorParameterPropagation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatorParametersTest::RunTest(const FString& Parameters)
{
    const FString OldSkip=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NODB"));
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore {FString Skip,External;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),Skip.IsEmpty()?nullptr:*Skip);FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),External.IsEmpty()?nullptr:*External);}} Restore{OldSkip,OldExternal};
    // Isolate parameter response from particle history; the full source physics
    // path has a separate five-card native-validated Session comparison.
    struct FCase {const TCHAR* Clip;const TCHAR* Parameter;float A,B;};
    const FCase Cases[]={
        {TEXT("states:action/animator/00.unity3d|base / Base Layer.Main.Locomotion_Anger"),TEXT("Speed"),.1f,.9f},
        {TEXT("states:studio/anime/00.unity3d|khs3_f_base3 / 17:Te_siri_D_momu_L.Te_siri_D_momu_L"),TEXT("siriL_X"),.1f,.9f},
        {TEXT("states:studio/anime/00.unity3d|khs3_f_base3 / 05:Te_mune_D_momu_L.Te_mune_D_momu_L"),TEXT("muneR_X"),.1f,.9f}};
    int32 Cards=0,Responses=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;
        FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),OldSkip.IsEmpty()?nullptr:*OldSkip);
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        // SetAnimClip must enter source mode itself after Open. This exercises
        // the common entry, without a test-only workaround for T-pose mode.
        FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),TEXT("1"));
        if(!TestTrue(TEXT("real exported skeleton"),Session.SkelBoneCount()>0)) return false;
        for(const auto& C:Cases) {
            if(!Session.SetAnimSceneMode(4)||!Session.SetAnimClip(C.Clip)||!Session.SetAnimationFloat(C.Parameter,C.A)||
                !Session.ApplyAnimFrame(.43f*Session.AnimDuration(),0.f)) return false;
            TArray<FTransform> Before;
            auto Snapshot=[&Session](TArray<FTransform>& Out) {
                Out.Reset();
                for(int32 Bone=0;Bone<Session.SkelBoneCount();++Bone) {
                    OkBoneView V{};if(!Session.SkelBoneAt(Bone,V)) return false;
                    Out.Add(FTransform(FQuat(V.pose_rot[0],V.pose_rot[1],V.pose_rot[2],V.pose_rot[3]),
                        FVector(V.pose_pos[0],V.pose_pos[1],V.pose_pos[2]),FVector(V.pose_scl[0],V.pose_scl[1],V.pose_scl[2])));
                }
                return true;
            };
            if(!Snapshot(Before)||!Session.SetAnimationFloat(C.Parameter,C.B)||!Session.ApplyAnimFrame(.43f*Session.AnimDuration(),0.f)) return false;
            TArray<FTransform> After;if(!Snapshot(After)) return false;
            int32 Changed=0;
            for(int32 Bone=0;Bone<Before.Num();++Bone) Changed+=!Before[Bone].Equals(After[Bone],1.e-5);
            if(!TestTrue(TEXT("float changes the exported skeleton, not just the getter"),Changed>0)) return false;
            if(!Session.SetAnimationFloat(C.Parameter,C.A)||!Session.ApplyAnimFrame(.43f*Session.AnimDuration(),0.f)) return false;
            TArray<FTransform> Repeat;if(!Snapshot(Repeat)) return false;
            for(int32 Bone=0;Bone<Before.Num();++Bone)
                if(!TestTrue(TEXT("returning parameter restores the entire pose"),Before[Bone].Equals(Repeat[Bone],1.e-6))) return false;
            AddInfo(FString::Printf(TEXT("AnimatorParameters card=%s parameter=%s changed_bones=%d restored=1"),Card,C.Parameter,Changed));++Responses;
        }
        ++Cards;
    }
    return TestEqual(TEXT("five cards"),Cards,5)&&TestEqual(TEXT("1D, 2D, nested 2D positive controls"),Responses,15);
}
#endif
