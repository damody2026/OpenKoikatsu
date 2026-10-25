// Included by OKStreetSocialScene.cpp: plan §6.4 / §6.7 heroines among themselves — bonds, rumours, encounters
// (rules when he is away, a DeepSeek-written scene when he can overhear), drama modes A/B/C, being caught, the foil.

static FString OKStreetBondKey(const FString& A,const FString& B){return A<B?A+TEXT("|")+B:B+TEXT("|")+A;}

// Plan §5.7 drama modes and the foil are rules (Tools/ok-rules src/social.rs drama_cast); the scene keeps the answer and
// asks again when the profiles, the global mode or the rules change.
void AOKStreetSocialScene::RefreshDramaCast() const{
 auto& S=*State;const FString Key=FString::Printf(TEXT("%d|%c|%d"),S.MindProfiles.Num(),S.DramaMode,S.Rules?S.Rules->Generation():-1);if(Key==S.DramaCastKey)return;
 auto Request=OKRulesRequest(TEXT("drama_cast"));Request->SetStringField(TEXT("global_mode"),FString::Chr(S.DramaMode));
 TArray<TSharedPtr<FJsonValue>> Rows;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){const FString Id=OKStreetStableId(S.Manifest.Characters[I]);auto H=MakeShared<FJsonObject>();H->SetStringField(TEXT("id"),Id);if(const auto P=S.MindProfiles.FindRef(Id))H->SetObjectField(TEXT("profile"),P.ToSharedRef());Rows.Add(MakeShared<FJsonValueObject>(H));}
 Request->SetArrayField(TEXT("heroines"),Rows);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 S.DramaCastKey=Key;S.DramaModes.Reset();for(const auto& P:Answer->GetObjectField(TEXT("modes"))->Values){const FString M=P.Value->AsString();S.DramaModes.Add(FString(*P.Key),M.IsEmpty()?S.DramaMode:M[0]);}
 S.DramaFoil=Answer->GetStringField(TEXT("foil"));
}

TCHAR AOKStreetSocialScene::DramaModeOf(int32 I) const{
 RefreshDramaCast();const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(I))return S.DramaMode;
 const TCHAR* M=S.DramaModes.Find(OKStreetStableId(S.Manifest.Characters[I]));return M?*M:S.DramaMode;
}

int32 AOKStreetSocialScene::FoilOf() const{
 RefreshDramaCast();return State->DramaFoil.IsEmpty()?INDEX_NONE:CharacterOf(State->DramaFoil);
}

// A heroine as the social rules see a listener: record, profile, mode, whether she is the foil.
TSharedRef<FJsonObject> AOKStreetSocialScene::RulesListener(int32 I) const{
 const auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[I]);auto L=MakeShared<FJsonObject>();
 L->SetStringField(TEXT("id"),Id);L->SetStringField(TEXT("name"),S.Manifest.Characters[I].Name);L->SetNumberField(TEXT("actor"),I);
 L->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget)));
 if(const auto P=S.MindProfiles.FindRef(Id))L->SetObjectField(TEXT("profile"),P.ToSharedRef());
 L->SetStringField(TEXT("mode"),FString::Chr(DramaModeOf(I)));L->SetBoolField(TEXT("foil"),FoilOf()==I);return L;
}

// Heroine I can see him: within 25 m, on the same side of the walls (both in the restaurant, or both in the street)
// and on the same floor. Walking past the restaurant was "saw it with my own eyes" of him inside with Jeanne.
bool AOKStreetSocialScene::SeesHim(int32 I) const{
 const auto& S=*State;if(!S.Agents.IsValidIndex(I-1))return false;const FVector& P=S.Agents[I-1].Position;
 if(FVector::Dist2D(P,S.Male)>=2500.f||FMath::Abs(P.Z-S.Male.Z)>=150.f)return false;
 // Out in the open (the street, the park) both can see across; a wall stands between only when one is indoors.
 const FString Hers=PlaceHere(P),His=PlaceHere(S.Male);if(Hers==His)return true;
 auto Indoors=[](const FString& Id){const FOKStreetPlace* Pl=Id.IsEmpty()?nullptr:OKStreetFindPlace(Id);return Pl&&!Pl->Rooms.IsEmpty();};
 return !Indoors(Hers)&&!Indoors(His);
}

