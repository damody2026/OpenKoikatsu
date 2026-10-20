#include "OKGalleryWidget.h"
#include "OKInteractionScene.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "Dom/JsonObject.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Misc/ScopeExit.h"

namespace {
// VoiceInfo.Param.Personality for No 0–39, from abdata/etcetra/list/config/00.unity3d.
const TCHAR* HeroineVoiceNames[]={
    TEXT("性感"),TEXT("大小姐"),TEXT("高傲"),TEXT("後輩"),TEXT("神秘"),
    TEXT("電波"),TEXT("大和撫子"),TEXT("假小子"),TEXT("純真"),TEXT("單純"),
    TEXT("中二"),TEXT("母性"),TEXT("大姐頭"),TEXT("辣妹"),TEXT("不良少女"),
    TEXT("野性"),TEXT("高冷"),TEXT("彆扭"),TEXT("不幸少女"),TEXT("文學少女"),
    TEXT("害羞"),TEXT("正統派"),TEXT("跟風"),TEXT("宅女"),TEXT("病嬌"),
    TEXT("懶散"),TEXT("無口"),TEXT("固執"),TEXT("古風"),TEXT("坦率高冷"),
    TEXT("好相處"),TEXT("好勝"),TEXT("真誠"),TEXT("艷麗"),TEXT("歸國子女"),
    TEXT("方言"),TEXT("抖S"),TEXT("無感情"),TEXT("一絲不苟"),TEXT("島娘")
};
}
FString UOKCharacterController::HeroineVoiceLabel(int32 Personality) {
    if(Personality>=0&&Personality<UE_ARRAY_COUNT(HeroineVoiceNames))
        return FString::Printf(TEXT("c%02d %s"),Personality,HeroineVoiceNames[Personality]);
    return FString::Printf(TEXT("性格 %d"),Personality);
}
void UOKCharacterController::SyncSpeechVoiceToCard() {
    SelectedSpeechVoice=Selected.Sex==1&&Selected.Personality>=0&&Selected.Personality<=39?Selected.Personality:-1;
    if(!SpeechVoiceCombo)return;
    bSuppressSpeechVoice=true;
    SpeechVoiceCombo->SetSelectedIndex(SelectedSpeechVoice<0?0:SelectedSpeechVoice+1);
    bSuppressSpeechVoice=false;
}
void UOKCharacterController::OnSpeechVoiceSelected(FString,ESelectInfo::Type) {
    if(bSuppressSpeechVoice||!SpeechVoiceCombo)return;
    if(InteractionWidget)InteractionWidget->OnStopSpeech();
    const int32 Index=SpeechVoiceCombo->GetSelectedIndex();
    SelectedSpeechVoice=Index>=1&&Index<=40?Index-1:-1;
    OnLoadSpeech();
    if(IsValid(PairedPlayback))StartPairBreath();
}
void UOKCharacterController::OnLoadSpeech() {
    bSpeechWasPlaying=false;SpeechIds.Reset();if(SpeechCombo)SpeechCombo->ClearOptions();
    if(!IsValid(PreviewActor)||!SpeechRootInput){
        if(SpeechStatus)SpeechStatus->SetText(FText::FromString(TEXT("請先載入3D角色")));return;
    }
    // Card mode keeps the native rule: this common-topic catalog has no male bank,
    // including Voice.infoTable[-100]. An explicit c00–c39 pick is the user's choice.
    const bool bCardVoice=SelectedSpeechVoice<0;
    if(bCardVoice&&Selected.Sex!=1){
        PreviewActor->StopSpeech();
        if(SpeechStatus)SpeechStatus->SetText(FText::FromString(TEXT("此一般話題索引沒有男角語音；請改選 c00–c39 女角聲線")));return;
    }
    const int32 Personality=bCardVoice?Selected.Personality:SelectedSpeechVoice;
    if(InteractionWidget){InteractionWidget->PrepareSpeechCatalog(SpeechRootInput->GetText().ToString(),Personality,FMath::Lerp(.94f,1.06f,Selected.VoiceRate));return;}
    FString Error;
    if(!PreviewActor->LoadGeneralSpeech(SpeechRootInput->GetText().ToString(),Personality,
        FMath::Lerp(.94f,1.06f,Selected.VoiceRate),Error)){
        if(SpeechStatus)SpeechStatus->SetText(FText::FromString(Error));return;
    }
    for(const FKKSpeechLine& Line:PreviewActor->GetGeneralSpeechLines()){
        SpeechIds.Add(Line.Id);if(SpeechCombo)SpeechCombo->AddOption(Line.Text.IsEmpty()?Line.Id:Line.Text);
    }
    if(SpeechCombo&&!SpeechIds.IsEmpty())SpeechCombo->SetSelectedIndex(0);
    if(SpeechStatus)SpeechStatus->SetText(FText::FromString(FString::Printf(TEXT("%s · %d 句一般對話"),*HeroineVoiceLabel(Personality),SpeechIds.Num())));
    if(InteractionWidget)InteractionWidget->PreloadSpeech();
}
void UOKCharacterController::OnSpeak() {
    if(!IsValid(PreviewActor)||!SpeechCombo)return;
    const int32 Index=SpeechCombo->GetSelectedIndex();if(!SpeechIds.IsValidIndex(Index))return;
    FString Error;
    if(!PreviewActor->SpeakGeneralLine(SpeechIds[Index],Error)){
        if(SpeechStatus)SpeechStatus->SetText(FText::FromString(Error));
    }else {bSpeechWasPlaying=true;if(SpeechStatus)SpeechStatus->SetText(FText::FromString(TEXT("說話中")));}
}
void UOKCharacterController::OnStopSpeech() {
    bSpeechWasPlaying=false;
    if(IsValid(PreviewActor)){PreviewActor->SetSpeechPaused(false);PreviewActor->StopHBreath();}
    if(IsValid(PartnerActor)){PartnerActor->SetSpeechPaused(false);PartnerActor->StopHBreath();}
    if(SpeechStatus)SpeechStatus->SetText(FText::FromString(TEXT("語音已停止")));
}
void UOKCharacterController::StartPairBreath() {
    if(!IsValid(PairedPlayback)||!IsValid(PreviewActor)||!IsValid(PartnerActor)||!SpeechRootInput)return;
    AKKPreviewActor* Female=Selected.Sex==1?PreviewActor.Get():PartnerActor.Get();
    Female->BeginFaceDisplayUpdate();ON_SCOPE_EXIT{Female->EndFaceDisplayUpdate();};
    const bool bMoving=!PairedPlayback->IsPaused()&&PairSpeedSlider&&PairSpeedSlider->GetValue()*4.f>0.01f;
    bPairBreathMoving=bMoving;bPairBreathArmed=true;
    Female->SetSpeechPaused(PairedPlayback->IsPaused());
    Female->StopHBreath();
    const int32 PairIndex=PairCombo?PairCombo->GetSelectedIndex()-1:-1;
    const TSharedPtr<FJsonObject> Pair=NativePairs.IsValidIndex(PairIndex)?NativePairs[PairIndex]:nullptr;
    const FString Category=Pair.IsValid()?Pair->GetStringField(TEXT("category")):FString();
    const FString State=PairStateCombo?PairStateCombo->GetSelectedOption():FString();
    int32 Mode=-1;
    if(Category.StartsWith(TEXT("AnimationInfo_")))Mode=FCString::Atoi(*Category.Mid(15));
    const int32 Personality=SelectedSpeechVoice>=0?SelectedSpeechVoice:(Selected.Sex==1?Selected.Personality:PartnerPersonality);
    const float Rate=Selected.Sex==1?Selected.VoiceRate:PartnerVoiceRate;
    FString Error,MovingError,StillError;const FString Root=SpeechRootInput->GetText().ToString();const int32 AnimId=Pair.IsValid()?int32(Pair->GetNumberField(TEXT("id"))):-1;
    PairMovingBreaths.Reset();PairStillBreaths.Reset();
    const bool MovingReady=Mode>=0&&KKSpeech::CollectHBreath(Root,Personality,Mode,State,AnimId,true,PairMovingBreaths,MovingError);
    const bool StillReady=Mode>=0&&KKSpeech::CollectHBreath(Root,Personality,Mode,State,AnimId,false,PairStillBreaths,StillError);
    const auto& Cues=bMoving?PairMovingBreaths:PairStillBreaths;Error=bMoving?MovingError:StillError;
    if(!(bMoving?MovingReady:StillReady)||!Female->BeginHBreath(Cues,FMath::Lerp(.94f,1.06f,Rate),Error)){
        if(SpeechStatus)SpeechStatus->SetText(FText::FromString(Error.IsEmpty()?TEXT("這個動作沒有可播放的喘息"):Error));
        return;
    }
    bSpeechWasPlaying=true;
    if(SpeechStatus)SpeechStatus->SetText(FText::FromString(FString::Printf(TEXT("喘息 %s · %s · %d 種叫聲"),*HeroineVoiceLabel(Personality),*State,Cues.Num())));
}
