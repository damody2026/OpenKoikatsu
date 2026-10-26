// Her own history and the phone (included by OKStreetSocialScene.cpp after the finance apps, whose small widgets it uses).
//
// Docs/STREET_BEHAVIOR_PLAN_20261012.md stage 2. Everything between him and one heroine is written to her history:
// face-to-face lines (FreeTalkAddLine), every event the rules record (MindObserve), phone messages and calls. The
// phone app shows one thread per heroine and her full record; the model reads the recent record each turn.
// Apart, the same free-talk pipeline carries the conversation: RemotePeer/RemoteMode say who is on the other end
// (1 typing, 2 a call — the call is the live voice conversation). A message she sends by herself is written by the
// model from her reason (ComposeHerText).

namespace {
// The UI font has no emoji: astral-plane characters (surrogate pairs) and the variation selector are left out of what is shown.
// The record's kinds in words.
FString OKHistoryKindLabel(const FString& Kind){
 static const TMap<FString,FString> Labels={{TEXT("talk"),TEXT("對話")},{TEXT("message"),TEXT("訊息")},{TEXT("call"),TEXT("通話")},{TEXT("call_missed"),TEXT("未接")},{TEXT("act"),TEXT("親密")},{TEXT("topic"),TEXT("話題")},{TEXT("touch"),TEXT("觸碰")},{TEXT("confession"),TEXT("告白")},{TEXT("act_refused"),TEXT("被拒")},
  {TEXT("activity"),TEXT("活動")},{TEXT("encounter"),TEXT("巧遇")},{TEXT("treat"),TEXT("請客")},{TEXT("errand"),TEXT("跑腿")},{TEXT("friends"),TEXT("聚會")},{TEXT("stage"),TEXT("關係")},{TEXT("breakup"),TEXT("分手")},
  {TEXT("promise"),TEXT("約定")},{TEXT("date"),TEXT("約會")},{TEXT("betrayal"),TEXT("爽約")},{TEXT("outing"),TEXT("出遊")},{TEXT("meal"),TEXT("用餐")},{TEXT("gift"),TEXT("禮物")},{TEXT("intimacy"),TEXT("親密")},{TEXT("sleep_together"),TEXT("過夜")},{TEXT("invite"),TEXT("邀約")}};
 const FString* L=Labels.Find(Kind);return L?FString(OKT(**L)):Kind;
}
FString OKPhoneShown(const FString& In){FString Out;Out.Reserve(In.Len());for(const TCHAR C:In){if((C>=0xD800&&C<=0xDFFF)||C==0xFE0F||C==0x200D)continue;Out.AppendChar(C);}return Out;}
constexpr int32 OKHistoryKeep=2000,OKHistoryPromptEvents=8,OKHistoryPromptPhone=8;
const TCHAR* OKPhoneDoingWord(const FString& Kind){
 return Kind==TEXT("class")?OKT(TEXT("ui.phone_inbox.activity_class")):Kind==TEXT("work")?OKT(TEXT("ui.phone_inbox.activity_part_time")):Kind==TEXT("meal")?OKT(TEXT("ui.life.eat_meal_label")):Kind==TEXT("sleep")?OKT(TEXT("ui.phone_inbox.activity_sleep")):Kind==TEXT("rest")?OKT(TEXT("ui.phone_inbox.activity_rest_home"))
  :Kind==TEXT("errand")?OKT(TEXT("ui.phone_inbox.errand_shopping")):Kind==TEXT("social")?OKT(TEXT("ui.phone_inbox.activity_friend_party")):Kind==TEXT("study")?OKT(TEXT("ui.campus.study_label")):Kind==TEXT("bath")?OKT(TEXT("ui.phone_inbox.activity_shower")):Kind==TEXT("promise")?OKT(TEXT("ui.phone_inbox.keep_appointment")):OKT(TEXT("ui.phone_inbox.activity_wander"));
}
}

// ---------------------------------------------------------------------------------------------------- history