// A rumour about him (and maybe a heroine): who knows it, who saw it and grew jealous, which old ones fall away are rules
// (social.rs rumor_make); here only who stood within 25 m.
void AOKStreetSocialScene::MakeRumor(int32 About,const FString& Kind,const FString& Text,float Heat,bool bSecret){
 auto& S=*State;const bool HasAbout=S.Manifest.Characters.IsValidIndex(About)&&About>0;
 auto Request=OKRulesRequest(TEXT("rumor_make"));
 Request->SetStringField(TEXT("about"),HasAbout?OKStreetStableId(S.Manifest.Characters[About]):FString());Request->SetStringField(TEXT("about_mode"),HasAbout?FString::Chr(DramaModeOf(About)):FString());
 Request->SetStringField(TEXT("global_mode"),FString::Chr(S.DramaMode));Request->SetStringField(TEXT("kind"),Kind);Request->SetStringField(TEXT("text"),Text);Request->SetNumberField(TEXT("heat"),Heat);
 Request->SetBoolField(TEXT("secret"),bSecret);Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);Request->SetStringField(TEXT("place"),PlaceHere(S.Male));
 TArray<TSharedPtr<FJsonValue>> Near;for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(I!=About&&SeesHim(I))Near.Add(MakeShared<FJsonValueObject>(RulesListener(I)));
 Request->SetArrayField(TEXT("near"),Near);
 TArray<TSharedPtr<FJsonValue>> Old;for(const auto& R:S.Store.Rumors){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),R.Id);O->SetNumberField(TEXT("heat"),R.Heat);O->SetNumberField(TEXT("day"),R.Day);Old.Add(MakeShared<FJsonValueObject>(O));}
 Request->SetArrayField(TEXT("rumors"),Old);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer||Answer->GetBoolField(TEXT("skip")))return;
 auto Candidate=S.Store;
 const TSharedPtr<FJsonObject>* New=nullptr;if(Answer->TryGetObjectField(TEXT("rumor"),New)){FOKStreetRumor R;OKRulesReadRumor(**New,R);R.Id=Candidate.NextRumor++;Candidate.Rumors.Add(R);}
 TSet<int32> Remove;for(const auto& V:Answer->GetArrayField(TEXT("remove")))Remove.Add(int32(V->AsNumber()));
 Candidate.Rumors.RemoveAll([&](const FOKStreetRumor& X){return Remove.Contains(X.Id);});
 ApplyRulesEffects(Candidate,*Answer);
 CommitStore(MoveTemp(Candidate));
}

// Hearing that he was with another heroine: jealousy toward her (social.rs jealousy_news).
void AOKStreetSocialScene::ApplyJealousyNews(int32 Listener,const FString& AboutId,float Confidence){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Listener)||Listener<=0)return;
 const TSharedRef<FJsonObject> Request=RulesListener(Listener);Request->SetStringField(TEXT("rule"),TEXT("jealousy_news"));Request->SetStringField(TEXT("about"),AboutId);Request->SetNumberField(TEXT("confidence"),Confidence);
 if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request))ApplyRulesRelation(OKStreetStableId(S.Manifest.Characters[Listener]),*Answer);
}

