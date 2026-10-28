// Included by OKStreetSocialScene.cpp: the heroine interaction module's scene side
// (Docs/STREET_SOCIAL_HEROINE_INTERACTION_MODULE.md). Interact() is the one way anything happens between him and a
// heroine; the table and the rules are the game rules' (Tools/ok-rules src/heroine.rs, acts.toml), which answer with the
// values, her record and the rumour; this side asks her (consent), writes them and plays the act. Systems that already
// settled their own values (the original voiced topics, touches, the confession) call it with RecordOnly so the record is
// still the same everywhere.

// A relation patch from the rules' answer ("relation"), written and saved. False when there was none or the save failed.
bool AOKStreetSocialScene::ApplyRulesRelation(const FString& Id,const FJsonObject& Answer){
 auto& S=*State;const TSharedPtr<FJsonObject>* Patch=nullptr;if(!Answer.TryGetObjectField(TEXT("relation"),Patch)||(*Patch)->Values.IsEmpty())return false;
 auto Candidate=S.Store;const int32 I=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);if(!Candidate.Relations.IsValidIndex(I))return false;
 OKRulesPatchRelation(Candidate.Relations[I],**Patch);
 return CommitStore(MoveTemp(Candidate));
}

// The rules' effects ("effects" of an answer), replayed in order on Candidate: money through the books (OKCash, OKBank,
// OKBookPost and the company wallets keep the journal reconciled), relation changes, her memories and record, rumours,
// messages she sends, log lines. A rumour or a record writes the live store itself, so while Candidate is a copy they
// wait until it is committed (CommitStore and NextDay flush them).
void AOKStreetSocialScene::ApplyRulesEffects(FOKStreetStore& Candidate,const FJsonObject& Answer){
 auto& S=*State;const TArray<TSharedPtr<FJsonValue>>* List=nullptr;if(!Answer.TryGetArrayField(TEXT("effects"),List))return;
 const bool Live=&Candidate==&S.Store;
 for(const auto& V:*List){
  const TSharedPtr<FJsonObject> E=V.IsValid()?V->AsObject():nullptr;if(!E)continue;
  const FString Op=E->GetStringField(TEXT("op"));
  if(!Live&&(Op==TEXT("rumor")||Op==TEXT("note"))){S.PendingRulesEffects.Add(E);continue;}FString Id;E->TryGetStringField(TEXT("id"),Id);const int32 Girl=Id.IsEmpty()?INDEX_NONE:CharacterOf(Id);
  auto Str=[&](const TCHAR* Name){FString Out;E->TryGetStringField(Name,Out);return Out;};auto Num=[&](const TCHAR* Name){double Out=0;E->TryGetNumberField(Name,Out);return Out;};
  if(Op==TEXT("cash"))OKCash(Candidate,int64(Num(TEXT("amount"))),Str(TEXT("category")),Str(TEXT("note")));
  else if(Op==TEXT("bank"))OKBank(Candidate,int64(Num(TEXT("amount"))),Str(TEXT("category")),Str(TEXT("note")));
  else if(Op==TEXT("post"))OKBookPost(Candidate,Str(TEXT("book")),Str(TEXT("account")),int64(Num(TEXT("amount"))),Str(TEXT("category")),Str(TEXT("note")));
  else if(Op==TEXT("relation")){const int32 I=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);if(Candidate.Relations.IsValidIndex(I)){
   auto& R=Candidate.Relations[I];const float Old=R.Favor;const FString OldStage=R.Stage;OKRulesPatchRelation(R,*E->GetObjectField(TEXT("set")));
   // Any favor gain (an activity, a topic, a gift) counts less the more lovers he already has (mind.rs favor_gain),
   // as a talk turn's does; a stage change (its own favor) is not a gain.
   if(R.Favor>Old&&R.Stage==OldStage&&R.Stage!=TEXT("lover")){int32 Lovers=0;for(const auto& X:Candidate.Relations)if(X.Id!=R.Id&&X.Stage==TEXT("lover"))++Lovers;
    if(Lovers>1){auto Request=OKRulesRequest(TEXT("favor_gain"));Request->SetStringField(TEXT("stage"),R.Stage);Request->SetNumberField(TEXT("favor"),Old);Request->SetNumberField(TEXT("limit"),R.Favor);
     Request->SetNumberField(TEXT("delta"),R.Favor-Old);Request->SetNumberField(TEXT("lovers"),Lovers);if(const TSharedPtr<FJsonObject> Gain=OKRulesEval(S,Request))R.Favor=float(Gain->GetNumberField(TEXT("favor")));}}
   // A big swing from a rule (not a talk turn): logged with where it came from, so an unexplained drop can be traced.
   if(FMath::Abs(R.Favor-Old)>=3.f)UE_LOG(LogTemp,Display,TEXT("Street favor effect: %s %.1f -> %.1f (%s; stage %s -> %s)"),*Id,Old,R.Favor,*GOKLastRule,*OldStage,*R.Stage);}}
  else if(Op==TEXT("observe")){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EffectObserve);if(Girl!=INDEX_NONE)MindObserve(Girl,Str(TEXT("kind")),Str(TEXT("text")),Str(TEXT("place")),float(Num(TEXT("importance"))),*Str(TEXT("type")));}
  else if(Op==TEXT("recent"))S.MindRecent.Add(Id,Str(TEXT("text")));
  else if(Op==TEXT("note")){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EffectNote);if(Girl!=INDEX_NONE){FOKHeroineArgs A;A.RecordOnly=true;A.Hers=E->GetBoolField(TEXT("hers"));A.Importance=float(Num(TEXT("importance")));A.Detail=Str(TEXT("detail"));A.Place=Str(TEXT("place"));A.RumorDetail=Str(TEXT("rumor_detail"));Interact(Girl,Str(TEXT("act")),A);}}
  else if(Op==TEXT("text")){if(Girl!=INDEX_NONE)QueueHerText(Girl,Str(TEXT("text")),Str(TEXT("kind")),Num(TEXT("delay")));}
  else if(Op==TEXT("rumor")){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EffectRumor);MakeRumor(Girl,Str(TEXT("kind")),Str(TEXT("text")),float(Num(TEXT("heat"))),E->GetBoolField(TEXT("secret")));}
  else if(Op==TEXT("wallet")||Op==TEXT("hero_open")||Op==TEXT("hero_close"))ApplyCompanyEffect(Candidate,*E);
  else if(Op==TEXT("bond_event")){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EffectBond);const int32 A=CharacterOf(Str(TEXT("a"))),B=CharacterOf(Str(TEXT("b")));if(A!=INDEX_NONE&&B!=INDEX_NONE)BondEventIn(Candidate,A,B,Str(TEXT("event")));}
  else if(Op==TEXT("mind_note")){if(Girl!=INDEX_NONE)MindNote(Girl,Str(TEXT("text")));}
  else if(Op==TEXT("intent"))S.Intents.Add(Id,Str(TEXT("intent")));
  else if(Op==TEXT("log")){UE_LOG(LogTemp,Display,TEXT("%s"),*Str(TEXT("text")));}
  else{UE_LOG(LogTemp,Warning,TEXT("Street rules: unknown effect %s"),*Op);}
 }
}

