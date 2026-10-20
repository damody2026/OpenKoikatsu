#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMisc.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatorAdditiveTest,"OpenKoikatsu.AnimationCatalog.AnimatorAdditiveLayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatorAdditiveTest::RunTest(const FString&)
{
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore{FString Old;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),Old.IsEmpty()?nullptr:*Old);}} Restore{OldExternal};
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),nullptr);
    const FString Payload=TEXT("[\"h/anim/female/05_01_00.unity3d\",\"khr_f_02_00\",\"\",\"\"]");
    FString Hex;FTCHARToUTF8 Utf8(*Payload);for(int32 I=0;I<Utf8.Length();++I) Hex+=FString::Printf(TEXT("%02X"),uint8(Utf8.Get()[I]));
    const FString Source=TEXT("states:controller@")+Hex;
    int32 Frames=0,Cards=0,Changed=0,Rejected=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        Session.SkelBoneCount();Session.SetAnimSceneMode(0);
        FString Stem=Card;for(TCHAR& C:Stem) if(!FChar::IsAlnum(C)&&C!=TEXT('_')) C=TEXT('_');
        const FString Fixture=FPaths::ProjectSavedDir()/TEXT("Tests/AnimatorAdditive297")/(Stem+TEXT("AnimatorAdditive297.json.additive.json"));
        FString Json;TArray<TSharedPtr<FJsonValue>> Data;
        if(!FFileHelper::LoadFileToString(Json,*Fixture)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||!TestEqual(TEXT("complete native controls"),Data.Num(),36)) return false;
        const auto Labels=Session.ClipNamesForSource(Source);
        for(const auto& Value:Data) {
            const auto F=Value->AsObject();if(!F.IsValid()) return false;
            const int32 Layer=int32(F->GetNumberField(TEXT("layer")));
            const FString Label=FString::Printf(TEXT("khr_f_02_00 / [Controller layer %d] %s + %s"),Layer,*F->GetStringField(TEXT("baseState")),*F->GetStringField(TEXT("state")));
            if(!TestTrue(TEXT("exact authored additive state"),Labels.Contains(Label))||!Session.SetAnimClip(Source+TEXT("|")+Label)) return false;
            for(const auto& P:F->GetArrayField(TEXT("parameters"))) {
                const auto Param=P->AsObject();if(!Session.SetAnimationFloat(Param->GetStringField(TEXT("name")),Param->GetNumberField(TEXT("value")))) return false;
            }
            if(!Session.SetAnimationFloat(TEXT("OK.LayerWeight"),F->GetNumberField(TEXT("weight")))) return false;
            for(float Invalid:{-.01f,1.01f}) {if(!TestFalse(TEXT("reject invalid layer weight"),Session.SetAnimationFloat(TEXT("OK.LayerWeight"),Invalid))) return false;++Rejected;}
            const float Time=F->GetNumberField(TEXT("elapsed")),Duration=F->GetNumberField(TEXT("length"));
            if(!TestTrue(TEXT("source layer duration"),FMath::IsNearlyEqual(Session.AnimDuration(),Duration,1.e-4f))||
                !TestTrue(TEXT("source layer normalized clock"),FMath::IsNearlyEqual(Time/Session.AnimDuration(),float(F->GetNumberField(TEXT("normalizedTime"))),2.e-5f))||!Session.ApplyAnimFrame(Time,1.f/60.f)) return false;
            for(int32 I=0;I<Session.SkelBoneCount();++I) {
                OkBoneView B{};if(!Session.SkelBoneAt(I,B)) return false;
                for(float V:B.pose_pos) if(!FMath::IsFinite(V)) return false;for(float V:B.pose_rot) if(!FMath::IsFinite(V)) return false;for(float V:B.pose_scl) if(!FMath::IsFinite(V)) return false;
            }
            ++Frames;
        }
        const FString Key=Source+TEXT("|khr_f_02_00 / [Controller layer 8] Base Layer.OLoop + 08:Te_kokan_C_kosuru.Te_kokan_C_kosuru");
        if(!Session.SetAnimClip(Key)||!Session.SetAnimationFloat(TEXT("OK.LayerWeight"),0.f)||!Session.ApplyAnimFrame(.61f,0.f)) return false;
        TMap<FString,FQuat> Before;
        for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B)) return false;Before.Add(UTF8_TO_TCHAR(B.name),FQuat(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]));}
        if(!Session.SetAnimationFloat(TEXT("OK.LayerWeight"),1.f)||!Session.ApplyAnimFrame(.61f,0.f)) return false;
        int32 CardChanged=0;
        for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B)) return false;CardChanged+=!Before[UTF8_TO_TCHAR(B.name)].Equals(FQuat(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]),1.e-4);}
        if(!TestTrue(TEXT("layer weight changes published bone palette"),CardChanged>0)) return false;
        Changed+=CardChanged;++Cards;AddInfo(FString::Printf(TEXT("Additive card=%s frames=36 changed_bones=%d source_scene_auto=1 physics_enabled=1"),Card,CardChanged));
    }
    AddInfo(FString::Printf(TEXT("Additive accepted cards=%d frames=%d changed_bones=%d invalid_weights_rejected=%d"),Cards,Frames,Changed,Rejected));
    return Cards==5&&Frames==180&&Changed>0&&Rejected==360;
}
#endif