// The heroine's own lines for the prompt (plan §7 [聽說]): the hottest rumours she knows, with how sure she is.
FString AOKStreetSocialScene::HeardText(int32 Character) const{
 const auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 TArray<const FOKStreetRumor*> Known;for(const auto& R:S.Store.Rumors)if(R.KnownBy.Contains(Id)&&R.About!=Id)Known.Add(&R);
 Known.Sort([](const FOKStreetRumor& A,const FOKStreetRumor& B){return A.Heat>B.Heat;});
 // When she heard it happened: without it "today at the dorm" came out as "yesterday you went to the dorm with Megumin".
 TArray<FString> Lines;for(int32 N=0;N<Known.Num()&&N<3;++N)Lines.Add(OKMsg(TEXT("ui.encounter.heard_item"), {{TEXT("days"), FMath::Max(0,S.Store.CalendarDay-Known[N]->Day)}, {TEXT("text"), *Known[N]->Text}, {TEXT("sure"), Known[N]->SureOf(Id)>0.8f?OKT(TEXT("ui.encounter.confirm_button")):Known[N]->SureOf(Id)>0.5f?OKT(TEXT("ui.encounter.heard_label")):OKT(TEXT("ui.encounter.unsure_option"))}}));
 return FString::Join(Lines,OKT(TEXT("ui.economy.semicolon_mark")));
}

// Two heroines meet (plan §6.4). The rules decide what changes (social.rs encounter); the words exist only when he can
// overhear.
void AOKStreetSocialScene::Encounter(int32 A,int32 B,bool bOverheard){
 auto& S=*State;const FString IdA=OKStreetStableId(S.Manifest.Characters[A]),IdB=OKStreetStableId(S.Manifest.Characters[B]);
 const FString Key=OKStreetBondKey(IdA,IdB);const FString Place=PlaceHere(S.Agents[A-1].Position);
 auto Request=OKRulesRequest(TEXT("encounter"));{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EncounterListeners);Request->SetObjectField(TEXT("a"),RulesListener(A));Request->SetObjectField(TEXT("b"),RulesListener(B));}
 Request->SetNumberField(TEXT("heat"),S.Store.BondHeat.FindRef(Key));Request->SetStringField(TEXT("place"),Place);Request->SetBoolField(TEXT("food_place"),!Place.IsEmpty()&&OKStreetNormsOf(Place).FoodAllowed);
 Request->SetBoolField(TEXT("overheard"),bOverheard);Request->SetNumberField(TEXT("seed"),FMath::Rand());Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EncounterRumors);TArray<TSharedPtr<FJsonValue>> Rumors;for(const auto& R:S.Store.Rumors)Rumors.Add(MakeShared<FJsonValueObject>(OKRulesRumorJson(R)));Request->SetArrayField(TEXT("rumors"),Rumors);}
 TSharedPtr<FJsonObject> Answer;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EncounterEval);Answer=OKRulesEval(S,Request);}if(!Answer)return;
 QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EncounterApply);
 // Changed in place and saved by the save task: a copy of the whole store to change and another to commit held the
 // frame ~5 ms for each pair that met (nothing here can fail half-way).
 auto& Candidate=S.Store;Candidate.BondHeat.Add(Key,float(Answer->GetNumberField(TEXT("heat"))));
 for(const auto& V:Answer->GetArrayField(TEXT("rumors"))){const auto O=V->AsObject();const int32 Id=int32(O->GetNumberField(TEXT("id")));if(auto* R=Candidate.Rumors.FindByPredicate([&](const FOKStreetRumor& X){return X.Id==Id;}))OKRulesReadRumor(*O,*R);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EncounterEffects);ApplyRulesEffects(Candidate,*Answer);}
 if(!S.SelfTest&&!S.Manifest.SavePath.IsEmpty())OKStreetAtomicSaveStoreAsync(S.Manifest.SavePath,S.Store);
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EncounterFlush);FlushRulesEffects();}
 UE_LOG(LogTemp,Display,TEXT("Street encounter %d-%d heat=%.0f learned=%d overheard=%d"),A,B,Answer->GetNumberField(TEXT("heat")),int32(Answer->GetNumberField(TEXT("learned"))),bOverheard?1:0);
}

