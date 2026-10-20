#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMisc.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatorParallelFadeTest,"OpenKoikatsu.AnimationCatalog.AnimatorParallelCrossFade",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatorParallelFadeTest::RunTest(const FString&)
{
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore{FString Old;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),Old.IsEmpty()?nullptr:*Old);}} Restore{OldExternal};
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),nullptr);
    const FString Payload=TEXT("[\"h/anim/female/05_01_00.unity3d\",\"khr_f_02_00\",\"\",\"\"]");
    FString Hex;FTCHARToUTF8 Utf8(*Payload);for(int32 I=0;I<Utf8.Length();++I) Hex+=FString::Printf(TEXT("%02X"),uint8(Utf8.Get()[I]));
    const FString Source=TEXT("controller@")+Hex;
    int32 Frames=0,Cards=0,Changed=0,Rejected=0,Zero=0,Events=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        Session.SkelBoneCount();Session.SetAnimSceneMode(0);
        FString Stem=Card;for(TCHAR& C:Stem) if(!FChar::IsAlnum(C)&&C!=TEXT('_')) C=TEXT('_');
        const FString Fixture=FPaths::ProjectSavedDir()/TEXT("Tests/AnimatorParallelFade303")/(Stem+TEXT("AnimatorParallelFade303.json.parallel-fade.json"));
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
        for(const auto& Row:Data){
            const auto F=Row->AsObject();if(!F.IsValid())return false;const FString Key=KeyFor(F);
            if(!Session.SetAnimClip(Key))return false;
            for(const auto& P:F->GetArrayField(TEXT("parameters"))){const auto Param=P->AsObject();if(!Session.SetAnimationFloat(Param->GetStringField(TEXT("name")),Param->GetNumberField(TEXT("value"))))return false;}
            for(const auto& L:F->GetArrayField(TEXT("layers"))){const auto Layer=L->AsObject();const int32 I=int32(Layer->GetNumberField(TEXT("layer")));if(!Session.SetAnimationFloat(FString::Printf(TEXT("OK.LayerWeight.%d"),I),Layer->GetNumberField(TEXT("weight"))))return false;if(I<73)for(const auto& Pair:{TPair<FString,FString>(TEXT("OK.LayerStartTime."),TEXT("startTime")),TPair<FString,FString>(TEXT("OK.LayerStartPhase."),TEXT("startPhase"))})if(!Session.SetAnimationFloat(Pair.Key+FString::FromInt(I),Layer->GetNumberField(Pair.Value)))return false;}
            TMap<int32,float> Lengths;
            for(const auto& Event:F->GetArrayField(TEXT("events"))){
                const auto E=Event->AsObject();const int32 Index=int32(E->GetNumberField(TEXT("layer")));const float Start=E->GetNumberField(TEXT("start"));
                if(!Session.ApplyAnimFrame(Start,0.f))return false;const auto Before=FloatSnapshot();
                for(int32 InvalidIndex:{0,999,73}){if(Session.CrossFadeAnimationLayer(Key,InvalidIndex,.4f,.67f)||Session.AnimationElapsed()!=Start||!Before.OrderIndependentCompareEqual(FloatSnapshot()))return false;++Rejected;}
                for(float InvalidDuration:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){if(Session.CrossFadeAnimationLayer(Key,Index,InvalidDuration,.67f)||!Before.OrderIndependentCompareEqual(FloatSnapshot()))return false;++Rejected;}
                if(Session.CrossFadeAnimationLayer(TEXT("not_a_state"),Index,.4f,0.f)||Session.CrossFadeAnimationLayer(Key,Index,.4f,std::numeric_limits<float>::quiet_NaN())||!Before.OrderIndependentCompareEqual(FloatSnapshot()))return false;Rejected+=2;
                if(!Session.CrossFadeAnimationLayer(Key,Index,E->GetNumberField(TEXT("blend")),E->GetNumberField(TEXT("offset")))||Session.AnimationElapsed()!=Start||!Session.IsAnimationLayerTransitioning(Index)||!FMath::IsNearlyEqual(Session.AnimationLayerTransitionSourceLength(Index),float(E->GetNumberField(TEXT("sourceLength"))),1.e-4f))return false;
                Lengths.Add(Index,E->GetNumberField(TEXT("sourceLength")));for(const auto& Pair:Lengths)if(!Session.IsAnimationLayerTransitioning(Pair.Key)||!FMath::IsNearlyEqual(Session.AnimationLayerTransitionSourceLength(Pair.Key),Pair.Value,1.e-4f))return false;
                if(!FMath::IsNearlyEqual(Session.AnimationTransitionSourceLength(),Lengths.Num()>1?0.f:float(E->GetNumberField(TEXT("sourceLength"))),1.e-4f))return false;
                const auto After=FloatSnapshot();const FString Epoch=FString::Printf(TEXT("OK.LayerStartTime.%d"),Index),Phase=FString::Printf(TEXT("OK.LayerStartPhase.%d"),Index);
                for(const auto& Pair:Before)if(Pair.Key!=Epoch&&Pair.Key!=Phase&&(!After.Contains(Pair.Key)||After[Pair.Key]!=Pair.Value))return false;
                if(!After.Contains(Epoch)||!After.Contains(Phase)||After[Epoch]!=Start||!FMath::IsNearlyEqual(After[Phase],float(E->GetNumberField(TEXT("offset"))),1.e-6f))return false;
                if(Session.CrossFadeAnimationLayer(Key,Index,.4f,0.f)||Session.CrossFadeAnimationLayer(Key,Index,0.f,0.f)||Session.CrossFadeAnimation(Key,.4f,0.f)||Session.SetAnimationFloat(TEXT("height"),.5f)||Session.SetAnimationTrigger(TEXT("Restart"))||!After.OrderIndependentCompareEqual(FloatSnapshot()))return false;Rejected+=5;++Events;
            }
            const float Elapsed=F->GetNumberField(TEXT("elapsed"));if(!Session.ApplyAnimFrame(Elapsed,1.f/60.f)||!FMath::IsNearlyEqual(Session.AnimDuration(),float(F->GetNumberField(TEXT("baseLength"))),1.e-4f))return false;
            bool Any=false;for(const auto& Event:F->GetArrayField(TEXT("events"))){const auto E=Event->AsObject();const int32 Index=int32(E->GetNumberField(TEXT("layer")));const bool Active=E->GetBoolField(TEXT("transitioning"));Any|=Active;if(Session.IsAnimationLayerTransitioning(Index)!=Active||(!Active&&Session.AnimationLayerTransitionSourceLength(Index)!=0.f))return false;}
            if(Session.IsAnimationTransitioning()!=Any||Session.IsAnimationLayerTransitioning(-1)||Session.AnimationLayerTransitionSourceLength(-1)!=0.f)return false;
            for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B))return false;for(float V:B.pose_pos)if(!FMath::IsFinite(V))return false;for(float V:B.pose_rot)if(!FMath::IsFinite(V))return false;for(float V:B.pose_scl)if(!FMath::IsFinite(V))return false;}++Frames;
        }
        const FString Key=KeyFor(Data[10]->AsObject());
        if(!Session.SetAnimClip(Key)||!Session.SetAnimationFloat(TEXT("OK.LayerWeight.7"),0.f)||!Session.SetAnimationFloat(TEXT("OK.LayerWeight.8"),1.f)||!Session.ApplyAnimFrame(.61f,0.f)||!Session.CrossFadeAnimationLayer(Key,7,.4f,.67f)||!Session.ApplyAnimFrame(.77f,0.f))return false;
        const float SourceLength=Session.AnimationLayerTransitionSourceLength(7);
        TArray<FQuat> Before;for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B))return false;Before.Emplace(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]);}
        if(!Session.CrossFadeAnimationLayer(Key,8,0.f,.43f)||!Session.IsAnimationTransitioning()||!Session.IsAnimationLayerTransitioning(7)||Session.IsAnimationLayerTransitioning(8)||Session.AnimationElapsed()!=.77f||Session.AnimationLayerTransitionSourceLength(8)!=0.f||Session.AnimationLayerTransitionSourceLength(7)!=SourceLength||!Session.ApplyAnimFrame(.77f,0.f))return false;
        int32 Visible=0;for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView B{};if(!Session.SkelBoneAt(I,B))return false;const FQuat Q(B.pose_rot[0],B.pose_rot[1],B.pose_rot[2],B.pose_rot[3]);if(!Q.Equals(Before[I],1.e-5f))++Visible;}if(!TestTrue(TEXT("instant second layer visibly changes palette"),Visible>0))return false;Changed+=Visible;++Zero;
        if(!Session.SetAnimClip(Key)||Session.IsAnimationTransitioning()||!Session.SetAnimationFloat(TEXT("height"),.37f))return false;++Cards;
    }
    AddInfo(FString::Printf(TEXT("ParallelFade accepted cards=%d frames=%d events=%d invalid_rejected=%d zero_during_active=%d changed=%d"),Cards,Frames,Events,Rejected,Zero,Changed));
    return TestEqual(TEXT("all five cards"),Cards,5)&&TestEqual(TEXT("parallel frames"),Frames,100)&&TestEqual(TEXT("parallel events"),Events,200)&&TestEqual(TEXT("atomic rejections"),Rejected,2600)&&TestEqual(TEXT("instant second layer"),Zero,5);
}
#endif