TArray<FOKStreetHistoryEntry>& AOKStreetSocialScene::HistoryOf(const FString& Id){
 auto& S=*State;auto& List=S.History.FindOrAdd(Id);
 if(!S.HistoryLoaded.Contains(Id)){
  S.HistoryLoaded.Add(Id);
  const FString Path=HistoryPath(Id);TArray<FString> Lines;OKStreetFlushSaves(); // lines still being appended
  if(!Path.IsEmpty()&&FFileHelper::LoadFileToStringArray(Lines,*Path)){
   TArray<FOKStreetHistoryEntry> Loaded;
   for(const FString& Line:Lines){FOKStreetHistoryEntry E;if(OKStreetHistoryParse(Line,E))Loaded.Add(MoveTemp(E));}
   // Entries later than the save's own time belong to a timeline that was rolled back (the save restored from an
   // earlier copy, the history left as it was): she would remember things that never happened. They move to an
   // archive beside the file instead of being read.
   {const int64 Now=OKStreetMindNow(S);FString Abandoned;int32 Moved=0;
    for(int32 K=Loaded.Num()-1;K>=0;--K)if(Loaded[K].Minute>Now){Abandoned=OKStreetHistoryLine(Loaded[K])+TEXT("\n")+Abandoned;Loaded.RemoveAt(K);++Moved;}
    if(Moved>0){
     FFileHelper::SaveStringToFile(Abandoned,*(FPaths::GetBaseFilename(Path,false)+TEXT(".abandoned.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
     FString Out;for(const auto& E:Loaded)Out+=OKStreetHistoryLine(E)+TEXT("\n");FFileHelper::SaveStringToFile(Out,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
     UE_LOG(LogTemp,Warning,TEXT("Street history: %s had %d entries after the save's time (a rolled-back save); moved to .abandoned.jsonl"),*Id,Moved);}}
   // The file only grows (lines are appended): past the limit it is rewritten with the newest entries.
   if(Loaded.Num()>OKHistoryKeep){Loaded.RemoveAt(0,Loaded.Num()-OKHistoryKeep);FString Out;for(const auto& E:Loaded)Out+=OKStreetHistoryLine(E)+TEXT("\n");FFileHelper::SaveStringToFile(Out,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
   Loaded.Append(MoveTemp(List));List=MoveTemp(Loaded); // anything added before the first read stays last
  }
 }
 return List;
}

FString AOKStreetSocialScene::HistoryPath(const FString& Id) const{
 if(!State||State->SelfTest||State->Manifest.SavePath.IsEmpty()||Id.IsEmpty())return FString();
 return FPaths::GetPath(State->Manifest.SavePath)/TEXT("history")/(Id+TEXT(".jsonl"));
}

void AOKStreetSocialScene::HistoryAdd(int32 Character,const FString& Kind,const FString& Who,const FString& Text,const FString& Place){
 if(!State||Character<=0||!State->Manifest.Characters.IsValidIndex(Character)||Text.IsEmpty())return;auto& S=*State;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 FOKStreetHistoryEntry E;E.Serial=FDateTime::UtcNow().GetTicks();E.Minute=OKStreetMindNow(S);E.Kind=Kind;E.Who=Who;E.Text=Text;
 if(OKStreetHistoryIsPhone(Kind))MindRemember(Character,Kind,OKMsg(TEXT("ui.memory.phone_entry"),{{TEXT("kind"),Kind},{TEXT("who"),Who},{TEXT("text"),Text}}),Place,Kind==TEXT("message")?0.2f:0.15f);
 // A place id (from the rules) becomes its name; none given: where he is, or where she is when they are apart.
 FString Where=Place;
 if(Where.IsEmpty())Where=RemoteWith(Character)&&S.Agents.IsValidIndex(Character-1)?PlaceHere(S.Agents[Character-1].Position):PlaceHere(S.Male);
 if(const auto* P=Where.IsEmpty()?nullptr:OKStreetFindPlace(Where))Where=P->Name;
 E.Place=Where.IsEmpty()?FString(OKT(TEXT("ui.etiquette.street_location"))):Where;
 auto& List=HistoryOf(Id);
 if(!List.IsEmpty())E.Serial=FMath::Max(E.Serial,List.Last().Serial+1);
 List.Add(E);if(List.Num()>OKHistoryKeep)List.RemoveAt(0,List.Num()-OKHistoryKeep);
 const FString Path=HistoryPath(Id);
 if(!Path.IsEmpty())OKStreetAppendQueued(Path,OKStreetHistoryLine(E)+TEXT("\n")); // written by the save task (OKStreetFlushSaves before it is read back)
}

// For the model: the latest events between them (and, apart, the latest phone traffic), with day, time and place.
FString AOKStreetSocialScene::HistoryPromptText(int32 Character){
 if(!State||!State->Manifest.Characters.IsValidIndex(Character)||Character<=0)return FString();
 const auto& List=HistoryOf(OKStreetStableId(State->Manifest.Characters[Character]));
 // Events with him first; her own routine (meals, shifts, errands, other heroines) at most once. The phone lines
 // too: all recent ones apart, the last few face to face (so she remembers what she just wrote).
 static const TSet<FString> Routine={TEXT("meal"),TEXT("shared_shift"),TEXT("errand"),TEXT("encounter"),TEXT("treat"),TEXT("friends"),TEXT("initiative"),TEXT("summary"),TEXT("reflection"),TEXT("rumor")};
 const bool Apart=RemoteWith(Character);const int32 PhoneWant=Apart?OKHistoryPromptPhone:3;
 TArray<FString> Events,Phone;FString Own;
 for(int32 K=List.Num()-1;K>=0&&(Events.Num()<OKHistoryPromptEvents||Phone.Num()<PhoneWant||Own.IsEmpty());--K){
  const auto& E=List[K];
  // Formatted only when it is used: most entries are talk lines, skipped, and a heroine with no phone lines had the
  // whole history formatted (9.7 ms a turn).
  const bool IsPhone=OKStreetHistoryIsPhone(E.Kind);
  if(!IsPhone&&E.Kind==TEXT("talk"))continue;
  if(IsPhone?Phone.Num()>=PhoneWant:Routine.Contains(E.Kind)?!Own.IsEmpty():Events.Num()>=OKHistoryPromptEvents)continue;
  const FString Line=OKMsg(TEXT("ui.phone_inbox.when_place_message"), {{TEXT("when"), *OKStreetCalendarLabel(int32(E.Minute/1440),(E.Minute%1440)/60.0)}, {TEXT("place"), *E.Place}, {TEXT("speaker"), E.Who==TEXT("him")?OKT(TEXT("ui.mind.he_label")):E.Who==TEXT("her")?OKT(TEXT("ui.economy.you_speaker_label")):TEXT("")}, {TEXT("text"), *E.Text}});
  if(OKStreetHistoryIsPhone(E.Kind)){if(Phone.Num()<PhoneWant)Phone.Insert(Line,0);}
  else if(E.Kind==TEXT("talk"))continue;
  else if(Routine.Contains(E.Kind)){if(Own.IsEmpty())Own=Line;}
  else if(Events.Num()<OKHistoryPromptEvents)Events.Insert(Line,0);
 }
 if(!Own.IsEmpty())Events.Add(OKT(TEXT("ui.phone_inbox.your_daily_life"))+Own);
 FString Out;
 if(!Events.IsEmpty())Out+=OKT(TEXT("ui.phone_inbox.recent_record_header"))+FString::Join(Events,TEXT("\n"));
 if(!Phone.IsEmpty())Out+=OKT(TEXT("ui.phone_inbox.recent_phone_header"))+FString::Join(Phone,TEXT("\n"));
 return Out;
}

// ---------------------------------------------------------------------------------------------------- apart

bool AOKStreetSocialScene::RemoteWith(int32 Character) const{
 return RemoteKind(Character)!=0;
}
// 0 face to face, 1 typing, 2 a call. A reply on its way keeps the mode it was sent in (he may hang up meanwhile).
int32 AOKStreetSocialScene::RemoteKind(int32 Character) const{
 if(!State||Character<=0)return 0;const auto& S=*State;
 if(S.PhoneComposing==Character)return 1;
 if(S.ReplyRemoteGirl==Character)return S.ReplyRemoteMode;
 return S.RemoteMode!=0&&S.RemotePeer==Character?S.RemoteMode:0;
}

FString AOKStreetSocialScene::RemoteDoing(int32 Character) const{
 const auto& S=*State;const bool Call=RemoteKind(Character)==2;
 FString Her=S.Agents.IsValidIndex(Character-1)?PlaceHere(S.Agents[Character-1].Position):FString();
 if(const auto* P=Her.IsEmpty()?nullptr:OKStreetFindPlace(Her))Her=P->Name;
 const FOKStreetPlanBlock* B=LifeBlock(Character,int32(OKStreetMindNow(S)%1440));
 return OKMsg(TEXT("ui.phone_inbox.apart_status_prompt"), {{TEXT("mode_hint"), Call?OKT(TEXT("ui.phone_inbox.call_style_hint")):OKT(TEXT("ui.phone_inbox.text_style_hint"))}, {TEXT("your_place"), Her.IsEmpty()?OKT(TEXT("ui.etiquette.street_location")):*Her}, {TEXT("your_doing"), B?OKPhoneDoingWord(B->Kind):OKT(TEXT("ui.phone_inbox.activity_wander"))}}); // not where he is: over the phone she knows that only if he says it (or somebody saw him)
}

// ---------------------------------------------------------------------------------------------------- phone

int32 AOKStreetSocialScene::PhoneUnread(int32 Girl){
 if(!State||!State->Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return 0;auto& S=*State;PhoneLoadSeen();
 const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);const int64 Seen=S.PhoneSeen.FindRef(Id);int32 N=0;
 for(const auto& E:HistoryOf(Id))if(E.Who==TEXT("her")&&OKStreetHistoryIsPhone(E.Kind)&&E.Serial>Seen)++N;
 return N;
}

void AOKStreetSocialScene::PhoneLoadSeen(){
 auto& S=*State;if(S.PhoneSeenLoaded)return;S.PhoneSeenLoaded=true;
 if(S.SelfTest||S.Manifest.SavePath.IsEmpty())return;FString Text;TSharedPtr<FJsonObject> O;
 if(!FFileHelper::LoadFileToString(Text,*(FPaths::GetPath(S.Manifest.SavePath)/TEXT("phone.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O)||!O.IsValid())return;
 const TSharedPtr<FJsonObject>* Seen=nullptr;
 if(O->TryGetObjectField(TEXT("seen"),Seen))for(const auto& V:(*Seen)->Values){FString Str;S.PhoneSeen.Add(FString(*V.Key),V.Value->TryGetString(Str)?FCString::Atoi64(*Str):int64(V.Value->AsNumber()));}
 // Messages she still meant to send when the game was closed go out shortly after it starts.
 const TArray<TSharedPtr<FJsonValue>>* Pending=nullptr;
 if(O->TryGetArrayField(TEXT("pending"),Pending))for(const auto& V:*Pending)if(const auto P=V->AsObject()){
  FOKStreetSceneState::FPendingText T;T.Girl=CharacterOf(P->GetStringField(TEXT("heroine")));T.Why=P->GetStringField(TEXT("why"));T.Kind=P->GetStringField(TEXT("kind"));T.Due=SceneTime()+FMath::FRandRange(10.0,40.0);
  if(T.Girl!=INDEX_NONE)S.PendingTexts.Add(T);}
}

void AOKStreetSocialScene::PhoneMarkSeen(int32 Girl){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return;PhoneLoadSeen();
 const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);const auto& List=HistoryOf(Id);
 if(List.IsEmpty()||S.PhoneSeen.FindRef(Id)>=List.Last().Serial)return;
 S.PhoneSeen.Add(Id,List.Last().Serial);PhoneSave();
}

void AOKStreetSocialScene::PhoneOpen(int32 Girl){
 auto& S=*State;
 const auto& Fa=S.Store.Banking;
 if(Girl==OKStreetFatherThread||(Girl==INDEX_NONE&&Fa.FatherSeen<Fa.FatherTexts.Num())){
  S.PhoneThread=OKStreetFatherThread;{auto C=S.Store;C.Banking.FatherSeen=C.Banking.FatherTexts.Num();CommitStore(MoveTemp(C));}
  if(S.App!=TEXT("phone"))OpenApp(TEXT("phone"),false);else RefreshUI();return;
 }
 if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0){
  // The one with unread messages first, else whoever he is with, else the first.
  Girl=INDEX_NONE;for(int32 I=1;I<S.Manifest.Characters.Num()&&Girl==INDEX_NONE;++I)if(PhoneUnread(I)>0)Girl=I;
  if(Girl==INDEX_NONE)Girl=S.PhoneThread>0?S.PhoneThread:FreeTalkPartner()>0?FreeTalkPartner():1;
 }
 S.PhoneThread=Girl;PhoneMarkSeen(Girl);
 if(S.App!=TEXT("phone"))OpenApp(TEXT("phone"),false);else RefreshUI();
}

void AOKStreetSocialScene::PhoneSend(){
 auto& S=*State;const int32 Girl=S.PhoneThread;const FString Text=S.PhoneDraft.TrimStartAndEnd();
 if(Text.IsEmpty()||!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return;
 if(!HasHerNumber(Girl)){S.FinNotice=OKT(TEXT("ui.phone_inbox.no_number"));RefreshUI();return;}
 if(S.FreeTalkBusy){S.FinNotice=OKT(TEXT("ui.phone_inbox.she_still_replying"));RefreshUI();return;}
 if(S.RemoteMode==2&&S.RemotePeer!=Girl){S.FinNotice=OKT(TEXT("ui.phone_inbox.busy_in_call"));RefreshUI();return;}
 if(S.RemoteMode!=2){S.RemoteMode=1;S.RemotePeer=Girl;}
 S.SystemNote=false;S.PhoneDraft.Reset();S.FreeTalkDraft=Text;SendFreeTalk();PhoneMarkSeen(Girl);RefreshUI();
}

// Whether she picks up: by stage, what she is doing and resentment (social.rs phone_answer).
float AOKStreetSocialScene::PhoneAnswerChance(int32 Girl) const{
 const auto& S=*State;const FOKStreetPlanBlock* B=LifeBlock(Girl,int32(OKStreetMindNow(S)%1440));
 auto Request=OKRulesRequest(TEXT("phone_answer"));Request->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget)));
 Request->SetStringField(TEXT("block"),B?B->Kind:FString());
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);return Answer?float(Answer->GetNumberField(TEXT("chance"))):0.f;
}

void AOKStreetSocialScene::PhoneCall(int32 Girl){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return;
 if(!HasHerNumber(Girl)){S.FinNotice=OKT(TEXT("ui.phone_inbox.no_number"));S.Message=S.FinNotice;RefreshUI();return;}
 {FString Why;if(PhoneCutOff(&Why)){S.FinNotice=Why;S.Message=Why;RefreshUI();return;}}
 if(S.RemoteMode==2){S.FinNotice=OKT(TEXT("ui.phone_inbox.in_call_now"));RefreshUI();return;}
 if((S.Target!=INDEX_NONE&&S.Target!=Girl)||!NothingGoingOnExcept(Girl)){S.FinNotice=OKT(TEXT("ui.phone_inbox.busy_with_others"));RefreshUI();return;}
 const FString Name=S.Manifest.Characters[Girl].Name;
 if(FMath::FRand()>=PhoneAnswerChance(Girl)){
  HistoryAdd(Girl,TEXT("call_missed"),TEXT("him"),OKT(TEXT("ui.phone_inbox.he_called_no_answer")));
  S.FinNotice=Name+OKT(TEXT("ui.phone_inbox.no_answer_suffix"));
  // Friends and lovers call or write back once they can; someone who barely knows him does not.
  const FOKStreetRelation R=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget);
  if(R.Stage!=TEXT("stranger")&&R.Resentment<60&&!S.PendingTexts.ContainsByPredicate([Girl](const FOKStreetSceneState::FPendingText& T){return T.Girl==Girl;})){const FOKStreetPlanBlock* B=LifeBlock(Girl,int32(OKStreetMindNow(S)%1440));
   QueueHerText(Girl,OKMsg(TEXT("ui.phone_inbox.missed_call_while_doing"), {{TEXT("doing"), B?OKPhoneDoingWord(B->Kind):OKT(TEXT("ui.phone_inbox.busy_word"))}}),TEXT("missed_call"),FMath::FRandRange(20.0,60.0));}
  UE_LOG(LogTemp,Display,TEXT("Street phone: call to actor %d not answered"),Girl);RefreshUI();return;
 }
 if(S.App==TEXT("phone"))CloseApp();
 S.RemoteMode=2;S.RemotePeer=Girl;S.CallStarted=SceneTime();
 HistoryAdd(Girl,TEXT("call"),TEXT("him"),OKT(TEXT("ui.phone_inbox.he_called_answered")));
 S.FreeTalkOpen=true;FreeTalkLiveSet(true);
 S.SystemNote=true;S.FreeTalkDraft=OKT(TEXT("ui.phone_inbox.he_called_you_prompt"));SendFreeTalk();
 S.Message=OKMsg(TEXT("ui.phone_inbox.in_call_with"), {{TEXT("name"), *Name}});
 UE_LOG(LogTemp,Display,TEXT("Street phone: call to actor %d answered"),Girl);RefreshUI();
}

void AOKStreetSocialScene::PhoneHangUp(){
 auto& S=*State;if(S.RemoteMode!=2)return;const int32 Girl=S.RemotePeer;
 const int32 Seconds=FMath::RoundToInt(SceneTime()-S.CallStarted);
 if(S.FreeTalkLive)FreeTalkLiveSet(false);
 S.RemoteMode=0;S.RemotePeer=INDEX_NONE;
 HistoryAdd(Girl,TEXT("call"),TEXT("world"),OKMsg(TEXT("ui.phone_inbox.call_ended_duration"), {{TEXT("minutes"), Seconds/60}, {TEXT("seconds"), Seconds%60}}));
 if(FreeTalkPartner()==INDEX_NONE)FreeTalkClose(); // someone here with him: that talk goes on
 S.Message=OKT(TEXT("ui.phone_inbox.call_ended_notice"));RefreshUI();
 UE_LOG(LogTemp,Display,TEXT("Street phone: call with actor %d ended after %d s"),Girl,Seconds);
}

// Her initiative when he is far: a lover may call; otherwise she writes.
void AOKStreetSocialScene::ReachOutByPhone(int32 Girl){
 auto& S=*State;FString Why=S.InitiativeReason.FindRef(Girl);S.InitiativeReason.Remove(Girl);if(Why.IsEmpty())Why=OKT(TEXT("ui.phone_inbox.miss_him_hint"));
 const FOKStreetRelation R=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget);
 if(!HasHerNumber(Girl,true))return;
 if(R.Stage==TEXT("lover")&&FMath::FRand()<0.5f&&S.RingGirl==INDEX_NONE&&S.RemoteMode==0&&S.Target==INDEX_NONE&&SelfTextAllowed(Girl)){
  S.RingGirl=Girl;S.RingWhy=Why;S.RingUntil=SceneTime()+20.0;
  S.Message=OKMsg(TEXT("ui.phone_inbox.girl_calling_hint"), {{TEXT("girl_name"), *S.Manifest.Characters[Girl].Name}});RefreshUI();
  UE_LOG(LogTemp,Display,TEXT("Street phone: actor %d calls him (%s)"),Girl,*Why);return;
 }
 if(!SelfTextAllowed(Girl)){UE_LOG(LogTemp,Display,TEXT("Street phone: actor %d has written enough today"),Girl);return;}
 QueueHerText(Girl,Why,TEXT("initiative"),0.0);
}

void AOKStreetSocialScene::PhoneAnswer(bool Accept){
 auto& S=*State;const int32 Girl=S.RingGirl;if(Girl==INDEX_NONE)return;S.RingGirl=INDEX_NONE;
 if(!Accept){
  HistoryAdd(Girl,TEXT("call_missed"),TEXT("her"),OKT(TEXT("ui.phone_inbox.call_declined")));
  QueueHerText(Girl,S.RingWhy+OKT(TEXT("ui.phone_inbox.you_called_declined_note")),TEXT("call_rejected"),FMath::FRandRange(5.0,15.0));RefreshUI();return;
 }
 if(S.Target!=INDEX_NONE&&S.Target!=Girl)EndTalk();
 S.RemoteMode=2;S.RemotePeer=Girl;S.CallStarted=SceneTime();
 HistoryAdd(Girl,TEXT("call"),TEXT("her"),OKT(TEXT("ui.phone_inbox.call_answered")));
 S.FreeTalkOpen=true;FreeTalkLiveSet(true);
 S.SystemNote=true;S.FreeTalkDraft=OKT(TEXT("ui.phone_inbox.you_call_him_because"))+S.RingWhy+OKT(TEXT("ui.phone_inbox.he_picked_up_hint"));SendFreeTalk();
 S.Message=OKMsg(TEXT("ui.phone_inbox.in_call_with"), {{TEXT("name"), *S.Manifest.Characters[Girl].Name}});RefreshUI();
}

void AOKStreetSocialScene::QueueHerText(int32 Girl,const FString& Why,const FString& Kind,double DelaySeconds){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return;
 // Without each other's numbers she cannot write to him either (a stranger texted him after one hello).
 if(!HasHerNumber(Girl,true)){UE_LOG(LogTemp,Display,TEXT("Street phone: actor %d has no number of his; her text (%s) not sent"),Girl,*Kind);return;}
 PhoneLoadSeen();FOKStreetSceneState::FPendingText T;T.Girl=Girl;T.Why=Why;T.Kind=Kind;T.Due=SceneTime()+DelaySeconds;S.PendingTexts.Add(T);PhoneSave();
}

// She writes to him: the model writes the message from her reason, in her voice, with her memory; it joins her
// memory and history like any message.
void AOKStreetSocialScene::ComposeHerText(int32 Girl,const FString& Why,const FString& Kind,int32 Tries){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);
 // Right here with him: she comes over and says it instead of texting.
 if(S.Target==Girl||S.Companion==Girl||(S.Agents.IsValidIndex(Girl-1)&&FVector::Dist2D(S.Agents[Girl-1].Position,S.Male)<1000.f)){
  S.InitiativeReason.Add(Girl,Why);if(S.ApproachNext.IsValidIndex(Girl))S.ApproachNext[Girl]=FPlatformTime::Seconds();
  UE_LOG(LogTemp,Display,TEXT("Street phone: actor %d is close by; says it instead of texting (%s)"),Girl,*Kind);return;
 }
 const FString Nudge=OKT(TEXT("ui.phone_inbox.you_text_him_because"))+Why+SuspicionNote(Girl)+OKT(TEXT("ui.phone_inbox.reply_only_message_hint"));
 FOKFreeTalkSettings Settings;FString System,User,Error;
 S.PhoneComposing=Girl;const bool Ok=PrepareFreeTalk(Girl,Nudge,Settings,System,User,Error);S.PhoneComposing=0;
 if(!Ok){UE_LOG(LogTemp,Warning,TEXT("Street phone: cannot write for actor %d: %s"),Girl,*Error);return;}
 S.PhoneInFlight=true;
 TWeakPtr<FOKStreetSceneState> WeakState=State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 // Through the heroine mind (ok-mind): it calls the model and retries what can be retried.
 SubmitMindChat(FString::Printf(TEXT("text:%d"),Girl),TEXT("dialogue"),OKStreetFreeTalkMessages(System,S.FreeTalkMemory.FindOrAdd(Id),User),400,1.0f,[WeakState,Weak,Girl,Id,Nudge,Kind,Why,Tries](bool Ok,const FString& Content,const FString& Error){
  const auto L=WeakState.Pin();if(!L)return;L->PhoneInFlight=false;
  if(!Weak.IsValid()||Weak->State!=L)return;
  FOKFreeTalkReply Reply;FString Failure=Error;bool Retry=false;
  if(!Ok||!OKStreetFreeTalkParseContent(Content,Reply,Failure,Retry)||Reply.Reply.IsEmpty()){
   // Her message is not lost: tried again later, a few times with a growing wait.
   if(Tries<3){FOKStreetSceneState::FPendingText T;T.Girl=Girl;T.Why=Why;T.Kind=Kind;T.Tries=Tries+1;T.Due=Weak->SceneTime()+5.0*(1<<Tries);L->PendingTexts.Add(T);Weak->PhoneSave();}
   UE_LOG(LogTemp,Warning,TEXT("Street phone: message from actor %d failed (%s)%s"),Girl,*Failure,Tries<3?*FString::Printf(TEXT(", trying again (%d)"),Tries+1):TEXT(""));return;}
  auto& Memory=L->FreeTalkMemory.FindOrAdd(Id);Memory.Add({TEXT("user"),Nudge});Memory.Add({TEXT("assistant"),OKStreetFreeTalkMemoryReply(Reply)});
  OKStreetFreeTalkTrimMemory(Memory);Weak->FreeTalkSaveMemory();
  FString Where=L->Agents.IsValidIndex(Girl-1)?Weak->PlaceHere(L->Agents[Girl-1].Position):FString();
  Weak->HistoryAdd(Girl,TEXT("message"),TEXT("her"),Reply.Reply,Where);
  L->FreeTalkNotice=OKMsg(TEXT("ui.bank.heroine_reply_line"), {{TEXT("heroine_name"), *L->Manifest.Characters[Girl].Name}, {TEXT("reply"), *OKPhoneShown(Reply.Reply)}});
  if(L->App==TEXT("phone")&&L->PhoneThread==Girl)Weak->PhoneMarkSeen(Girl);
  UE_LOG(LogTemp,Display,TEXT("Street phone: actor %d writes (%s): %s"),Girl,*Kind,*Reply.Reply);
  Weak->RefreshUI();
 });
}

void AOKStreetSocialScene::TickPhone(){
 auto& S=*State;const double Now=SceneTime();
 // Typing ends with the phone put away (once her reply is in); a call ends when the live talk is switched off.
 if(S.RemoteMode==1&&S.App!=TEXT("phone")&&!S.FreeTalkBusy){S.RemoteMode=0;S.RemotePeer=INDEX_NONE;}
 if(S.RemoteMode==2&&!S.FreeTalkLive&&Now-S.CallStarted>2.0)PhoneHangUp();
 OKSetCrowdRuntimeSpeechRemote(this,S.RemoteMode==2?S.RemotePeer:INDEX_NONE); // her voice on the line, wherever she is
 if(S.RingGirl!=INDEX_NONE&&Now>S.RingUntil){
  const int32 Girl=S.RingGirl;S.RingGirl=INDEX_NONE;
  HistoryAdd(Girl,TEXT("call_missed"),TEXT("her"),OKT(TEXT("ui.phone_inbox.call_no_answer")));
  QueueHerText(Girl,S.RingWhy+OKT(TEXT("ui.phone_inbox.you_called_no_answer_note")),TEXT("call_missed"),3.0);
  S.Message=OKMsg(TEXT("ui.phone_inbox.missed_call_from"), {{TEXT("girl_name"), *S.Manifest.Characters[Girl].Name}});RefreshUI();
 }
 // One message on its way at a time.
 if(!S.PhoneInFlight)for(int32 K=0;K<S.PendingTexts.Num();++K)if(S.PendingTexts[K].Due<=Now){
  const auto T=S.PendingTexts[K];S.PendingTexts.RemoveAt(K);PhoneSave();ComposeHerText(T.Girl,T.Why,T.Kind,T.Tries);break;
 }
}

// ---------------------------------------------------------------------------------------------------- UI

void AOKStreetSocialScene::BuildPhoneRow(const TSharedRef<SVerticalBox>& Panel){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 int32 Unread=FMath::Max(0,S.Store.Banking.FatherTexts.Num()-S.Store.Banking.FatherSeen);for(int32 I=1;I<S.Manifest.Characters.Num();++I)Unread+=PhoneUnread(I);
 auto Row=SNew(SWrapBox).UseAllottedSize(true);
 Row->AddSlot().Padding(2)[OKFinButton(Weak,Unread>0?OKMsg(TEXT("ui.phone_inbox.phone_unread_label"), {{TEXT("unread_count"), Unread}}):FString(OKT(TEXT("ui.finance_apps.phone_label"))),[Weak](){Weak->PhoneOpen(INDEX_NONE);},true,Unread>0?FLinearColor(1,.6f,.6f):FLinearColor::White)];
 if(S.RingGirl!=INDEX_NONE){
  Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.phone_inbox.incoming_call_from"), {{TEXT("caller_name"), *S.Manifest.Characters[S.RingGirl].Name}}),FLinearColor(1,.85f,.3f),12,true)];
  Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.phone_inbox.answer_button")),[Weak](){Weak->PhoneAnswer(true);},true,FLinearColor(.5f,1,.5f))];
  Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.phone_inbox.decline_button")),[Weak](){Weak->PhoneAnswer(false);},true,FLinearColor(1,.5f,.5f))];
 }
 // A table booked here for later (a reservation: come, or the shop remembers).
 {const FString Here=PlaceHere(S.Male);const FString Label=Here.IsEmpty()?FString():FString(OKStreetNormsOf(Here).Label);
  if(!Here.IsEmpty()&&(Here==TEXT("restaurant")||Here==TEXT("cafe")||Here==TEXT("hotel")||Here==TEXT("snack")||Here==TEXT("dept_food")||Label.Contains(OKT(TEXT("ui.phone_inbox.meal_suffix")))))
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.phone_inbox.book_here_two_hours")),[Weak,Here](){Weak->BookTable(Here);})];}
 if(S.RemoteMode==2&&S.Manifest.Characters.IsValidIndex(S.RemotePeer)){
  Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.phone_inbox.in_call_label"), {{TEXT("peer_name"), *S.Manifest.Characters[S.RemotePeer].Name}}),FLinearColor(.5f,1,.5f),12,true)];
  Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.phone_inbox.hang_up_button")),[Weak](){Weak->PhoneHangUp();},true,FLinearColor(1,.5f,.5f))];
 }
 Panel->AddSlot().AutoHeight().Padding(2)[Row];
}

TSharedRef<SWidget> AOKStreetSocialScene::BuildPhoneApp(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 // The unknown number (his father): texts only, nobody answers.
 const auto& Fa=S.Store.Banking;
 auto FatherButton=[&](bool Selected)->TSharedRef<SWidget>{const int32 N=FMath::Max(0,Fa.FatherTexts.Num()-Fa.FatherSeen);
  return OKFinButton(Weak,FString(OKT(TEXT("ui.phone_inbox.unknown_number")))+(N>0?OKMsg(TEXT("ui.phone_inbox.count_paren"), {{TEXT("n"), N}}):FString()),[Weak](){Weak->PhoneOpen(OKStreetFatherThread);},true,Selected?FLinearColor(1,.85f,.3f):FLinearColor(.8f,.8f,1));};
 if(S.PhoneThread==OKStreetFatherThread&&!Fa.FatherTexts.IsEmpty()){
  auto People=SNew(SVerticalBox);People->AddSlot().AutoHeight().Padding(2)[FatherButton(true)];
  for(int32 I=1;I<S.Manifest.Characters.Num();++I){if(!HasHerNumber(I))continue;const int32 N=PhoneUnread(I);
   People->AddSlot().AutoHeight().Padding(2)[OKFinButton(Weak,S.Manifest.Characters[I].Name+(N>0?OKMsg(TEXT("ui.phone_inbox.count_paren"), {{TEXT("n"), N}}):FString()),[Weak,I](){Weak->State->PhoneThread=I;Weak->PhoneMarkSeen(I);Weak->RefreshUI();})];}
  auto Lines=SNew(SVerticalBox);
  for(const auto& T:Fa.FatherTexts)
   Lines->AddSlot().AutoHeight().Padding(FMargin(4,3,120,3)).HAlign(HAlign_Left)[SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(FLinearColor(.22f,.22f,.28f)).Padding(8)[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[OKFinText(T.Value,FLinearColor::White,12)]
    +SVerticalBox::Slot().AutoHeight()[OKFinText(OKStreetCalendarLabel(int32(T.Key/1440),(T.Key%1440)/60.0),FLinearColor(.65f,.65f,.7f),8)]]];
  TSharedRef<SScrollBox> Scroll=SNew(SScrollBox).ScrollBarAlwaysVisible(true)+SScrollBox::Slot()[Lines];Scroll->ScrollToEnd();
  return SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)[SNew(SBox).WidthOverride(220)[SNew(SScrollBox)+SScrollBox::Slot()[People]]]
   +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[OKFinText(OKT(TEXT("ui.phone_inbox.unknown_number")),FLinearColor(1,.85f,.3f),16,true)]
    +SVerticalBox::Slot().FillHeight(1).Padding(0,6)[Scroll]
    +SVerticalBox::Slot().AutoHeight()[OKFinText(OKMsg(TEXT("ui.phone_inbox.callback_empty_number"), {{TEXT("transfer_count"), Fa.FatherGifts}, {TEXT("total_amount"), *FText::AsNumber(Fa.FatherTotal).ToString()}}),FLinearColor(.65f,.65f,.7f),10)]];
 }
 int32 Girl=S.Manifest.Characters.IsValidIndex(S.PhoneThread)&&S.PhoneThread>0?S.PhoneThread:1;
 if(!HasHerNumber(Girl))for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(HasHerNumber(I)){Girl=I;break;}
 // Nobody's number yet (a new game): said so, without a thread, a call button or a send box for nobody.
 if(!HasHerNumber(Girl)){auto People=SNew(SVerticalBox);if(!Fa.FatherTexts.IsEmpty())People->AddSlot().AutoHeight().Padding(2)[FatherButton(false)];
  return SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[People]+SVerticalBox::Slot().AutoHeight().Padding(6)[OKFinText(OKT(TEXT("ui.phone_inbox.no_contacts")),FLinearColor(.7f,.7f,.7f),12)];}
 PhoneMarkSeen(Girl); // what is on screen is read
 // Left: everyone, with unread counts (the unknown number on top once it has written).
 auto People=SNew(SVerticalBox);
 if(!Fa.FatherTexts.IsEmpty())People->AddSlot().AutoHeight().Padding(2)[FatherButton(false)];
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){if(!HasHerNumber(I))continue; // only the numbers he has (contacts.rs)
  const int32 N=PhoneUnread(I);const FString Label=S.Manifest.Characters[I].Name+(N>0?OKMsg(TEXT("ui.phone_inbox.count_paren"), {{TEXT("n"), N}}):FString());
  People->AddSlot().AutoHeight().Padding(2)[OKFinButton(Weak,Label,[Weak,I](){Weak->State->PhoneThread=I;Weak->PhoneMarkSeen(I);Weak->RefreshUI();},true,I==Girl?FLinearColor(1,.85f,.3f):FLinearColor::White)];
 }
 // Right: her thread or her full record.
 const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);const auto& List=HistoryOf(Id);const bool Log=S.PhoneTab==TEXT("log");
 auto Lines=SNew(SVerticalBox);int32 Shown=0;
 auto When=[](const FOKStreetHistoryEntry& E){return OKMsg(TEXT("ui.phone_inbox.when_place"), {{TEXT("when"), *OKStreetCalendarLabel(int32(E.Minute/1440),(E.Minute%1440)/60.0)}, {TEXT("place"), *E.Place}});};
 if(Log){
  for(int32 K=List.Num()-1;K>=0&&Shown<200;--K,++Shown){const auto& E=List[K];
   const FString Who=E.Who==TEXT("him")?S.Manifest.Characters[0].Name+OKT(TEXT("ui.economy.colon_mark")):E.Who==TEXT("her")?S.Manifest.Characters[Girl].Name+OKT(TEXT("ui.economy.colon_mark")):FString();
   Lines->AddSlot().AutoHeight().Padding(2)[OKFinText(OKPhoneShown(OKMsg(TEXT("ui.phone_inbox.history_entry"), {{TEXT("when"), *When(E)}, {TEXT("kind_label"), *OKHistoryKindLabel(E.Kind)}, {TEXT("who"), *Who}, {TEXT("text"), *E.Text}})),E.Kind==TEXT("act_refused")||E.Kind==TEXT("breakup")?FLinearColor(1,.5f,.5f):E.Who==TEXT("her")?OKStreetSpeakerColor(Girl):E.Who==TEXT("him")?OKStreetPlayerLineColor():FLinearColor(.9f,.9f,.9f),10,false,true)];}
 }else{
  TArray<const FOKStreetHistoryEntry*> Phone;for(int32 K=List.Num()-1;K>=0&&Phone.Num()<80;--K)if(OKStreetHistoryIsPhone(List[K].Kind))Phone.Insert(&List[K],0);
  for(const auto* E:Phone){
   const bool Mine=E->Who==TEXT("him");
   // Call entries are written from her side (for her memory: "he" is the player). The phone shows them from his side.
   FString Call=E->Text;
   if(E->Kind!=TEXT("message")){
    static const TCHAR* Pairs[][2]={{TEXT("他打電話來，你沒有接"),TEXT("你打電話給她，她沒有接")},{TEXT("他打電話給你，你接了"),TEXT("你打電話給她，她接了")},{TEXT("你打電話給他，他按掉了"),TEXT("她打電話給你，你按掉了")},{TEXT("你打電話給他，他接了"),TEXT("她打電話給你，你接了")},{TEXT("你打電話給他，他沒有接"),TEXT("她打電話給你，你沒有接")}};
    for(const auto& P:Pairs)if(Call==P[0]||Call==OKT(P[0])){Call=OKT(P[1]);break;}
   }
   // A photo: the picture itself (from its file), its caption under it.
   {FString File,Caption;if(E->Kind==TEXT("photo")&&OKStreetPhotoEntry(E->Text,File,Caption)){
    Lines->AddSlot().AutoHeight().Padding(Mine?FMargin(120,3,4,3):FMargin(4,3,120,3)).HAlign(Mine?HAlign_Right:HAlign_Left)[SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(Mine?FLinearColor(.2f,.45f,.25f):FLinearColor(.22f,.22f,.28f)).Padding(6)[SNew(SVerticalBox)
     +SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(320).HeightOverride(180)[SNew(SImage).Image(OKStreetPhotoBrush(File))]]
     +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[OKFinText(Caption,FLinearColor(.85f,.85f,.9f),10,false,true)]
     +SVerticalBox::Slot().AutoHeight()[OKFinText(Mine?OKStreetCalendarLabel(int32(E->Minute/1440),(E->Minute%1440)/60.0):When(*E),FLinearColor(.65f,.65f,.7f),8)]]];
    continue;}}
   const FString Text=OKPhoneShown(E->Kind==TEXT("message")?E->Text:OKT(TEXT("ui.phone_inbox.call_label"))+Call);
   Lines->AddSlot().AutoHeight().Padding(Mine?FMargin(120,3,4,3):FMargin(4,3,120,3)).HAlign(Mine?HAlign_Right:HAlign_Left)[SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(Mine?FLinearColor(.2f,.45f,.25f):FLinearColor(.22f,.22f,.28f)).Padding(8)[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[OKFinText(Text,Mine?OKStreetPlayerLineColor():OKStreetSpeakerColor(Girl),12,false,true)]
    // The place kept with an entry is where she was: under his own message only the time (he wrote it from elsewhere).
    +SVerticalBox::Slot().AutoHeight()[OKFinText(Mine?OKStreetCalendarLabel(int32(E->Minute/1440),(E->Minute%1440)/60.0):When(*E),FLinearColor(.65f,.65f,.7f),8)]]];
  }
  if(Phone.IsEmpty())Lines->AddSlot().AutoHeight().Padding(6)[OKFinText(OKT(TEXT("ui.phone_inbox.no_messages_yet")),FLinearColor(.7f,.7f,.7f),11)];
 }
 auto Tabs=SNew(SHorizontalBox)
  +SHorizontalBox::Slot().AutoWidth().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.phone_inbox.messages_tab")),[Weak](){Weak->State->PhoneTab=TEXT("msg");Weak->RefreshUI();},true,Log?FLinearColor::White:FLinearColor(1,.85f,.3f))]
  +SHorizontalBox::Slot().AutoWidth().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.phone_inbox.her_record_label")),[Weak](){Weak->State->PhoneTab=TEXT("log");Weak->RefreshUI();},true,Log?FLinearColor(1,.85f,.3f):FLinearColor::White)]
  +SHorizontalBox::Slot().AutoWidth().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.photos.album_tab")),[Weak](){Weak->State->PhoneTab=TEXT("album");Weak->RefreshUI();},true,S.PhoneTab==TEXT("album")?FLinearColor(1,.85f,.3f):FLinearColor::White)]
  +SHorizontalBox::Slot().FillWidth(1)[SNullWidget::NullWidget]
  +SHorizontalBox::Slot().AutoWidth().Padding(2)[OKFinButton(Weak,S.RemoteMode==2&&S.RemotePeer==Girl?FString(OKT(TEXT("ui.phone_inbox.hang_up_button"))):FString(OKT(TEXT("ui.phone_inbox.call_button"))),[Weak,Girl](){if(Weak->State->RemoteMode==2)Weak->PhoneHangUp();else Weak->PhoneCall(Girl);},true,FLinearColor(.5f,1,.5f))];
 auto Input=SNew(SHorizontalBox)
  +SHorizontalBox::Slot().FillWidth(1)[SNew(SEditableTextBox).Text(FText::FromString(S.PhoneDraft)).HintText(FText::FromString(OKT(TEXT("ui.phone_inbox.input_hint_enter"))))
   .OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->PhoneDraft=T.ToString();})
   .OnTextCommitted_Lambda([Weak](const FText& T,ETextCommit::Type How){if(Weak.IsValid()&&How==ETextCommit::OnEnter){Weak->State->PhoneDraft=T.ToString();Weak->PhoneSend();}})]
  +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[OKFinButton(Weak,S.FreeTalkBusy&&S.RemotePeer==Girl?FString(OKT(TEXT("ui.phone_inbox.peer_typing"))):FString(OKT(TEXT("ui.phone_inbox.send_button"))),[Weak](){Weak->PhoneSend();},!S.FreeTalkBusy)];
 // The thread opens at its newest message. The album tab shows his photos instead (send one to her from there).
 TSharedRef<SScrollBox> Scroll=SNew(SScrollBox).ScrollBarAlwaysVisible(true)+SScrollBox::Slot()[S.PhoneTab==TEXT("album")?BuildPhotoAlbum(Girl):StaticCastSharedRef<SWidget>(Lines)];if(!Log&&S.PhoneTab!=TEXT("album")){Scroll->ScrollToEnd();
  // Once more after the first layout: the bubbles wrap only then, and the newest one was left below the fold.
  TWeakPtr<SScrollBox> WeakScroll=Scroll;Scroll->RegisterActiveTimer(0.05f,FWidgetActiveTimerDelegate::CreateLambda([WeakScroll](double,float){if(const auto P=WeakScroll.Pin())P->ScrollToEnd();return EActiveTimerReturnType::Stop;}));}
 return SNew(SHorizontalBox)
  +SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)[SNew(SBox).WidthOverride(220)[SNew(SScrollBox)+SScrollBox::Slot()[People]]]
  +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[OKFinText(S.Manifest.Characters[Girl].Name,FLinearColor(1,.85f,.3f),16,true)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Tabs]
   +SVerticalBox::Slot().FillHeight(1)[Scroll]
   +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[Log?SNullWidget::NullWidget:StaticCastSharedRef<SWidget>(Input)]];
}