// Plan §7.0 rule 3: news for her context, appended after her last turn.
void AOKStreetSocialScene::MindNote(int32 Character,const FString& Text){
 auto& S=*State;if(!S.Mind||!S.Manifest.Characters.IsValidIndex(Character))return;
 TSharedRef<FJsonObject> Job=MakeShared<FJsonObject>();Job->SetStringField(TEXT("kind"),TEXT("note"));Job->SetStringField(TEXT("heroine"),OKStreetStableId(S.Manifest.Characters[Character]));Job->SetStringField(TEXT("text"),Text);
 FString Error;S.Mind->Submit(OKStreetMindJson(Job),Error);
}

// A scene he can overhear (plan §6.4): one DeepSeek call writes 2-6 lines, both voices speak them.
void AOKStreetSocialScene::RequestOverheard(int32 A,int32 B){
 auto& S=*State;if(!S.Mind)return;QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_RequestOverheard);
 auto Persona=[&](int32 I){FOKFreeTalkPersona P;FString E;OKStreetFreeTalkLoadPersona(S.Manifest.Characters[I].Name,P,E);return S.Manifest.Characters[I].Name+OKT(TEXT("ui.economy.colon_mark"))+P.Profile.Left(300);};
 const FString IdA=OKStreetStableId(S.Manifest.Characters[A]),IdB=OKStreetStableId(S.Manifest.Characters[B]);
 const FOKStreetRelation RA=OKStreetViewRelation(S.Store,IdA,S.Manifest.TalkBudget),RB=OKStreetViewRelation(S.Store,IdB,S.Manifest.TalkBudget);
 const FString User=OKMsg(TEXT("ui.encounter.pair_context_summary"), {{TEXT("name_a"), *Persona(A)}, {TEXT("name_b"), *Persona(B)}, {TEXT("bond_score"), S.Store.BondHeat.FindRef(OKStreetBondKey(IdA,IdB))}, {TEXT("jealousy_a_to_b"), RA.JealousyToward.FindRef(IdB)}, {TEXT("jealousy_b_to_a"), RB.JealousyToward.FindRef(IdA)}, {TEXT("heard_a"), *HeardText(A)}, {TEXT("heard_b"), *HeardText(B)}, {TEXT("scene_line"), *SceneLine(A)}});
 TSharedRef<FJsonObject> Job=MakeShared<FJsonObject>();Job->SetStringField(TEXT("kind"),TEXT("chat"));Job->SetStringField(TEXT("priority"),TEXT("overheard"));
 Job->SetStringField(TEXT("tag"),FString::Printf(TEXT("overheard:%d:%d"),A,B));Job->SetNumberField(TEXT("max_tokens"),500);Job->SetNumberField(TEXT("temperature"),1.1);
 TArray<TSharedPtr<FJsonValue>> Messages;
 auto Add=[&](const TCHAR* Speaker,const FString& Content){auto M=MakeShared<FJsonObject>();M->SetStringField(TEXT("role"),Speaker);M->SetStringField(TEXT("content"),Content);Messages.Add(MakeShared<FJsonValueObject>(M));};
 Add(TEXT("system"),OKT(TEXT("ui.encounter.encounter_dialogue_prompt")));
 Add(TEXT("user"),User);
 Job->SetArrayField(TEXT("messages"),Messages);
 FString Error;if(!S.Mind->Submit(OKStreetMindJson(Job),Error))UE_LOG(LogTemp,Warning,TEXT("Street overheard request failed: %s"),*Error);
}

