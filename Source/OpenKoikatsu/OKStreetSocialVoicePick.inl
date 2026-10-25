// Her own TTS voice (ok-tts Base backend, Tools/ok-tts/src/voicemgr.rs). By default every heroine speaks with the fixed
// voice of her personality; the player can ask for candidate voices from a description, listen to them, and keep one in
// her card (okVo chunk), where it travels with the card, or make it the default of that personality. Requests for her
// lines carry her card path, so a voice stored there is used. The server does the work; this file only asks and shows.

#include "GenericPlatform/GenericPlatformHttp.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"

static constexpr float OKVoicePickTimeout=240.f;
static const FButtonStyle* OKStreetGlassButton(); // the panel's button look, defined with the panel in OKStreetSocialScene.cpp

// Her card file (absolute), or empty when the selection has none for her.
static FString OKStreetCardFor(const FOKStreetSceneState& S,int32 Character){
 if(!S.HighCards.IsValidIndex(Character)||S.HighCards[Character].IsEmpty())return FString();
 const FString Full=FPaths::ConvertRelativePathToFull(S.HighCards[Character]);
 return IFileManager::Get().FileExists(*Full)?Full:FString();
}

static TSharedRef<IHttpRequest,ESPMode::ThreadSafe> OKVoicePickPost(const FString& Url,const TSharedRef<FJsonObject>& Body,float Timeout){
 FString Payload;FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload));
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(Url);Request->SetVerb(TEXT("POST"));Request->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));Request->SetContentAsString(Payload);Request->SetTimeout(Timeout);
 return Request;
}

// The answer's JSON when it says ok; otherwise Error holds why.
static TSharedPtr<FJsonObject> OKVoicePickAnswer(FHttpResponsePtr Response,bool Connected,FString& Error){
 TSharedPtr<FJsonObject> J;bool Ok=false;Error=OKT(TEXT("ui.scene.tts_connect_failed"));
 if(!Connected||!Response.IsValid()||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J)||!J.IsValid())return nullptr;
 if(J->TryGetBoolField(TEXT("ok"),Ok)&&Ok)return J;
 J->TryGetStringField(TEXT("error"),Error);
 return nullptr;
}

// Whether her card already holds a voice of her own.
void AOKStreetSocialScene::ProbeCardVoice(){
 if(!State)return;auto& S=*State;S.CardHasVoice=false;
 const int32 Target=S.Target;const FString Card=OKStreetCardFor(S,Target);
 if(!S.TtsEnabled||Card.IsEmpty())return;
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(S.TtsEndpoint/TEXT("voice/card?card=")+FGenericPlatformHttp::UrlEncode(Card));Request->SetVerb(TEXT("GET"));Request->SetTimeout(5);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Target](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State||State->Target!=Target)return;FString Error;
  if(const auto J=OKVoicePickAnswer(Response,Connected,Error)){const TSharedPtr<FJsonObject>* Voice=nullptr;const bool Has=J->TryGetObjectField(TEXT("voice"),Voice)&&Voice&&Voice->IsValid();
   // Shown in the right panel only (and only when it changed: asked on every new target).
   if(Has!=State->CardHasVoice){State->CardHasVoice=Has;RefreshUI(OKStreetUi::Right);}}
 });
 Request->ProcessRequest();
}

