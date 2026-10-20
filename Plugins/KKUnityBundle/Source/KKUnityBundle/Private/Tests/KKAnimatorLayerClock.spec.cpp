#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMisc.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatorLayerClockTest,"OpenKoikatsu.AnimationCatalog.AnimatorIndependentLayerClock",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatorLayerClockTest::RunTest(const FString&)
{
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore{FString Old;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),Old.IsEmpty()?nullptr:*Old);}} Restore{OldExternal};
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),nullptr);
    const FString Payload=TEXT("[\"h/anim/female/05_01_00.unity3d\",\"khr_f_02_00\",\"\",\"\"]");
    FString Hex;FTCHARToUTF8 Utf8(*Payload);for(int32 I=0;I<Utf8.Length();++I) Hex+=FString::Printf(TEXT("%02X"),uint8(Utf8.Get()[I]));
    const FString Source=TEXT("controller@")+Hex;
    int32 Frames=0,Cards=0,Changed=0,Rejected=0,RejectedClock=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        Session.SkelBoneCount();Session.SetAnimSceneMode(0);
        FString Stem=Card;for(TCHAR& C:Stem) if(!FChar::IsAlnum(C)&&C!=TEXT('_')) C=TEXT('_');
        const FString Fixture=FPaths::ProjectSavedDir()/TEXT("Tests/AnimatorLayerClock299")/(Stem+TEXT("AnimatorLayerClock299.json.layer-clock.json"));
        FString Json;TArray<TSharedPtr<FJsonValue>> Data;
        if(!FFileHelper::LoadFileToString(Json,*Fixture)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||!TestEqual(TEXT("complete native controls"),Data.Num(),20)) return false;
        auto KeyFor=[&](const TSharedPtr<FJsonObject>& F) {
            const FString Base=TEXT("khr_f_02_00 / [Controller layer 0] ")+F->GetStringField(TEXT("baseState"));
            TArray<TSharedPtr<FJsonValue>> Selected;
            for(const auto& L:F->GetArrayField(TEXT("layers"))) {
                auto Layer=L->AsObject();int32 I=int32(Layer->GetNumberField(TEXT("layer")));
                Selected.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("khr_f_02_00 / [Controller layer %d] %s%s"),I,*F->GetStringField(TEXT("baseState")),I<73?*(FString(TEXT(" + "))+Layer->GetStringField(TEXT("state"))):TEXT(""))));
            }
            TArray<TSharedPtr<FJsonValue>> Descriptor={MakeShared<FJsonValueString>(Source),MakeShared<FJsonValueString>(Base),MakeShared<FJsonValueArray>(Selected)};
            FString Serialized;FJsonSerializer::Serialize(Descriptor,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized));
            FString Encoded;FTCHARToUTF8 Bytes(*Serialized);for(int32 I=0;I<Bytes.Length();++I) Encoded+=FString::Printf(TEXT("%02X"),uint8(Bytes.Get()[I]));
            return FString(TEXT("states:layers@"))+Encoded+TEXT("|")+Base;
        };
        for(const auto& Value:Data) {
            const auto F=Value->AsObject();if(!F.IsValid()) return false;
            const FString Key=KeyFor(F);
            if(!Session.SetAnimClip(Key)) return false;
            for(const auto& P:F->GetArrayField(TEXT("parameters"))) {
                const auto Param=P->AsObject();if(!Session.SetAnimationFloat(Param->GetStringField(TEXT("name")),Param->GetNumberField(TEXT("value")))) return false;
            }
            for(const auto& L:F->GetArrayField(TEXT("layers"))) {
                auto Layer=L->AsObject();const FString Weight=FString::Printf(TEXT("OK.LayerWeight.%d"),int32(Layer->GetNumberField(TEXT("layer"))));
                if(!Session.SetAnimationFloat(Weight,Layer->GetNumberField(TEXT("weight")))) return false;
                const int32 Index=int32(Layer->GetNumberField(TEXT("layer")));
                for(const auto& Pair:{TPair<FString,FString>(TEXT("OK.LayerStartTime."),TEXT("startTime")),TPair<FString,FString>(TEXT("OK.LayerStartPhase."),TEXT("startPhase"))}) {
                    const FString Control=Pair.Key+FString::FromInt(Index);
                    if(Index>=73) {if(!TestFalse(TEXT("synced clock remains inherited"),Session.SetAnimationFloat(Control,.37f))) return false;++RejectedClock;}
                    else {
                        if(!Session.SetAnimationFloat(Control,Layer->GetNumberField(Pair.Value))) return false;
                        for(float Invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
                            if(!TestFalse(TEXT("reject nonfinite layer clock"),Session.SetAnimationFloat(Control,Invalid))) return false;++RejectedClock;
                        }
                    }
                }
                for(float Invalid:{-.01f,1.01f}) {if(!TestFalse(TEXT("reject invalid layer weight"),Session.SetAnimationFloat(Weight,Invalid))) return false;++Rejected;}
            }
            const float Time=F->GetNumberField(TEXT("elapsed")),Duration=F->GetNumberField(TEXT("baseLength"));
            if(!TestTrue(TEXT("source layer duration"),FMath::IsNearlyEqual(Session.AnimDuration(),Duration,1.e-4f))||
                !TestTrue(TEXT("source layer normalized clock"),FMath::IsNearlyEqual(Time/Session.AnimDuration(),float(F->GetNumberField(TEXT("baseNormalizedTime"))),2.e-5f))||!Session.ApplyAnimFrame(Time,1.f/60.f)) return false;
            for(int32 I=0;I<Session.SkelBoneCount();++I) {
                OkBoneView B{};if(!Session.SkelBoneAt(I,B)) return false;
                for(float V:B.pose_pos) if(!FMath::IsFinite(V)) return false;for(float V:B.pose_rot) if(!FMath::IsFinite(V)) return false;for(float V:B.pose_scl) if(!FMath::IsFinite(V)) return false;
            }
            ++Frames;
        }
        const FString Key=KeyFor(Data[0]->AsObject());
        if(!Session.SetAnimClip(Key)||!Session.SetAnimationFloat(TEXT("OK.LayerWeight.7"),0.f)||!Session.SetAnimationFloat(TEXT("OK.LayerWeight.8"),1.f)||!Session.ApplyAnimFrame(.61f,0.f)) return false;
        TMap<FString,FQuat> Before;
        for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B)) return false;Before.Add(UTF8_TO_TCHAR(B.name),FQuat(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]));}
        if(!Session.SetAnimationFloat(TEXT("OK.LayerStartPhase.8"),.37f)||!Session.ApplyAnimFrame(.61f,0.f)) return false;
        int32 CardChanged=0;
        for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B)) return false;CardChanged+=!Before[UTF8_TO_TCHAR(B.name)].Equals(FQuat(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]),1.e-4);}
        if(!TestTrue(TEXT("independent start phase changes published bone palette"),CardChanged>0)) return false;
        Changed+=CardChanged;++Cards;AddInfo(FString::Printf(TEXT("LayerClock card=%s frames=20 changed_bones=%d source_scene_auto=1 physics_enabled=1"),Card,CardChanged));
    }
    AddInfo(FString::Printf(TEXT("LayerClock accepted cards=%d frames=%d changed_bones=%d invalid_weights_rejected=%d invalid_clocks_rejected=%d"),Cards,Frames,Changed,Rejected,RejectedClock));
    return Cards==5&&Frames==100&&Changed>0&&Rejected==520&&RejectedClock==1320;
}
#endif