// What waited for the store to be committed (rumours, records), now on the live store.
void AOKStreetSocialScene::FlushRulesEffects(){
 auto& S=*State;if(S.PendingRulesEffects.IsEmpty())return;
 TArray<TSharedPtr<FJsonValue>> List;for(const auto& E:S.PendingRulesEffects)List.Add(MakeShared<FJsonValueObject>(E));S.PendingRulesEffects.Reset();
 auto Answer=MakeShared<FJsonObject>();Answer->SetArrayField(TEXT("effects"),List);ApplyRulesEffects(S.Store,*Answer);
}

// Steps 5 and 6 from the rules' answer: her history (a memory of hers when it says so; MindObserve writes the history line
// too) and what others hear.
void AOKStreetSocialScene::HeroineApplyRecord(int32 Girl,const FJsonObject& Answer,const FString& Place){
 const TSharedPtr<FJsonObject>* Record=nullptr;
 if(Answer.TryGetObjectField(TEXT("record"),Record)){
  const FString History=(*Record)->GetStringField(TEXT("history")),Text=(*Record)->GetStringField(TEXT("text"));const float Memory=float((*Record)->GetNumberField(TEXT("memory")));
  // Everything they do is hers to remember (with its time); an act the rules gave no weight gets the kind's default.
  if(Memory!=0.f)MindObserve(Girl,History,Text,Place,Memory); // negative: MindObserve weighs it
  else{HistoryAdd(Girl,History,(*Record)->GetBoolField(TEXT("hers"))?TEXT("her"):TEXT("him"),Text,Place);MindRemember(Girl,History,Text,Place,-1.f);}
 }
 const TSharedPtr<FJsonObject>* Rumor=nullptr;
 if(Answer.TryGetObjectField(TEXT("rumor"),Rumor))MakeRumor(Girl,(*Rumor)->GetStringField(TEXT("kind")),(*Rumor)->GetStringField(TEXT("text")),float((*Rumor)->GetNumberField(TEXT("heat"))),(*Rumor)->GetBoolField(TEXT("secret")));
}