void AOKStreetSocialScene::HandleOverheard(const TSharedPtr<FJsonObject>& R,int32 A,int32 B){
 auto& S=*State;bool Ok=false;R->TryGetBoolField(TEXT("ok"),Ok);FString Content;
 TSharedPtr<FJsonObject> Body;
 if(!Ok||!R->TryGetStringField(TEXT("content"),Content)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content),Body)||!Body.IsValid()){Encounter(A,B,false);return;}
 const TArray<TSharedPtr<FJsonValue>>* Lines=nullptr;if(!Body->TryGetArrayField(TEXT("lines"),Lines)){Encounter(A,B,false);return;}
 TArray<FString> LinesA,LinesB,Transcript;
 for(const auto& V:*Lines){const auto O=V->AsObject();if(!O)continue;FString Who,Text;O->TryGetStringField(TEXT("who"),Who);O->TryGetStringField(TEXT("text"),Text);if(Text.IsEmpty())continue;
  const int32 Speaker=Who==TEXT("B")?B:A;FreeTalkAddLine(Speaker,false,Text);(Speaker==A?LinesA:LinesB).Add(Text);Transcript.Add(S.Manifest.Characters[Speaker].Name+OKT(TEXT("ui.economy.colon_mark"))+Text);}
 if(Transcript.IsEmpty()){Encounter(A,B,false);return;}
 // He started talking with someone meanwhile: their chat is only remembered, not shown or spoken over hers.
 const bool Busy=FreeTalkPartner()!=INDEX_NONE||S.Target!=INDEX_NONE;
 if(!Busy)S.FreeTalkCaptionHer=Transcript.Last();
 if(!Busy&&!S.TtsCacheDir.IsEmpty()){if(!LinesA.IsEmpty())SpeakFreeTalk(A,LinesA,++S.FreeTalkSerial);if(!LinesB.IsEmpty())SpeakFreeTalk(B,LinesB,++S.FreeTalkSerial);}
 const FString Joined=FString::Join(Transcript,OKT(TEXT("ui.encounter.slash_separator")));
 MindNote(A,OKMsg(TEXT("ui.encounter.chatted_on_street"), {{TEXT("partner_name"), *S.Manifest.Characters[B].Name}, {TEXT("topic"), *Joined}}));MindNote(B,OKMsg(TEXT("ui.encounter.chatted_on_street"), {{TEXT("partner_name"), *S.Manifest.Characters[A].Name}, {TEXT("topic"), *Joined}}));
 Encounter(A,B,true);RefreshUI(OKStreetUi::Bottom|OKStreetUi::History); // their lines: the caption and the log, not the whole panel
}

