#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMisc.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKMotionTimeScaleTest,"OpenKoikatsu.AnimationCatalog.AnimatorMotionTimeScale",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKMotionTimeScaleTest::RunTest(const FString&)
{
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore{FString Old;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),Old.IsEmpty()?nullptr:*Old);}} Restore{OldExternal};
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),nullptr);
    const FString Payload=TEXT("[\"h/anim/female/05_01_00.unity3d\",\"khr_f_02_00\",\"\",\"\"]");
    FString Hex;FTCHARToUTF8 Utf8(*Payload);for(int32 I=0;I<Utf8.Length();++I) Hex+=FString::Printf(TEXT("%02X"),uint8(Utf8.Get()[I]));
    const FString SyncedSource=TEXT("states:controller@")+Hex;
    int32 Frames=0,Cards=0,ScaledClocks=0,SyncedClocks=0,Changed=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        Session.SkelBoneCount();Session.SetAnimSceneMode(0);
        FString Stem=Card;for(TCHAR& C:Stem) if(!FChar::IsAlnum(C)&&C!=TEXT('_')) C=TEXT('_');
        FString Json;TArray<TSharedPtr<FJsonValue>> Data;
        const FString Fixture=FPaths::ProjectSavedDir()/TEXT("Tests/MotionTimeScale296")/(Stem+TEXT("MotionTimeScale296.json.motion-time-scale.json"));
        if(!FFileHelper::LoadFileToString(Json,*Fixture)) return false;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||!TestEqual(TEXT("complete source fixture"),Data.Num(),60)) return false;
        const auto Labels=Session.ClipNamesForSource(SyncedSource);
        for(const auto& Value:Data) {
            const auto F=Value->AsObject();if(!F.IsValid()) return false;
            const int32 Layer=int32(F->GetNumberField(TEXT("layer")));
            const float Time=F->GetNumberField(TEXT("elapsed")),NativeDuration=F->GetNumberField(TEXT("length"));
            FString Key=TEXT("states:action/animator/00.unity3d|player / Base Layer.Main.Locomotion");
            if(Layer>0) {
                const FString Label=FString::Printf(TEXT("khr_f_02_00 / [Controller layer %d] Base Layer.OLoop"),Layer);
                if(!TestTrue(TEXT("exact source synchronized state"),Labels.Contains(Label))) return false;
                Key=SyncedSource+TEXT("|")+Label;
            }
            if(!TestTrue(TEXT("scaled source state selectable"),Session.SetAnimClip(Key))) return false;
            float Speed=0.f;
            for(const auto& P:F->GetArrayField(TEXT("parameters"))) {
                const auto Param=P->AsObject();const FString Name=Param->GetStringField(TEXT("name"));const float Number=Param->GetNumberField(TEXT("value"));
                if(!Session.SetAnimationFloat(Name,Number)) return false;if(Name==TEXT("Speed")) Speed=Number;
            }
            if(!TestTrue(TEXT("duration equals native Animator"),FMath::IsNearlyEqual(Session.AnimDuration(),NativeDuration,1.e-4f))||
                !TestTrue(TEXT("native normalized time"),FMath::IsNearlyEqual(Time/Session.AnimDuration(),float(F->GetNumberField(TEXT("normalizedTime"))),2.e-5f))) return false;
            if(Layer>0) {if(!TestTrue(TEXT("synchronized base clock retained"),FMath::IsNearlyEqual(Session.AnimDuration(),float(F->GetNumberField(TEXT("baseLength"))),1.e-4f))) return false;++SyncedClocks;}
            else if(Speed>0.f) ++ScaledClocks;
            if(!Session.ApplyAnimFrame(Time,1.f/60.f)) return false;
            for(int32 I=0;I<Session.SkelBoneCount();++I) {
                OkBoneView B{};if(!Session.SkelBoneAt(I,B)) return false;
                for(float V:B.pose_pos) if(!FMath::IsFinite(V)) return false;for(float V:B.pose_rot) if(!FMath::IsFinite(V)) return false;for(float V:B.pose_scl) if(!FMath::IsFinite(V)) return false;
            }
            ++Frames;
        }
        // Source arm motion must reach the published palette with normal
        // source physics still enabled.
        const FString K=SyncedSource+TEXT("|khr_f_02_00 / [Controller layer 73] Base Layer.OLoop");
        if(!Session.SetAnimClip(K)||!Session.SetAnimationFloat(TEXT("Breast1"),.37f)||!Session.ApplyAnimFrame(.17f,0.f)) return false;
        TMap<FString,FQuat> Before;
        for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B)) return false;Before.Add(UTF8_TO_TCHAR(B.name),FQuat(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]));}
        if(!Session.ApplyAnimFrame(.61f,0.f)) return false;int32 CardChanged=0;
        for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B)) return false;CardChanged+=!Before[UTF8_TO_TCHAR(B.name)].Equals(FQuat(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]),1.e-4);}
        if(!TestTrue(TEXT("source pose advances in actual palette"),CardChanged>0)) return false;
        Changed+=CardChanged;++Cards;AddInfo(FString::Printf(TEXT("MotionTimeScale card=%s frames=60 native_clock=1 synced_clock=1 changed_bones=%d physics_enabled=1"),Card,CardChanged));
    }
    AddInfo(FString::Printf(TEXT("MotionTimeScale accepted cards=%d frames=%d scaled_clock_controls=%d synced_controls=%d changed_bones=%d"),Cards,Frames,ScaledClocks,SyncedClocks,Changed));
    return Cards==5&&Frames==300&&ScaledClocks==80&&SyncedClocks==180&&Changed>0;
}
#endif