void AOKStreetSocialScene::RequestVoiceCandidates(){
 if(!State)return;auto& S=*State;
 if(S.VoicePickBusy||!S.Manifest.Characters.IsValidIndex(S.Target)||S.TtsCacheDir.IsEmpty())return;
 const int32 Target=S.Target;
 auto Body=MakeShared<FJsonObject>();
 Body->SetStringField(TEXT("personality"),FString::FromInt(S.Manifest.Characters[Target].PersonalityId));Body->SetNumberField(TEXT("age"),S.TtsAge);
 Body->SetStringField(TEXT("out_dir"),S.TtsCacheDir/TEXT("voice-candidates"));Body->SetNumberField(TEXT("count"),3);
 if(!S.VoicePickDescription.TrimStartAndEnd().IsEmpty())Body->SetStringField(TEXT("description"),S.VoicePickDescription.TrimStartAndEnd());
 S.VoicePickBusy=true;S.VoicePickTarget=Target;S.VoicePickCandidates.Reset();S.VoicePickNotice=OKT(TEXT("ui.voice.voice_pick_busy"));RefreshUI();
 auto Request=OKVoicePickPost(S.TtsEndpoint/TEXT("voice/candidates"),Body,OKVoicePickTimeout);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Target](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State)return;auto& L=*State;L.VoicePickBusy=false;FString Error;
  const auto J=OKVoicePickAnswer(Response,Connected,Error);
  if(!J){L.VoicePickNotice=OKT(TEXT("ui.voice.voice_pick_failed"))+Error;UE_LOG(LogTemp,Warning,TEXT("Street voice candidates failed: %s"),*Error);RefreshUI();return;}
  const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
  if(L.Target==Target&&J->TryGetArrayField(TEXT("candidates"),List))for(const auto& V:*List){
   const auto C=V->AsObject();if(!C.IsValid())continue;FOKStreetSceneState::FVoiceCandidate Item;
   C->TryGetStringField(TEXT("candidate"),Item.Tag);C->TryGetStringField(TEXT("reference_path"),Item.Reference);C->TryGetStringField(TEXT("preview_path"),Item.Preview);
   if(!Item.Tag.IsEmpty())L.VoicePickCandidates.Add(Item);
  }
  L.VoicePickNotice.Reset();UE_LOG(LogTemp,Display,TEXT("Street voice candidates: %d for actor %d"),L.VoicePickCandidates.Num(),Target);RefreshUI();
 });
 Request->ProcessRequest();
}

void AOKStreetSocialScene::PlayVoiceCandidate(int32 Index,bool Reference){
 if(!State||!State->VoicePickCandidates.IsValidIndex(Index)||State->Target!=State->VoicePickTarget)return;auto& S=*State;
 const auto& C=S.VoicePickCandidates[Index];
 if(!FreeTalkSpeakVoice(S.Target,Reference?C.Reference:C.Preview)){S.VoicePickNotice=OKT(TEXT("ui.scene.voice_play_failed"));RefreshUI();}
}

// AsDefault: the voice of her personality for everyone with it; otherwise kept in her card. Index < 0: her current
// voice is written into her card as it is (fixing it there even if the default changes later).
void AOKStreetSocialScene::FixVoiceCandidate(int32 Index,bool AsDefault){
 if(!State||!State->Manifest.Characters.IsValidIndex(State->Target))return;auto& S=*State;
 const int32 Target=S.Target;const FString Card=OKStreetCardFor(S,Target);
 if(!AsDefault&&Card.IsEmpty()){S.VoicePickNotice=OKT(TEXT("ui.voice.voice_pick_no_card"));RefreshUI();return;}
 auto Body=MakeShared<FJsonObject>();
 Body->SetStringField(TEXT("personality"),FString::FromInt(S.Manifest.Characters[Target].PersonalityId));Body->SetNumberField(TEXT("age"),S.TtsAge);
 if(S.VoicePickCandidates.IsValidIndex(Index)&&S.VoicePickTarget==Target)Body->SetStringField(TEXT("candidate"),S.VoicePickCandidates[Index].Tag);
 if(AsDefault)Body->SetBoolField(TEXT("profile"),true);else Body->SetStringField(TEXT("card"),Card);
 auto Request=OKVoicePickPost(S.TtsEndpoint/TEXT("voice/fix"),Body,30.f);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,AsDefault,Card](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State)return;auto& L=*State;FString Error;
  if(!OKVoicePickAnswer(Response,Connected,Error)){L.VoicePickNotice=OKT(TEXT("ui.voice.voice_pick_failed"))+Error;RefreshUI();return;}
  L.VoicePickNotice=AsDefault?OKT(TEXT("ui.voice.voice_pick_saved_default")):OKT(TEXT("ui.voice.voice_pick_saved_card"));
  if(!AsDefault)L.CardHasVoice=true;
  UE_LOG(LogTemp,Display,TEXT("Street voice fixed: default=%d card=%s"),AsDefault?1:0,*Card);RefreshUI();
 });
 Request->ProcessRequest();
}

