#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatorCartesianTest,"OpenKoikatsu.AnimationCatalog.Animator2DLayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatorCartesianTest::RunTest(const FString& Parameters)
{
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore {FString Value;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),Value.IsEmpty()?nullptr:*Value);}} Restore{OldExternal};
    int32 Cards=0,Frames=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        if(!TestTrue(TEXT("nonempty evaluated skeleton"),Session.SkelBoneCount()>0)) return false;
        for(const TCHAR* State:{TEXT("17:Te_siri_D_momu_L.Te_siri_D_momu_L"),TEXT("05:Te_mune_D_momu_L.Te_mune_D_momu_L")}) {
            const bool bNested=FString(State).StartsWith(TEXT("05:"));
            const FString X=bNested?TEXT("muneR_X"):TEXT("siriL_X"),Y=bNested?TEXT("muneR_Y"):TEXT("siriL_Y");
            if(!TestTrue(TEXT("exact authored override layer"),Session.SetAnimClip(FString(TEXT("states:studio/anime/00.unity3d|khs3_f_base3 / "))+State))) return false;
            TSet<FString> Names;
            for(int32 Index=0;Index<Session.AnimationFloatCount();++Index) {
                FString Name;float Value,Min,Max;
                if(!Session.AnimationFloatAt(Index,Name,Value,Min,Max)) return false;
                Names.Add(Name);
            }
            if(!TestTrue(TEXT("authored blend parameters including base height"),Names.Contains(X)&&Names.Contains(Y)&&Names.Contains(TEXT("height"))&&(!bNested||Names.Contains(TEXT("Breast"))))) return false;
            if(!Session.SetAnimationFloat(TEXT("height"),.37f)||(bNested&&!Session.SetAnimationFloat(TEXT("Breast"),.61f))) return false;
            for(int32 Frame=0;Frame<30;++Frame) {
                const float Point=Frame/29.f;
                if(!Session.SetAnimationFloat(X,FMath::Lerp(-.25f,1.25f,Point))||!Session.SetAnimationFloat(Y,1.f-Point)||
                    !Session.ApplyAnimFrame(.43f*Session.AnimDuration(),1.f/60.f)) return false;
                for(int32 Bone=0;Bone<Session.SkelBoneCount();++Bone) {
                    OkBoneView View{};if(!Session.SkelBoneAt(Bone,View)) return false;
                    for(float V:View.pose_pos) if(!FMath::IsFinite(V)) return false;
                    for(float V:View.pose_rot) if(!FMath::IsFinite(V)) return false;
                    for(float V:View.pose_scl) if(!FMath::IsFinite(V)) return false;
                }
                ++Frames;
            }
        }
        AddInfo(FString::Printf(TEXT("Animator2D card=%s states=2 frames=60 finite=1"),Card));++Cards;
    }
    return TestEqual(TEXT("five cards"),Cards,5)&&TestEqual(TEXT("300 layered source frames"),Frames,300);
}
#endif