// Below the gate (his idea): it goes into her memory, costs respect and trust when the rules say (once per 20 s), and
// she answers in her own words (once per 20 s, so a reply that repeats the act cannot loop).
void AOKStreetSocialScene::HeroineRefuseGate(int32 Girl,const FString& ActId,const FJsonObject& Answer){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);
 const TSharedPtr<FJsonObject>* Gate=nullptr;if(!Answer.TryGetObjectField(TEXT("gate"),Gate))return;
 const FString What=(*Gate)->GetStringField(TEXT("what"));const FString Place=PlaceHere(S.Male);
 MindObserve(Girl,TEXT("act_refused"),What,Place,float((*Gate)->GetNumberField(TEXT("importance"))));
 if(ApplyRulesRelation(Id,Answer))S.GatePenaltyAt=SceneTime();
 S.MindRecent.Add(Id,What);
 UE_LOG(LogTemp,Display,TEXT("Street heroine gate: actor=%d %s refused (stage %d)"),Girl,*ActId,int32((*Gate)->GetNumberField(TEXT("stage"))));
 if(FreeTalkPartner()==Girl&&!S.FreeTalkBusy&&SceneTime()-S.GateNudgeAt>20.0){
  S.GateNudgeAt=SceneTime();S.SystemNote=true;S.FreeTalkDraft=OKT(TEXT("ui.finance_apps.open_paren"))+What+OKT(TEXT("ui.heroine.react_in_character_suffix"));SendFreeTalk();
 }else{S.FreeTalkNotice=OKStreetScriptedName(S.Manifest.Characters[Girl].Name)+OKT(TEXT("ui.heroine.dodged_suffix"));RefreshUI();}
}

// Settled elsewhere (her consent and values were the caller's): her record and the rumour, in the caller's words.
void AOKStreetSocialScene::HeroineNote(int32 Girl,const TCHAR* ActId,const FString& Text,const FString& Place,float Importance,const FString& RumorDetail){
 FOKHeroineArgs A;A.RecordOnly=true;A.Detail=Text;A.Place=Place;A.Importance=Importance;A.RumorDetail=RumorDetail;Interact(Girl,ActId,A);
}

// Who pays (P2): her own leaning at a checkout, unless she said this one is on her (a treat back she came over for).
void AOKStreetSocialScene::HeroineCheckoutLean(int32 Girl,int32 Total,FString& Lean,float& Insist){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);
 bool Promise=false;
 if(const int64* Until=S.HerTreatPromise.Find(Girl)){if(*Until>=OKStreetMindNow(S))Promise=true;else S.HerTreatPromise.Remove(Girl);}
 auto Request=OKRulesHeroineRequest(S,TEXT("checkout_lean"),Id);Request->SetNumberField(TEXT("total"),Total);Request->SetBoolField(TEXT("promise"),Promise);
 Lean=TEXT("player_pays");Insist=0.f;
 if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){Lean=Answer->GetStringField(TEXT("lean"));Insist=float(Answer->GetNumberField(TEXT("insist")));}
}

float AOKStreetSocialScene::ChastityOf(int32 Girl) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl))return 100.f;
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesHeroineRequest(S,TEXT("heroine_chastity"),OKStreetStableId(S.Manifest.Characters[Girl])));
 return Out.IsValid()?float(Out->GetNumberField(TEXT("chastity"))):100.f;
}

