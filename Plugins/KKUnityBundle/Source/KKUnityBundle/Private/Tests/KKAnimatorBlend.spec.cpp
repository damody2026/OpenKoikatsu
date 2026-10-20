#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatorBlendTest,"OpenKoikatsu.AnimationCatalog.Animator1DBlends",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatorBlendTest::RunTest(const FString& Parameters)
{
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore {FString Value;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),Value.IsEmpty()?nullptr:*Value);}} Restore{OldExternal};
    int32 Cards=0,Frames=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        if(!TestTrue(TEXT("nonempty evaluated skeleton"),Session.SkelBoneCount()>0)) return false;
        for(const TCHAR* State:{TEXT("Base Layer.Main.Locomotion_Anger"),TEXT("Base Layer.Twoshot.Twoshot_00"),TEXT("Base Layer.Lesbian.Lesbian3")}) {
            if(!TestTrue(TEXT("exact source state"),Session.SetAnimClip(FString(TEXT("states:action/animator/00.unity3d|base / "))+State))) return false;
            if(!TestTrue(TEXT("authored blend parameters"),Session.AnimationFloatCount()>0)) return false;
            FString Name;float Value,Min,Max;
            const FString BlendName=FString(State).Contains(TEXT("Locomotion"))?TEXT("Speed"):
                (FString(State).Contains(TEXT("Lesbian"))?TEXT("height1"):TEXT("height"));
            bool Found=false;
            for(int32 Index=0;Index<Session.AnimationFloatCount();++Index)
                if(Session.AnimationFloatAt(Index,Name,Value,Min,Max)&&Name==BlendName) {Found=true;break;}
            if(!TestTrue(TEXT("parameter matched by source identity"),Found)) return false;
            if(!TestTrue(TEXT("invalid parameter is atomic"),!Session.SetAnimationFloat(TEXT("missing_parameter"),.4f)&&!Session.SetAnimationFloat(Name,std::numeric_limits<float>::quiet_NaN()))) return false;
            for(int32 Frame=0;Frame<30;++Frame) {
                if(!Session.SetAnimationFloat(Name,FMath::Lerp(Min,Max,Frame/29.f))||!Session.ApplyAnimFrame(.43f*Session.AnimDuration(),1.f/60.f)) return false;
                for(int32 Bone=0;Bone<Session.SkelBoneCount();++Bone) {
                    OkBoneView View{};
                    if(!Session.SkelBoneAt(Bone,View)) return false;
                    for(float V:View.pose_pos) if(!FMath::IsFinite(V)) return false;
                    for(float V:View.pose_rot) if(!FMath::IsFinite(V)) return false;
                    for(float V:View.pose_scl) if(!FMath::IsFinite(V)) return false;
                }
                ++Frames;
            }
        }
        AddInfo(FString::Printf(TEXT("Animator1D card=%s states=3 frames=90 finite=1"),Card));++Cards;
    }
    return TestEqual(TEXT("five cards"),Cards,5)&&TestEqual(TEXT("450 dynamic source frames"),Frames,450);
}
#endif