// Every ten game minutes: pairs in the same place (or within 4 m on the street) may meet, once a pair a day; in mode C a
// jealous heroine near him may catch him with another one. Who stands where is here; the rules decide (encounters_tick).
void AOKStreetSocialScene::TickEncounters(){
 auto& S=*State;if(S.SelfTest||!S.Mind)return;
 // The pairs that met play out one a frame: all of a round in one frame (gossip, memories, history, the rules for each)
 // stalled the street 60-120 ms every ten game minutes.
 if(!S.PendingEncounters.IsEmpty()){const FIntVector P=S.PendingEncounters[0];S.PendingEncounters.RemoveAt(0);
  if(S.Agents.IsValidIndex(P.X-1)&&S.Agents.IsValidIndex(P.Y-1)){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EncounterPair);if(P.Z)RequestOverheard(P.X,P.Y);else Encounter(P.X,P.Y,false);}}
 const int64 Now=OKStreetMindNow(S);if(Now-S.EncounterTick<10||!S.PendingEncounters.IsEmpty())return;S.EncounterTick=Now;
 const int32 With=S.Target!=INDEX_NONE?S.Target:S.Companion!=INDEX_NONE?S.Companion:S.OutingGirl;
 auto Request=OKRulesRequest(TEXT("encounters_tick"));Request->SetNumberField(TEXT("seed"),FMath::Rand());Request->SetStringField(TEXT("place"),PlaceHere(S.Male));
 Request->SetStringField(TEXT("with"),With>0&&S.Manifest.Characters.IsValidIndex(With)?OKStreetStableId(S.Manifest.Characters[With]):FString());
 TArray<TSharedPtr<FJsonValue>> Pairs;TMap<FString,TPair<int32,int32>> Actors;
 // Where each of them is, once (not twice for each of the 45 pairs).
 TArray<FString> PlaceOf;PlaceOf.SetNum(S.Manifest.Characters.Num());for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(S.Agents.IsValidIndex(I-1))PlaceOf[I]=PlaceHere(S.Agents[I-1].Position);
 const FString HerePlace=PlaceHere(S.Male);
 for(int32 A=1;A<S.Manifest.Characters.Num();++A)for(int32 B=A+1;B<S.Manifest.Characters.Num();++B){
  if(!S.Agents.IsValidIndex(A-1)||!S.Agents.IsValidIndex(B-1))continue;
  const FString Pair=FString::Printf(TEXT("%d|%d"),A,B);
  auto P=MakeShared<FJsonObject>();
  for(const auto& Side:TArray<TPair<FString,int32>>{{TEXT("a"),A},{TEXT("b"),B}}){
   const int32 I=Side.Value;const FString& Name=Side.Key;
   P->SetStringField(Name,OKStreetStableId(S.Manifest.Characters[I]));P->SetStringField(Name+TEXT("_name"),S.Manifest.Characters[I].Name);
   P->SetStringField(Name+TEXT("_mode"),FString::Chr(DramaModeOf(I)));P->SetBoolField(Name+TEXT("_busy"),BusyWithPlayer(I));
   P->SetBoolField(Name+TEXT("_sees"),FVector::Dist2D(S.Agents[I-1].Position,S.Male)<1500&&FMath::Abs(S.Agents[I-1].Position.Z-S.Male.Z)<300.f);
  }
  const FString& PA=PlaceOf[A];const FString& PB=PlaceOf[B];
  P->SetBoolField(TEXT("together"),(!PA.IsEmpty()&&PA==PB)||(PA.IsEmpty()&&PB.IsEmpty()&&FVector::Dist2D(S.Agents[A-1].Position,S.Agents[B-1].Position)<400));
  // He hears them only on their floor, and not while he is in a conversation of his own (it would talk over her).
  const bool SameFloor=FMath::Abs(S.Agents[A-1].Position.Z-S.Male.Z)<300.f;
  P->SetBoolField(TEXT("near"),SameFloor&&FreeTalkPartner()==INDEX_NONE&&S.Target==INDEX_NONE&&(FVector::Dist2D(S.Agents[A-1].Position,S.Male)<2000||(!PA.IsEmpty()&&HerePlace==PA)));
  P->SetBoolField(TEXT("met_today"),S.EncounterDay.FindRef(Pair)==S.Store.CalendarDay+1);
  Actors.Add(P->GetStringField(TEXT("a"))+TEXT("|")+P->GetStringField(TEXT("b")),{A,B});
  Pairs.Add(MakeShared<FJsonValueObject>(P));
 }
 Request->SetArrayField(TEXT("pairs"),Pairs);
 TSharedPtr<FJsonObject> Answer;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EncountersEval);Answer=OKRulesEval(S,Request);}if(!Answer)return;
 QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_EncountersApply);
 for(const auto& V:Answer->GetArrayField(TEXT("caught"))){const auto O=V->AsObject();ApplyJealousyNews(CharacterOf(O->GetStringField(TEXT("seer"))),O->GetStringField(TEXT("other")),1.f);}
 ApplyRulesEffects(S.Store,*Answer);
 for(const auto& V:Answer->GetArrayField(TEXT("meet"))){
  const auto O=V->AsObject();const auto* AB=Actors.Find(O->GetStringField(TEXT("a"))+TEXT("|")+O->GetStringField(TEXT("b")));if(!AB)continue;
  S.EncounterDay.Add(FString::Printf(TEXT("%d|%d"),AB->Key,AB->Value),S.Store.CalendarDay+1);
  if(!O->GetBoolField(TEXT("met")))continue;
  bool Overheard=false;O->TryGetBoolField(TEXT("overheard"),Overheard);
  S.PendingEncounters.Add(FIntVector(AB->Key,AB->Value,Overheard?1:0));
 }
}