// Her stranger cap is set once, when her profile is known (it is saved and the test panel can change it).
void AOKStreetSocialScene::InitStrangerCap(const FString& Id,const TSharedPtr<FJsonObject>& Profile){
 auto& S=*State;const FOKStreetRelation View=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);if(View.StrangerCap>0)return;
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRelationRequest(TEXT("heroine_stranger_cap"),View,Profile));if(!Out.IsValid())return;
 auto Candidate=S.Store;const int32 I=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);if(!Candidate.Relations.IsValidIndex(I))return;
 Candidate.Relations[I].StrangerCap=int32(Out->GetNumberField(TEXT("stranger_cap")));
 UE_LOG(LogTemp,Display,TEXT("Street stranger cap: %s = %d"),*Id,Candidate.Relations[I].StrangerCap);
 CommitStore(MoveTemp(Candidate));
}

// A stranger whose favor reached her cap is his friend now (whatever raised it: talk, topics, gifts, the test panel).
void AOKStreetSocialScene::TickAutoFriendship(){
 auto& S=*State;if(S.SelfTest)return;
 // Mid-conversation she says she is glad, once what she is saying now is over; his half-typed line stays his.
 for(auto It=S.HerSaysNext.CreateIterator();It;++It){
  const int32 Girl=It.Key();if(FreeTalkPartner()!=Girl){It.RemoveCurrent();continue;}
  if(S.FreeTalkBusy||S.FreeTalkSpeechPlayed<S.FreeTalkSpeechLines.Num()||OKStreetConversationBusy(S.Node!=INDEX_NONE,S.CollectedActive,S.RelationshipPending,S.TouchActive))continue;
  const FString Kept=S.FreeTalkDraft;S.SystemNote=true;S.FreeTalkDraft=It.Value();It.RemoveCurrent();SendFreeTalk();S.FreeTalkDraft=Kept;
  UE_LOG(LogTemp,Display,TEXT("Street auto friendship: actor=%d says she is glad"),Girl);break;
 }
 // One girl a frame, in turn (each is asked every ~10 frames): asking the rules for all of them every frame cost ~1.2 ms
 // a frame.
 const int32 Count=S.Manifest.Characters.Num()-1;
 for(int32 Tried=0;Tried<Count;++Tried){
  const int32 Girl=1+int32(S.AutoFriendshipNext++%uint32(FMath::Max(1,Count)));
  if(S.RelationshipPending&&S.Target==Girl)continue; // a voiced friendship scene is deciding it
  const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);const int32 Index=OKStreetFindRelation(S.Store,Id);if(Index==INDEX_NONE)continue;
  const FOKStreetRelation& R=S.Store.Relations[Index];if(R.Stage!=TEXT("stranger")||R.StrangerCap<=0)continue;
  // Reaching her cap makes them friends (talk.rs auto_friendship).
  auto Request=OKRulesRequest(TEXT("auto_friendship"));Request->SetObjectField(TEXT("relation"),OKRulesRelationJson(R));
  const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out||!Out->GetBoolField(TEXT("friend")))break; // asked: her turn is over
  const int32 Cap=int32(Out->GetNumberField(TEXT("cap")));
  auto Candidate=S.Store;OKRulesPatchRelation(Candidate.Relations[Index],*Out->GetObjectField(TEXT("relation")));
  if(!CommitStore(MoveTemp(Candidate)))continue;
  {FOKHeroineArgs A;A.RecordOnly=true;A.Detail=OKMsg(TEXT("ui.heroine.favor_reached"), {{TEXT("cap"), Cap}});Interact(Girl,TEXT("friendship"),A);}
  S.MindRecent.Add(Id,OKT(TEXT("ui.heroine.already_friends_thought")));
  if(FreeTalkPartner()==Girl)S.HerSaysNext.Add(Girl,OKT(TEXT("ui.heroine.friendship_prompt_note")));
  S.Message=OKMsg(TEXT("ui.heroine.became_friends"), {{TEXT("name"), *S.Manifest.Characters[Girl].Name}, {TEXT("cap"), Cap}});S.FreeTalkNotice=S.Message;
  UE_LOG(LogTemp,Display,TEXT("Street auto friendship: actor=%d cap=%d"),Girl,Cap);
  RefreshUI();break;
 }
}

