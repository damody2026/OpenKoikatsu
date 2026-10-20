#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMisc.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatorLayerFadeTest,"OpenKoikatsu.AnimationCatalog.AnimatorLayerCrossFade",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatorLayerFadeTest::RunTest(const FString&)
{
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore{FString Old;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),Old.IsEmpty()?nullptr:*Old);}} Restore{OldExternal};
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),nullptr);
    const FString Payload=TEXT("[\"h/anim/female/05_01_00.unity3d\",\"khr_f_02_00\",\"\",\"\"]");
    FString Hex;FTCHARToUTF8 Utf8(*Payload);for(int32 I=0;I<Utf8.Length();++I) Hex+=FString::Printf(TEXT("%02X"),uint8(Utf8.Get()[I]));
    const FString Source=TEXT("controller@")+Hex;
    int32 Frames=0,Cards=0,Changed=0,Rejected=0,Zero=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        Session.SkelBoneCount();Session.SetAnimSceneMode(0);
        FString Stem=Card;for(TCHAR& C:Stem) if(!FChar::IsAlnum(C)&&C!=TEXT('_')) C=TEXT('_');
        const FString Fixture=FPaths::ProjectSavedDir()/TEXT("Tests/AnimatorLayerFade302")/(Stem+TEXT("AnimatorLayerFade302.json.layer-fade.json"));
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
        auto FloatSnapshot=[&]() {TMap<FString,float> Out;for(int32 I=0;I<Session.AnimationFloatCount();++I){FString Name;float FloatValue,Min,Max;if(!Session.AnimationFloatAt(I,Name,FloatValue,Min,Max))return TMap<FString,float>();Out.Add(Name,FloatValue);}return Out;};
        for(const auto& Row:Data) {
            const auto F=Row->AsObject();if(!F.IsValid())return false;
            const FString Key=KeyFor(F);const int32 Index=int32(F->GetNumberField(TEXT("layer")));
            if(!Session.SetAnimClip(Key))return false;
            for(const auto& P:F->GetArrayField(TEXT("parameters"))){const auto Param=P->AsObject();if(!Session.SetAnimationFloat(Param->GetStringField(TEXT("name")),Param->GetNumberField(TEXT("value"))))return false;}
            for(const auto& L:F->GetArrayField(TEXT("layers"))){const auto Layer=L->AsObject();const int32 I=int32(Layer->GetNumberField(TEXT("layer")));if(!Session.SetAnimationFloat(FString::Printf(TEXT("OK.LayerWeight.%d"),I),Layer->GetNumberField(TEXT("weight"))))return false;if(I<73)for(const auto& Pair:{TPair<FString,FString>(TEXT("OK.LayerStartTime."),TEXT("startTime")),TPair<FString,FString>(TEXT("OK.LayerStartPhase."),TEXT("startPhase"))})if(!Session.SetAnimationFloat(Pair.Key+FString::FromInt(I),Layer->GetNumberField(Pair.Value)))return false;}
            const float Start=F->GetNumberField(TEXT("start")),Elapsed=F->GetNumberField(TEXT("elapsed")),BaseLength=F->GetNumberField(TEXT("baseLength"));
            if(!Session.ApplyAnimFrame(Start,0.f))return false;const auto Before=FloatSnapshot();
            for(int32 InvalidIndex:{0,999,73}){if(Session.CrossFadeAnimationLayer(Key,InvalidIndex,.4f,.67f)||Session.AnimationElapsed()!=Start||!Before.OrderIndependentCompareEqual(FloatSnapshot()))return false;++Rejected;}
            for(float InvalidDuration:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){if(Session.CrossFadeAnimationLayer(Key,Index,InvalidDuration,.67f)||!Before.OrderIndependentCompareEqual(FloatSnapshot()))return false;++Rejected;}
            if(Session.CrossFadeAnimationLayer(TEXT("not_a_state"),Index,.4f,0.f)||Session.CrossFadeAnimationLayer(Key,Index,.4f,std::numeric_limits<float>::quiet_NaN())||!Before.OrderIndependentCompareEqual(FloatSnapshot()))return false;Rejected+=2;
            if(!Session.CrossFadeAnimationLayer(Key,Index,F->GetNumberField(TEXT("blend")),F->GetNumberField(TEXT("offset")))||Session.AnimationElapsed()!=Start||!FMath::IsNearlyEqual(Session.AnimationTransitionSourceLength(),float(F->GetNumberField(TEXT("sourceLength"))),1.e-4f))return false;
            const auto After=FloatSnapshot();const FString Epoch=FString::Printf(TEXT("OK.LayerStartTime.%d"),Index),Phase=FString::Printf(TEXT("OK.LayerStartPhase.%d"),Index);
            for(const auto& Pair:Before)if(Pair.Key!=Epoch&&Pair.Key!=Phase&&(!After.Contains(Pair.Key)||After[Pair.Key]!=Pair.Value))return false;
            if(!After.Contains(Epoch)||!After.Contains(Phase)||After[Epoch]!=Start||!FMath::IsNearlyEqual(After[Phase],float(F->GetNumberField(TEXT("offset"))),1.e-6f))return false;
            if(Session.CrossFadeAnimationLayer(Key,Index,.4f,0.f)||Session.SetAnimationFloat(TEXT("height"),.5f)||Session.SetAnimationTrigger(TEXT("Restart")))return false;Rejected+=3;
            if(!Session.ApplyAnimFrame(Elapsed,1.f/60.f)||Session.IsAnimationTransitioning()!=F->GetBoolField(TEXT("transitioning"))||!FMath::IsNearlyEqual(Session.AnimDuration(),BaseLength,1.e-4f))return false;
            for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B))return false;for(float V:B.pose_pos)if(!FMath::IsFinite(V))return false;for(float V:B.pose_rot)if(!FMath::IsFinite(V))return false;for(float V:B.pose_scl)if(!FMath::IsFinite(V))return false;}
            ++Frames;
        }
        const FString Key=KeyFor(Data[0]->AsObject());
        if(!Session.SetAnimClip(Key)||!Session.SetAnimationFloat(TEXT("OK.LayerWeight.7"),0.f)||!Session.SetAnimationFloat(TEXT("OK.LayerWeight.8"),1.f)||!Session.ApplyAnimFrame(.61f,0.f))return false;
        TArray<FQuat> Before;for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B))return false;Before.Emplace(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]);}
        if(!Session.CrossFadeAnimationLayer(Key,8,0.f,.67f)||Session.IsAnimationTransitioning()||Session.AnimationElapsed()!=.61f||Session.AnimationTransitionSourceLength()!=0.f||!Session.ApplyAnimFrame(.61f,0.f))return false;
        int32 CardChanged=0;for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};Session.SkelBoneAt(I,B);CardChanged+=!Before[I].Equals(FQuat(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]),1.e-4);}
        if(!TestTrue(TEXT("layer offset changes published palette"),CardChanged>0))return false;
        ++Zero;Changed+=CardChanged;++Cards;AddInfo(FString::Printf(TEXT("LayerFade card=%s frames=20 changed_bones=%d"),Card,CardChanged));
    }
    AddInfo(FString::Printf(TEXT("LayerFade accepted cards=%d frames=%d changed_bones=%d invalid_rejected=%d zero_duration=%d"),Cards,Frames,Changed,Rejected,Zero));
    return Cards==5&&Frames==100&&Changed>0&&Rejected==1100&&Zero==5;
}
#endif