void AOKStreetSocialScene::RemoveCardVoice(){
 if(!State)return;auto& S=*State;const FString Card=OKStreetCardFor(S,S.Target);if(Card.IsEmpty())return;
 auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("card"),Card);
 auto Request=OKVoicePickPost(S.TtsEndpoint/TEXT("voice/card/remove"),Body,30.f);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State)return;auto& L=*State;FString Error;
  if(!OKVoicePickAnswer(Response,Connected,Error)){L.VoicePickNotice=OKT(TEXT("ui.voice.voice_pick_failed"))+Error;RefreshUI();return;}
  L.CardHasVoice=false;L.VoicePickNotice=OKT(TEXT("ui.voice.voice_pick_removed"));RefreshUI();
 });
 Request->ProcessRequest();
}

// The voice part of the right panel, under her voice description (TTS on, someone targeted).
void AOKStreetSocialScene::AddVoicePickUI(TSharedRef<SVerticalBox> Box){
 auto& S=*State;
 if(!S.TtsEnabled||!S.Manifest.Characters.IsValidIndex(S.Target))return;
 const TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 Box->AddSlot().AutoHeight().Padding(8,2)[SNew(STextBlock).Text(FText::FromString(S.CardHasVoice?OKT(TEXT("ui.voice.voice_pick_card_voice")):OKT(TEXT("ui.voice.voice_pick_card_none")))).AutoWrapText(true).WrapTextAt(360).Font(FCoreStyle::GetDefaultFontStyle("Regular",11))];
 Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.VoicePickOpen?OKT(TEXT("ui.voice.voice_pick_close")):OKT(TEXT("ui.voice.voice_pick_open"))))
  .OnClicked_Lambda([Weak](){if(Weak.IsValid()){auto& L=*Weak->State;L.VoicePickOpen=!L.VoicePickOpen;if(L.VoicePickOpen&&L.VoicePickDescription.IsEmpty())L.VoicePickDescription=L.TtsVoiceInstruct;Weak->RefreshUI();}return FReply::Handled();})];
 // Right under the button: the candidate list makes the panel long, and a note at its end went unseen.
 if(!S.VoicePickNotice.IsEmpty())Box->AddSlot().AutoHeight().Padding(8,2)[SNew(STextBlock).Text(FText::FromString(S.VoicePickNotice)).AutoWrapText(true).WrapTextAt(360)];
 if(S.VoicePickOpen){
  Box->AddSlot().AutoHeight().Padding(8,2)[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.voice.voice_pick_description_hint")))).AutoWrapText(true).WrapTextAt(360)];
  Box->AddSlot().AutoHeight().Padding(8,2)[SNew(SBox).WidthOverride(360).MaxDesiredHeight(120)[SNew(SMultiLineEditableTextBox).Text(FText::FromString(S.VoicePickDescription)).AutoWrapText(true)
   .OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->VoicePickDescription=T.ToString();})]];
  Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!S.VoicePickBusy).Text(FText::FromString(OKT(TEXT("ui.voice.voice_pick_generate"))))
   .OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->RequestVoiceCandidates();return FReply::Handled();})];
  if(S.VoicePickTarget==S.Target)for(int32 I=0;I<S.VoicePickCandidates.Num();++I){
   Box->AddSlot().AutoHeight().Padding(8,4,8,0)[SNew(STextBlock).Text(FText::FromString(OKMsg(TEXT("ui.voice.voice_pick_candidate"),{{TEXT("index"),I+1}})))];
   Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4,0)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.voice.voice_pick_listen")))).OnClicked_Lambda([Weak,I](){if(Weak.IsValid())Weak->PlayVoiceCandidate(I,false);return FReply::Handled();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.voice.voice_pick_reference")))).OnClicked_Lambda([Weak,I](){if(Weak.IsValid())Weak->PlayVoiceCandidate(I,true);return FReply::Handled();})]];
   Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4,0)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.voice.voice_pick_use_card")))).OnClicked_Lambda([Weak,I](){if(Weak.IsValid())Weak->FixVoiceCandidate(I,false);return FReply::Handled();})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.voice.voice_pick_use_default")))).OnClicked_Lambda([Weak,I](){if(Weak.IsValid())Weak->FixVoiceCandidate(I,true);return FReply::Handled();})]];
  }
  Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!S.CardHasVoice).Text(FText::FromString(OKT(TEXT("ui.voice.voice_pick_keep_current"))))
   .OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->FixVoiceCandidate(INDEX_NONE,false);return FReply::Handled();})];
  if(S.CardHasVoice)Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.voice.voice_pick_remove"))))
   .OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->RemoveCardVoice();return FReply::Handled();})];
 }
}