// Alone with him though not his lover: his charm (look) against her resistance (chastity, charm resistance).
// Lovers: the private-room invitation like before.
bool AOKStreetSocialScene::AloneConsent(int32 Girl,const TCHAR* What){
 auto& S=*State;if(Girl<=0||!S.Manifest.Characters.IsValidIndex(Girl))return false;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 auto Request=OKRulesHeroineRequest(S,TEXT("heroine_private"),Id);Request->SetNumberField(TEXT("charm"),PlayerLook());Request->SetNumberField(TEXT("roll"),FMath::FRand());
 const TSharedPtr<FJsonObject> P=OKRulesEval(S,Request);if(!P.IsValid())return false;
 const float Chance=float(P->GetNumberField(TEXT("chance")));
 if(Chance<0.f)return PanelConsent(Girl,FCString::Strcmp(What,TEXT("intimacy"))==0?TEXT("intimacy"):TEXT("rest"));
 const bool Yes=P->GetBoolField(TEXT("yes"));
 UE_LOG(LogTemp,Display,TEXT("Street alone consent: actor=%d %s stage=%s favor=%.0f chastity=%.0f charm=%.0f resistance=%.0f chance=%.2f -> %d"),Girl,What,*R.Stage,R.Favor,P->GetNumberField(TEXT("chastity")),PlayerLook(),P->GetNumberField(TEXT("resistance")),Chance,int32(Yes));
 if(Yes){S.MindRecent.Add(Id,OKT(TEXT("ui.heroine.he_wants_alone_accepted")));return true;}
 const FString Rule=P->GetStringField(TEXT("why"));const FString Why=Rule.IsEmpty()?FString(OKT(TEXT("ui.heroine.she_declined_this_time"))):Rule;
 HeroineNote(Girl,TEXT("invite_refused"),OKT(TEXT("ui.heroine.he_wants_alone_i_declined"))+Why+OKT(TEXT("ui.finance_apps.close_paren")),PlaceHere(S.Male),0.4f);
 S.MindRecent.Add(Id,OKT(TEXT("ui.heroine.he_wants_alone_declined"))+Why+OKT(TEXT("ui.finance_apps.close_paren")));
 S.Message=OKMsg(TEXT("ui.heroine.refused_reason"), {{TEXT("name"), *S.Manifest.Characters[Girl].Name}, {TEXT("reason"), *Why}});S.FreeTalkNotice=S.Message;RefreshUI();
 return false;
}

