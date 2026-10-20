#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMisc.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatorCrossFadeTest,"OpenKoikatsu.AnimationCatalog.AnimatorCrossFade",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatorCrossFadeTest::RunTest(const FString&)
{
    const FString Old=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore{FString Value;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),Value.IsEmpty()?nullptr:*Value);}}Restore{Old};
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),nullptr);
    int32 Frames=0,Cards=0,Changed=0,Rejected=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)){AddError(Error);return false;}Session.SkelBoneCount();Session.SetAnimSceneMode(0);
        FString Stem=Card;for(TCHAR& C:Stem)if(!FChar::IsAlnum(C)&&C!=TEXT('_'))C=TEXT('_');
        FString Json;TArray<TSharedPtr<FJsonValue>> Rows;
        if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectSavedDir()/TEXT("Tests/AnimatorCrossFade301")/(Stem+TEXT("AnimatorCrossFade301.json.crossfade.json"))))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Rows)||Rows.Num()!=15)return false;
        for(const auto& Row:Rows) {
            const auto F=Row->AsObject();const FString Prefix=TEXT("states:")+F->GetStringField(TEXT("bundle"))+TEXT("|")+F->GetStringField(TEXT("controller"))+TEXT(" / ");
            const FString From=Prefix+F->GetStringField(TEXT("fromState")),To=Prefix+F->GetStringField(TEXT("toState"));
            const float Start=F->GetNumberField(TEXT("start")),Elapsed=F->GetNumberField(TEXT("elapsed"));
            if(!Session.SetAnimClip(From)||!Session.SetAnimationFloat(TEXT("MotionSpeed"),F->GetNumberField(TEXT("speed")))||!Session.SetAnimationFloat(TEXT("Speed"),.37f)||!Session.ApplyAnimFrame(Start,0.f))return false;
            for(float Duration:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){if(!TestFalse(TEXT("invalid CrossFade is atomic"),Session.CrossFadeAnimation(To,Duration,0.f))||Session.AnimationElapsed()!=Start)return false;++Rejected;}
            if(!Session.CrossFadeAnimation(To,F->GetNumberField(TEXT("blend")),F->GetNumberField(TEXT("offset")))||Session.AnimationElapsed()!=Start||!Session.ApplyAnimFrame(Elapsed,1.f/60.f))return false;
            if(!TestEqual(TEXT("source transition complete state"),Session.IsAnimationTransitioning(),F->GetBoolField(TEXT("transitioning"))))return false;
            for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B))return false;for(float V:B.pose_pos)if(!FMath::IsFinite(V))return false;for(float V:B.pose_rot)if(!FMath::IsFinite(V))return false;}
            if(Session.IsAnimationTransitioning()){if(Session.CrossFadeAnimation(From,.4f,0.f)||Session.SetAnimationFloat(TEXT("MotionSpeed"),1.2f))return false;Rejected+=2;}
            ++Frames;
        }
        const FString From=TEXT("states:action/animator/00.unity3d|base / Base Layer.Main.Idle"),To=TEXT("states:action/animator/00.unity3d|base / Base Layer.Main.Locomotion_Anger");
        if(!Session.SetAnimClip(From)||!Session.SetAnimationFloat(TEXT("MotionSpeed"),1.f)||!Session.SetAnimationFloat(TEXT("Speed"),.37f)||!Session.ApplyAnimFrame(.61f,0.f))return false;
        TArray<FQuat> Before;for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};Session.SkelBoneAt(I,B);Before.Emplace(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]);}
        if(!Session.CrossFadeAnimation(To,.4f,.37f)||!Session.ApplyAnimFrame(1.19f,.58f))return false;
        int32 CardChanged=0;for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};Session.SkelBoneAt(I,B);CardChanged+=!Before[I].Equals(FQuat(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]),1.e-4);}
        if(!TestTrue(TEXT("CrossFade changes published palette"),CardChanged>0))return false;
        Changed+=CardChanged;++Cards;AddInfo(FString::Printf(TEXT("CrossFade card=%s frames=15 changed_bones=%d"),Card,CardChanged));
    }
    AddInfo(FString::Printf(TEXT("CrossFade accepted cards=%d frames=%d changed_bones=%d invalid_rejected=%d"),Cards,Frames,Changed,Rejected));return Cards==5&&Frames==75&&Changed>0;
}
#endif