// Development: -StreetSocialStageTest=<dir> checks the 2026-10-12 plan end to end (needs DeepSeek): she writes first,
// he answers on the phone and she replies, a stranger refuses a kiss, the shared consent, the money reminder, a
// debt, a missed call; screenshots of the phone; results as "STAGE TEST" lines, then the game quits.
void AOKStreetSocialScene::TickStageTest(){
 auto& S=*State;FString Dir;if(!S.AllReady||!FParse::Value(FCommandLine::Get(),TEXT("StreetSocialStageTest="),Dir))return;
 static int32 Step=0;static double At=0;static int32 Base=0;static int32 Passed=0,Failed=0;static int64 StartSerial=0;static FOKStreetStore Saved;static TMap<FString,TArray<FOKFreeTalkTurn>> SavedMemory;
 const double Now=SceneTime();if(At==0)At=Now+6.0;if(Now<At)return;
 auto Check=[&](const TCHAR* Name,bool Ok,const FString& Detail){UE_LOG(LogTemp,Display,TEXT("STAGE TEST %s: %s %s"),Name,Ok?TEXT("PASS"):TEXT("FAIL"),*Detail);if(Ok)++Passed;else ++Failed;};
 auto Id=[&](int32 I){return OKStreetStableId(S.Manifest.Characters[I]);};
 auto Count=[&](int32 I,const TCHAR* Kind,const TCHAR* Who){int32 N=0;for(const auto& E:HistoryOf(Id(I)))if(E.Kind==Kind&&(!Who||E.Who==Who))++N;return N;};
 auto Shot=[&](const TCHAR* Name){FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("stage.%s.png"),Name),true,false);}; // with the UI (the phone is UI)
 switch(Step){
 case 0:{ // broken model JSON (a stray key at the end): the spoken lines are saved, not read out as code
  FOKFreeTalkReply R;FString E;bool Again=false;
  const bool Ok=OKStreetFreeTalkParseContent(OKT(TEXT("ui.phone_inbox.sample_reply_json")),R,E,Again);
  Check(TEXT("broken_json"),Ok&&R.Reply==OKT(TEXT("ui.phone_inbox.sample_reply_text")),R.Reply+E);}
  StartSerial=FDateTime::UtcNow().GetTicks();Saved=S.Store;SavedMemory=S.FreeTalkMemory;S.TestBooks=BooksSnapshot(); // put back at the end
  Base=Count(1,TEXT("message"),TEXT("her"));QueueHerText(1,OKT(TEXT("ui.phone_inbox.want_lunch_invite")),TEXT("test"),0.0);Step=1;At=Now+1;break;
 case 1: if(Count(1,TEXT("message"),TEXT("her"))>Base||Now-At>45){Check(TEXT("her_text"),Count(1,TEXT("message"),TEXT("her"))>Base,HistoryOf(Id(1)).Num()?HistoryOf(Id(1)).Last().Text:FString());Step=2;}break;
 case 2: PhoneOpen(1);Base=Count(1,TEXT("message"),TEXT("her"));S.PhoneDraft=OKT(TEXT("ui.phone_inbox.sure_what_eat"));PhoneSend();Step=3;At=Now+1;break;
 case 3: if(Count(1,TEXT("message"),TEXT("her"))>Base||Now-At>45){Check(TEXT("phone_reply"),Count(1,TEXT("message"),TEXT("her"))>Base&&Count(1,TEXT("message"),TEXT("him"))>0,HistoryOf(Id(1)).Last().Text);
   UE_LOG(LogTemp,Display,TEXT("STAGE TEST prompt: %s"),*HistoryPromptText(1).Replace(TEXT("\n"),TEXT(" | ")));Shot(TEXT("thread"));Step=4;At=Now+2;}break;
 case 4: S.PhoneTab=TEXT("log");RefreshUI();Step=5;At=Now+1.5;break;
 case 5: Shot(TEXT("record"));Step=6;At=Now+2;break;
 case 6:{ // a stranger: the kiss is refused and recorded
  CloseApp();
  int32 Stranger=INDEX_NONE;for(int32 I=1;I<S.Manifest.Characters.Num()&&Stranger==INDEX_NONE;++I)if(OKStreetViewRelation(S.Store,Id(I),S.Manifest.TalkBudget).Stage==TEXT("stranger"))Stranger=I;
  if(Stranger==INDEX_NONE){Check(TEXT("lover_gate"),true,TEXT("(no stranger in this save; skipped)"));}
  else{const int32 Before=Count(Stranger,TEXT("act_refused"),nullptr);DoAct(Stranger,TEXT("kiss"));Check(TEXT("lover_gate"),Count(Stranger,TEXT("act_refused"),nullptr)==Before+1,HistoryOf(Id(Stranger)).Last().Text+TEXT(" @ ")+HistoryOf(Id(Stranger)).Last().Place);
   Check(TEXT("private_consent"),!PrivateConsent(Stranger),S.Message);}
  Step=7;break;}
 case 7:{ // the money reminder is not a meeting: past due it stays pending while the money is open, done once settled
  const int32 A=AddAppointment(TEXT("entrust"),2,FString(),OKStreetMindNow(S)-120,24*60,OKT(TEXT("ui.phone_inbox.test_return_money")));
  S.AppointmentTickMinute=-1;TickAppointments();
  const FOKStreetAppointment* Found=S.Store.Appointments.FindByPredicate([&](const FOKStreetAppointment& X){return X.Id==A;});
  Check(TEXT("entrust_reminder"),Found&&Found->Status!=TEXT("missed"),Found?Found->Status:FString(TEXT("missing"))); // pending while the money is open, done once settled; never a no-show
  // Bills and debt are game rules now (Tools/ok-rules day.rs, cargo test money_allowance_income_bills_debt_and_repaying).
  S.RingGirl=3;S.RingWhy=OKT(TEXT("ui.phone_inbox.want_hear_voice"));S.RingUntil=Now+1.0;Base=Count(3,TEXT("call_missed"),TEXT("her"));Step=8;At=Now+2.5;break;}
 case 8: Check(TEXT("missed_call"),Count(3,TEXT("call_missed"),TEXT("her"))==Base+1&&S.PendingTexts.ContainsByPredicate([](const FOKStreetSceneState::FPendingText& T){return T.Girl==3;})||S.PhoneInFlight,TEXT(""));
  Step=9;At=Now+1;break;
 case 9: PhoneOpen(3);Step=10;At=Now+20;break; // her text after the missed call arrives meanwhile
 case 10: Shot(TEXT("missed"));Step=11;At=Now+2;break;
 case 11:{ // an activity together leaves time, money and a memory
  CloseApp();S.Agents[0].Position=S.Male+FRotator(0,S.Rotations[0].Yaw+90,0).Vector()*120.f;S.Agents[0].Held=true;
  Base=Count(1,TEXT("activity"),nullptr);BeginPhoto(1);Step=12;At=Now+8;break;}
 case 12: FinishPhoto();Step=13;At=Now+1;break;
 case 13: Check(TEXT("activity_aftermath"),Count(1,TEXT("activity"),nullptr)==Base+1,HistoryOf(Id(1)).Last().Text);Step=14;break;
 case 14:{ // a facility inside the restaurant itself
  const auto* P=OKStreetFindPlace(TEXT("restaurant"));
  if(!P||P->Rooms.IsEmpty()){Check(TEXT("facility_in_place"),false,TEXT("no restaurant"));Step=20;break;}
  const FVector C=P->Rooms[0].GetCenter();S.Male=FVector(C.X,C.Y,P->Rooms[0].Min.Z);S.Agents[1].Position=S.Male+FVector(100,0,0);S.Agents[1].Held=true;
  BeginFacility(0,2);Step=15;At=Now+1;break;}
 case 15: if(S.FacilityEntered||Now-At>40){Check(TEXT("facility_in_place"),S.FacilityEntered&&S.FacilityInPlace&&FVector::Dist2D(S.FacilityOrigin,S.Male)<600.f,FString::Printf(TEXT("entered=%d in_place=%d place=%s"),S.FacilityEntered,S.FacilityInPlace,*PlaceHere(S.Male)));Step=16;At=Now+3;}break;
 case 16: Shot(TEXT("facility"));Step=17;At=Now+1;break;
 case 17: Base=Count(2,TEXT("activity"),nullptr);FinishFacility();Step=18;At=Now+1;break;
 case 18: Check(TEXT("facility_aftermath"),Count(2,TEXT("activity"),nullptr)==Base+1,HistoryOf(Id(2)).Num()?HistoryOf(Id(2)).Last().Text:FString());Step=20;break;
 case 20:{ // seen by him, she takes the hotel lift to a spot upstairs
  const auto* P=OKStreetFindPlace(TEXT("hotel"));const FOKStreetModelAnchor* Up=nullptr;
  if(P)for(const auto& A:P->Anchors)if(A.Position.Z>400.f&&(!Up||A.Position.Z<Up->Position.Z))Up=&A;
  if(!P||!Up){Check(TEXT("npc_lift"),false,TEXT("no upstairs anchor in the hotel"));Step=22;break;}
  S.Male=P->Door+FVector(0,0,0);S.Agents[3].Position=P->Door+FVector(80,0,0);S.Agents[3].Held=true;S.ScheduleHeld.Add(4);S.Commutes.Remove(4);
  const bool Started=StartNpcLift(4,Up->Position);Check(TEXT("npc_lift_start"),Started,Up->Name);
  if(!Started){Step=22;break;}
  S.Yaw=0;S.Pitch=-15;S.Distance=600;Step=21;At=Now+90;break;}
 case 21:
  if(!S.NpcLifts.Contains(4)){const float Z=S.NpcLiftArrivedZ.FindRef(4);Check(TEXT("npc_lift"),Z>300.f,FString::Printf(TEXT("arrived at z=%.0f"),Z));Step=22;} // where she got off, before her day takes her on
  else if(S.NpcLifts[4].Stage==2&&!S.LifeDone.Contains(TEXT("liftshot"))){S.LifeDone.Add(TEXT("liftshot"));Shot(TEXT("lift"));}
  else if(Now>At){Check(TEXT("npc_lift"),false,FString::Printf(TEXT("stuck at stage %d"),S.NpcLifts[4].Stage));Step=22;}
  break;
 case 22:{ // round 3: the same appointment twice is one; money owed does not keep him from sleeping
  const int64 T=OKStreetMindNow(S)+600;
  const int32 A1=AddAppointment(TEXT("pickup"),1,TEXT("cafe"),T,20,OKT(TEXT("ui.phone_inbox.test_pickup")));const int32 A2=AddAppointment(TEXT("pickup"),1,TEXT("cafe"),T,20,OKT(TEXT("ui.phone_inbox.test_pickup")));
  Check(TEXT("appointment_dedupe"),A1!=0&&A1==A2,FString::Printf(TEXT("%d %d"),A1,A2));
  AddAppointment(TEXT("entrust"),2,FString(),OKStreetMindNow(S)+60,24*60,OKT(TEXT("ui.phone_inbox.test_repay_money")));
  const FOKStreetAppointment* Blocking=AppointmentBefore(OKStreetMindNow(S)+120);
  Check(TEXT("entrust_not_blocking"),!Blocking||Blocking->Kind!=TEXT("entrust"),Blocking?Blocking->Label:FString(TEXT("none")));
  // her pay is for the minutes she was there, once
  FOKStreetPlanBlock W;W.Start=600;W.End=720;W.Kind=TEXT("work");const TMap<int32,int32> SinceWas=S.PlanBlockSince;
  S.PlanBlockSince.Add(1,600);const int32 P1=HerShiftPay(1,W,60,660);S.PlanBlockSince.Add(1,680);const int32 P2=HerShiftPay(1,W,60,720);const int32 P3=HerShiftPay(1,W,60,720);
  S.PlanBlockSince=SinceWas;S.PaidShiftMinutes.Reset();
  Check(TEXT("shift_pay_once"),P1==60&&P2==40&&P3==0,FString::Printf(TEXT("%d %d %d"),P1,P2,P3));
  // three messages of her own a day
  S.SelfTexts.Remove(5);const bool T1=SelfTextAllowed(5);S.SelfTexts.FindOrAdd(5)[0]-=40;const bool T2=SelfTextAllowed(5);S.SelfTexts.FindOrAdd(5)[1]-=40;const bool T3=SelfTextAllowed(5);S.SelfTexts.FindOrAdd(5)[2]-=40;const bool T4=SelfTextAllowed(5);
  const bool TooSoon=[&]{S.SelfTexts.Remove(5);SelfTextAllowed(5);return !SelfTextAllowed(5);}();S.SelfTexts.Remove(5);
  Check(TEXT("self_text_cap"),T1&&T2&&T3&&!T4&&TooSoon,FString::Printf(TEXT("%d%d%d%d %d"),T1,T2,T3,T4,TooSoon));
  // a history file past the limit is rewritten to the newest entries when read
  const FString Fake=TEXT("stage-test-history");const FString Path=HistoryPath(Fake);
  if(!Path.IsEmpty()){FString Out;for(int32 K=0;K<2100;++K){FOKStreetHistoryEntry E;E.Serial=1000+K;E.Minute=K;E.Kind=TEXT("talk");E.Who=TEXT("her");E.Text=FString::Printf(TEXT("line %d"),K);Out+=OKStreetHistoryLine(E)+TEXT("\n");}
   IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FFileHelper::SaveStringToFile(Out,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
   S.HistoryLoaded.Remove(Fake);S.History.Remove(Fake);const int32 InMemory=HistoryOf(Fake).Num();TArray<FString> Lines;FFileHelper::LoadFileToStringArray(Lines,*Path);
   Check(TEXT("history_compact"),InMemory==2000&&Lines.Num()==2000&&HistoryOf(Fake)[0].Serial==1100,FString::Printf(TEXT("memory %d file %d first %lld"),InMemory,Lines.Num(),HistoryOf(Fake)[0].Serial));
   IFileManager::Get().Delete(*Path);S.History.Remove(Fake);S.HistoryLoaded.Remove(Fake);}
  // a night whose poses never come ready leaves nobody stuck
  if(S.SleepPhase==0){S.SleepPhase=1;S.SleepGirl=2;S.SleepNap=true;S.SleepAt=SceneTime()-50;S.SleepBed=S.Male;S.Agents[1].Held=true;TickSleep();
   Check(TEXT("sleep_abort"),S.SleepPhase==0&&S.SleepGirl==INDEX_NONE&&!S.SleepNap&&!S.Agents[1].Held&&!BusyWithPlayer(2),FString::Printf(TEXT("phase %d girl %d"),S.SleepPhase,S.SleepGirl));}
  Step=30;break;}
 case 30:{
  // Leave the save as it was: the store, her free-talk memory, and the history lines written by the test.
  CommitStore(FOKStreetStore(Saved));BooksRestore(S.TestBooks);S.FreeTalkMemory=SavedMemory;FreeTalkSaveMemory();
  OKStreetFlushSaves(); // the test's own appends first: then each file is rewritten without them
  for(auto& Pair:S.History){Pair.Value.RemoveAll([&](const FOKStreetHistoryEntry& E){return E.Serial>=StartSerial;});
   const FString Path=HistoryPath(Pair.Key);if(Path.IsEmpty())continue;FString Out;for(const auto& E:Pair.Value)Out+=OKStreetHistoryLine(E)+TEXT("\n");FFileHelper::SaveStringToFile(Out,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
  UE_LOG(LogTemp,Display,TEXT("STAGE TEST done: %d passed, %d failed"),Passed,Failed);FPlatformMisc::RequestExitWithStatus(false,0);Step=99;break;}
 default:break;
 }
}

// The appointments he has, each with a button to call it off (a wrong one the judge booked, or a change of plans).
// Collapsed under one button so the panel stays short.
void AOKStreetSocialScene::BuildAppointmentRows(const TSharedRef<SVerticalBox>& Panel){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);const int64 Now=OKStreetMindNow(S);
 TArray<const FOKStreetAppointment*> Pending;for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Kind!=TEXT("entrust"))Pending.Add(&A);
 {static int32 Logged=-1;if(Pending.Num()!=Logged){Logged=Pending.Num();UE_LOG(LogTemp,Display,TEXT("Street appointment rows: %d pending of %d"),Pending.Num(),S.Store.Appointments.Num());}}
 if(Pending.IsEmpty())return;
 Pending.Sort([](const FOKStreetAppointment& A,const FOKStreetAppointment& B){return A.Start<B.Start;});
 const FString Title=OKMsg(TEXT("ui.phone_inbox.appointments_label"), {{TEXT("toggle_mark"), S.AppointmentsOpen?TEXT("[-]"):TEXT("[+]")}, {TEXT("pending_count"), Pending.Num()}});
 Panel->AddSlot().AutoHeight().Padding(2)[SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,Title,[Weak](){Weak->State->AppointmentsOpen=!Weak->State->AppointmentsOpen;Weak->RefreshUI();})]];
 if(!S.AppointmentsOpen)return;
 for(const auto* A:Pending){const int32 Id=A->Id;const int64 Left=A->Start-Now;
  const FString When=FString::Printf(TEXT("%s %02d:%02d"),*OKStreetCalendarLabel(int32(A->Start/1440),0.0).Left(10),int32(A->Start%1440)/60,int32(A->Start%1440)%60);
  auto Row=SNew(SWrapBox).UseAllottedSize(true);
  Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.bank.date_text_line"), {{TEXT("date"), *When}, {TEXT("text"), *A->Label}}),Left<120?FLinearColor(1,.75f,.3f):FLinearColor::White,10)];
  Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.phone_inbox.cancel_promise_id"), {{TEXT("id"), Id}}),[Weak,Id](){Weak->CancelAppointment(Id);})];
  Panel->AddSlot().AutoHeight().Padding(2)[Row];}
}
// Whose number he has (contacts.rs phone_contact): given in talk, or a friend or lover, or someone he has written or
// called with already. The phone lists, calls and texts only them, both ways.
bool AOKStreetSocialScene::HasHerNumber(int32 Girl,bool SheReachesHim){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return false;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);
 auto Request=OKRulesRequest(TEXT("phone_contact"));OKRulesPutExt(S,Request);Request->SetStringField(TEXT("girl"),Id);Request->SetStringField(TEXT("way"),SheReachesHim?TEXT("hers"):TEXT("his"));
 Request->SetStringField(TEXT("stage"),OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget).Stage);
 Request->SetBoolField(TEXT("phone_history"),HistoryOf(Id).ContainsByPredicate([](const FOKStreetHistoryEntry& E){return OKStreetHistoryIsPhone(E.Kind);}));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);return !Out.IsValid()||Out->GetBoolField(TEXT("contact"));
}

// She gave him her number (the judge's gives_number): kept in the save; she remembers she gave it.
void AOKStreetSocialScene::HerNumberGiven(int32 Girl,const TCHAR* What){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return;const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);
 auto Request=OKRulesRequest(TEXT("contact_add"));OKRulesPutExt(S,Request);Request->SetStringField(TEXT("girl"),Id);Request->SetStringField(TEXT("what"),What);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid()||!Out->GetBoolField(TEXT("added")))return;
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);if(!CommitStore(MoveTemp(Candidate)))return;
 HeroineNote(Girl,TEXT("contact"),Out->GetStringField(TEXT("fact")),PlaceHere(S.Male),0.6f);
 if(FCString::Strcmp(What,TEXT("his"))!=0)S.FreeTalkNotice=OKMsg(TEXT("ui.phone_inbox.number_saved"),{{TEXT("name"),S.Manifest.Characters[Girl].Name}});
 UE_LOG(LogTemp,Display,TEXT("Street phone: actor %d %s"),Girl,FCString::Strcmp(What,TEXT("his"))==0?TEXT("took his number"):TEXT("gave her number"));RefreshUI();
}