// Her panel (P5): what he can do with her right now. Acts come from the table: shown from the stage they need (the
// feelings they also need are Interact's to judge), face to face only when she is here, drinking only where food is.
void AOKStreetSocialScene::BuildHeroinePanel(const TSharedRef<SVerticalBox>& Panel,int32 Her){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Button=[](const FString& Label,TFunction<void()> Do,bool bEnabled=true){
  return SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(bEnabled).OnClicked_Lambda([Do](){Do();return FReply::Handled();})[SNew(STextBlock).Text(FText::FromString(Label))];};
 const FOKStreetRelation R=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Her]),S.Manifest.TalkBudget);
 const bool Here=!RemoteWith(Her);const FString Place=PlaceHere(S.Male);
 auto Row=SNew(SWrapBox).UseAllottedSize(true);
 // The bag: share it, feed her, or give it.
 TSet<FString> Seen;
 for(const FString& ItemId:S.Store.Inventory){if(Seen.Contains(ItemId)||!Here)continue;Seen.Add(ItemId);const auto* I=S.Catalog.Find(ItemId);if(!I)continue;
  if(I->Consumable&&!I->Giftable){Row->AddSlot().Padding(1)[Button(OKT(TEXT("ui.heroine.eat_together"))+I->Name,[Weak,ItemId](){if(Weak.IsValid())Weak->EatFromBag(ItemId,false);})];Row->AddSlot().Padding(1)[Button(OKT(TEXT("ui.heroine.feed_her"))+I->Name,[Weak,ItemId](){if(Weak.IsValid())Weak->EatFromBag(ItemId,true);})];}
  else Row->AddSlot().Padding(1)[Button(OKT(TEXT("ui.heroine.gift_her"))+I->Name,[Weak,ItemId](){if(Weak.IsValid())Weak->StartGift(ItemId);})];}
 // What he borrowed from her: give it back (items.rs items_view).
 if(Here){auto Request=OKRulesRequest(TEXT("items_view"));OKRulesPutExt(S,Request);
  if(const TSharedPtr<FJsonObject> Bag=OKRulesEval(S,Request)){const FString HerId=OKStreetStableId(S.Manifest.Characters[Her]);
   for(const auto& V:Bag->GetArrayField(TEXT("borrowed"))){const auto O=V->AsObject();if(O->GetStringField(TEXT("owner"))!=HerId)continue;const FString ItemId=O->GetStringField(TEXT("item"));
    Row->AddSlot().Padding(1)[Button(OKMsg(TEXT("ui.heroine.return_her_item"),{{TEXT("item"),O->GetStringField(TEXT("name"))}}),[Weak,Her,ItemId](){if(Weak.IsValid())Weak->ReturnBorrowed(Her,ItemId);})];}}}
 if(S.OutingGirl==Her)for(const auto& Pair:TArray<TPair<int32,FString>>{{0,OKT(TEXT("ui.heroine.you_lead"))},{1,OKT(TEXT("ui.heroine.she_leads"))},{2,OKT(TEXT("ui.heroine.walk_side_by_side"))}}){const int32 Mode=Pair.Key;
  Row->AddSlot().Padding(1)[Button(Pair.Value,[Weak,Mode](){if(Weak.IsValid())Weak->SetFollowMode(Mode);},S.FollowMode!=Mode)];}
 if(Here)Row->AddSlot().Padding(1)[Button(OKT(TEXT("ui.heroine.more_intimate")),[Weak,Her](){if(Weak.IsValid())Weak->RequestIntimacy(Her,false);})];
 for(const FOKHeroineAct& A:OKHeroineActs()){
  if(A.Button.IsEmpty()||OKHeroineStageIndex(R.Stage)<A.Stage||(A.FaceToFace&&!Here))continue;
  if(A.Id==TEXT("alcohol")&&!OKStreetNormsOf(Place).FoodAllowed)continue;
  const FString Act=A.Id;Row->AddSlot().Padding(1)[Button(A.Button,[Weak,Her,Act](){if(Weak.IsValid())Weak->DoAct(Her,Act);})];
 }
 Row->AddSlot().Padding(1)[Button(OKT(TEXT("ui.heroine.lend_her_200")),[Weak](){if(Weak.IsValid())Weak->StartLoan(int32(Weak->Local(TEXT("cash"),200)));})];
 for(const auto& C:S.Choices)if(C.Heroine==Her&&C.Kind!=TEXT("checkout")){const FString ChoiceId=C.Id;
  Row->AddSlot().Padding(1)[Button(C.Label+OKT(TEXT("ui.heroine.okay_suffix")),[Weak,ChoiceId](){if(Weak.IsValid())Weak->ResolveChoice(ChoiceId,TEXT("accept"),0.7f);})];
  // A no of his own as a button too (her wish to be close had only "okay": no was words or waiting it out; R3-228).
  if(C.Options.Contains(TEXT("decline")))Row->AddSlot().Padding(1)[Button(C.Label+OKT(TEXT("ui.heroine.decline_suffix")),[Weak,ChoiceId](){if(Weak.IsValid())Weak->ResolveChoice(ChoiceId,TEXT("decline"),0.7f);})];}
 Panel->AddSlot().AutoHeight()[Row];
}

FOKHeroineResult AOKStreetSocialScene::Interact(int32 Girl,const FString& ActId,const FOKHeroineArgs& Args){
 FOKHeroineResult Out;
 if(!State||!OKHeroineFind(ActId)){UE_LOG(LogTemp,Warning,TEXT("Street heroine: unknown interaction %s"),*ActId);return Out;}
 Out.Known=true;
 if(!State->Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return Out;auto& S=*State;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);
 const FString Here=PlaceHere(S.Male);const auto* HerePlace=Here.IsEmpty()?nullptr:OKStreetFindPlace(Here);const FDateTime Now=StreetNow();
 auto Request=OKRulesHeroineRequest(S,TEXT("heroine_act"),Id);
 Request->SetStringField(TEXT("act"),ActId);Request->SetBoolField(TEXT("hers"),Args.Hers);Request->SetBoolField(TEXT("record_only"),Args.RecordOnly);
 Request->SetStringField(TEXT("detail"),Args.Detail);Request->SetStringField(TEXT("rumor_detail"),Args.RumorDetail);Request->SetNumberField(TEXT("importance"),Args.Importance);
 Request->SetStringField(TEXT("name"),S.Manifest.Characters[Girl].Name);
 Request->SetBoolField(TEXT("remote"),!Args.RecordOnly&&RemoteWith(Girl));Request->SetBoolField(TEXT("penalty_ready"),SceneTime()-S.GatePenaltyAt>20.0);
 Request->SetStringField(TEXT("place"),Here);Request->SetStringField(TEXT("place_name"),HerePlace?HerePlace->Name:OKT(TEXT("ui.etiquette.street_location")));Request->SetNumberField(TEXT("hours"),Now.GetHour()+Now.GetMinute()/60.0);
 TSharedPtr<FJsonObject> A=OKRulesEval(S,Request);if(!A.IsValid())return Out;
 // Settled elsewhere: only the record (and the rumour).
 if(Args.RecordOnly){HeroineApplyRecord(Girl,*A,Args.Place);Out.Done=true;return Out;}
 // 2. Does she agree: his invitation goes through her invitation rule, then the rules finish the steps.
 FString Consent;
 if(A->TryGetStringField(TEXT("consent"),Consent)){
  if(!PanelConsent(Girl,Consent)){Out.Refused=true;Out.Why=TEXT("consent");return Out;}
  Request->SetBoolField(TEXT("consented"),true);A=OKRulesEval(S,Request);if(!A.IsValid())return Out;
 }
 A->TryGetStringField(TEXT("why"),Out.Why);
 // 1. Over the phone a touch stays words; below the relationship it does not happen.
 if(Out.Why==TEXT("remote")){Out.Done=true;return Out;}
 bool Refused=false;
 if(A->TryGetBoolField(TEXT("refused"),Refused)&&Refused){
  Out.Refused=true;
  if(A->HasField(TEXT("gate")))HeroineRefuseGate(Girl,ActId,*A);
  else UE_LOG(LogTemp,Display,TEXT("Street heroine gate: actor=%d %s not done (her idea, relationship too new)"),Girl,*ActId);
  return Out;
 }
 // 3. How she took it here (the place's norms and the crowd against her shyness) and 4. what it did to her values.
 double Reaction=0;A->TryGetNumberField(TEXT("reaction"),Reaction);Out.Reaction=int32(Reaction);
 FString Recent;
 if(A->TryGetStringField(TEXT("recent"),Recent)){
  S.MindRecent.Add(Id,Recent);S.FreeTalkNotice=Recent;ApplyRulesRelation(Id,*A);
  UE_LOG(LogTemp,Display,TEXT("Street act: actor=%d act=%s place=%s awkward=%.2f reaction=%d staff=%d"),Girl,*ActId,*Here,A->GetNumberField(TEXT("awkward")),Out.Reaction,A->GetBoolField(TEXT("staff"))?1:0);
 }
 // 5. Her record (an awkward scene is a memory of hers) and 6. what others hear.
 HeroineApplyRecord(Girl,*A,Args.Place);
 const TSharedPtr<FJsonObject>* Awkward=nullptr;
 if(A->TryGetObjectField(TEXT("awkward_memory"),Awkward))MindObserve(Girl,TEXT("awkward_scene"),(*Awkward)->GetStringField(TEXT("text")),Here,float((*Awkward)->GetNumberField(TEXT("importance"))));
 // 7. What it looks like.
 if(A->GetBoolField(TEXT("play_act")))PlayLoverAct(Girl,ActId,Out.Reaction);
 Out.Done=A->GetBoolField(TEXT("done"));
 RefreshUI();
 return Out;
}
