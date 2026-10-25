// Included by OKStreetSocialScene.cpp: 吹牛 (Cheat), a card game for him and 2-5 heroines with him, indoors only.
// The deck is dealt out; in turn each puts 1-4 cards face down claiming they are all of this turn's rank (A, 2 ... K, then A
// again). Anyone may call 「吹牛！」: the cards are shown, a liar takes the whole pile, a wrong call makes the caller take it.
// Whoever plays her last cards and is not caught wins.
// The game is a rule (ok-rules bluff.rs bluff_step): every action (deal, a play, a call, the window closing, his words) goes
// in with the table and the answer is the new table, the lines to say, the log and who will call when. Here: the clock,
// the voices, the table on screen and his hand of cards in front of him. When it ends they remember it (games.rs bluff_end).

// Say a line at the table: hers in her own voice (TTS) with the caption; his as a caption (there is no voice for him).
void AOKStreetSocialScene::BluffSay(const TArray<int32>& Seats,const TArray<FString>& Lines){
 auto& S=*State;auto& B=S.Bluff;TArray<int32> Speakers;TArray<FString> Spoken,Caption;
 for(int32 I=0;I<Seats.Num()&&I<Lines.Num();++I){
  const int32 G=B.Seats.IsValidIndex(Seats[I])?B.Seats[Seats[I]]:0;
  if(G==0){S.FreeTalkCaptionPlayer=T(TEXT("ui.common.you"))+T(TEXT("ui.common.colon"))+T(TEXT("ui.common.quote_open"))+Lines[I]+T(TEXT("ui.common.quote_close"));continue;}
  Speakers.Add(G);Spoken.Add(Lines[I]);Caption.Add(S.Manifest.Characters[G].Name+T(TEXT("ui.common.colon"))+Lines[I]);
 }
 if(Speakers.IsEmpty())return;
 S.FreeTalkCaptionHer=FString::Join(Caption,TEXT("\n"));
 if(!S.TtsCacheDir.IsEmpty()){SpeakPartyLines(Speakers,Spoken);B.SpeakUntil=SceneTime()+3.0+2.5*Spoken.Num();}
}

// Still speaking: the table waits for the line (at most a few seconds, in case a voice never comes).
bool AOKStreetSocialScene::BluffSpeaking() const{
 const auto& S=*State;const auto& B=S.Bluff;if(SceneTime()>=B.SpeakUntil)return false;
 if(S.FreeTalkSpeechPlayed<S.FreeTalkSpeechLines.Num())return true;
 for(int32 K=1;K<B.Seats.Num();++K)if(OKCrowdRuntimeSpeechActive(const_cast<AOKStreetSocialScene*>(this),B.Seats[K]))return true;
 return false;
}

namespace {
TArray<int32> OKBluffCards(const TSharedPtr<FJsonObject>& O,const TCHAR* Field){
 TArray<int32> Out;const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(O.IsValid()&&O->TryGetArrayField(Field,A))for(const auto& V:*A)Out.Add(int32(V->AsNumber()));return Out;
}
TArray<TSharedPtr<FJsonValue>> OKBluffJsonCards(const TArray<int32>& Cards){TArray<TSharedPtr<FJsonValue>> A;for(const int32 C:Cards)A.Add(MakeShared<FJsonValueNumber>(C));return A;}
}

// One action through the rule; on success the table, the log and the voices follow its answer. Returns the answer (null:
// refused or no rules), with the reason on the notice line when refused.
TSharedPtr<FJsonObject> AOKStreetSocialScene::BluffStep(const TSharedRef<FJsonObject>& Action){
 auto& S=*State;auto& B=S.Bluff;
 auto Request=OKRulesRequest(TEXT("bluff_step"));Request->SetObjectField(TEXT("action"),Action);Request->SetNumberField(TEXT("seed"),FMath::Rand());
 if(B.Table.IsValid())Request->SetObjectField(TEXT("table"),B.Table);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);
 if(!Answer.IsValid())return nullptr;
 if(!Answer->GetBoolField(TEXT("ok"))){
  FString Why;Answer->TryGetStringField(TEXT("why"),Why);
  UE_LOG(LogTemp,Display,TEXT("Street bluff: %s refused: %s"),*Action->GetStringField(TEXT("kind")),*Why);
  if(!Why.IsEmpty()){S.FreeTalkNotice=Why;RefreshUI();}
  return nullptr;
 }
 if(!Answer->GetBoolField(TEXT("handled")))return Answer;
 B.Table=Answer->GetObjectField(TEXT("table"));const auto& T=B.Table;
 Answer->TryGetNumberField(TEXT("clock_rate"),B.ClockRate);
 const TArray<TSharedPtr<FJsonValue>>& Hands=T->GetArrayField(TEXT("hands"));B.Hands.SetNum(Hands.Num());
 for(int32 K=0;K<Hands.Num();++K){B.Hands[K].Reset();for(const auto& V:Hands[K]->AsArray())B.Hands[K].Add(int32(V->AsNumber()));}
 B.PileCount=T->GetArrayField(TEXT("pile")).Num();B.Turn=int32(T->GetNumberField(TEXT("turn")));B.Rank=int32(T->GetNumberField(TEXT("rank")));
 B.LastSeat=int32(T->GetNumberField(TEXT("last_seat")));B.LastRank=int32(T->GetNumberField(TEXT("last_rank")));B.LastCount=T->GetArrayField(TEXT("last_cards")).Num();
 B.Caught=OKBluffCards(T,TEXT("caught"));B.Rounds=int32(T->GetNumberField(TEXT("rounds")));
 B.RoundClaimed=0;{const TArray<TSharedPtr<FJsonValue>>* Round=nullptr;if(T->TryGetArrayField(TEXT("round"),Round))for(const auto& P:*Round){const auto& Row=P->AsArray();if(Row.Num()>1)B.RoundClaimed+=int32(Row[1]->AsNumber());}}
 const bool WasWindow=B.Window;B.Window=T->GetBoolField(TEXT("window"));B.Open=T->GetBoolField(TEXT("open"));
 if(B.Rank>=0)B.ClaimRank=B.Rank;
 B.Selected=B.Selected.Intersect(TSet<int32>(B.Hands.IsValidIndex(0)?B.Hands[0]:TArray<int32>()));
 for(const auto& V:Answer->GetArrayField(TEXT("log"))){B.Log.Add(V->AsString());S.Message=B.Log.Last();UE_LOG(LogTemp,Display,TEXT("Street bluff: %s"),*B.Log.Last());}
 TArray<int32> Seats;TArray<FString> Lines;
 for(const auto& V:Answer->GetArrayField(TEXT("say"))){const auto O=V->AsObject();Seats.Add(int32(O->GetNumberField(TEXT("seat"))));Lines.Add(O->GetStringField(TEXT("line")));}
 if(!B.Intro)BluffSay(Seats,Lines);
 // A play went down: the window opens once the claim has been said (TickBluff), so the call times are offsets for now.
 if(B.Window&&!WasWindow){
  B.WindowUntil=0;B.WindowLength=Answer->GetNumberField(TEXT("window_length"));B.ChallengeAt.Reset();if(B.LastSeat==0)B.Selected.Reset(); // his picks for later stay while the others play
  for(const auto& V:Answer->GetArrayField(TEXT("calls"))){const auto O=V->AsObject();B.ChallengeAt.Add(int32(O->GetNumberField(TEXT("seat"))),O->GetNumberField(TEXT("at")));}
 }
 if(!B.Window)B.ChallengeAt.Reset();
 if(Answer->GetBoolField(TEXT("next_turn")))B.NextAt=SceneTime()+FMath::FRandRange(1.2f,2.2f);
 const int32 Winner=int32(Answer->GetNumberField(TEXT("winner")));
 if(Winner>=0)EndBluff(FString(),Winner);
 RefreshUI();
 return Answer;
}

// His words at the table (voice or typed): the rule reads a call ("吹牛／騙人／不信／翻開") or a count ("兩張": his picked
// cards, or the real ones first). Anything else is table talk for the group.
bool AOKStreetSocialScene::BluffHear(const FString& Text){
 auto& S=*State;auto& B=S.Bluff;if(!B.Active||B.Intro)return false;
 auto Action=MakeShared<FJsonObject>();Action->SetStringField(TEXT("kind"),TEXT("hear"));Action->SetStringField(TEXT("text"),Text);Action->SetArrayField(TEXT("selected"),OKBluffJsonCards(B.Selected.Array()));
 if(B.ClaimRank>=0)Action->SetNumberField(TEXT("claim"),B.ClaimRank);
 const TSharedPtr<FJsonObject> Answer=BluffStep(Action);
 const bool Handled=Answer.IsValid()&&Answer->GetBoolField(TEXT("handled"));
 if(Handled)UE_LOG(LogTemp,Display,TEXT("Street bluff: he says \"%s\" at the table"),*Text);
 return Handled;
}

TArray<int32> AOKStreetSocialScene::BluffGroup() const{
 const auto& S=*State;TArray<int32> Group;
 if(FreeTalkPartner()!=INDEX_NONE)Group.Add(FreeTalkPartner());
 for(const int32 G:S.Party)Group.AddUnique(G);
 return Group;
}

bool AOKStreetSocialScene::BluffCanStart(FString* Why) const{
 const auto& S=*State;auto No=[&](const TCHAR* W){if(Why)*Why=W;return false;};
 if(S.Bluff.Active)return No(*T(TEXT("ui.bluff.already")));
 const TArray<int32> Group=BluffGroup();
 if(Group.Num()<2)return No(*T(TEXT("ui.bluff.need_players")));
 if(Group.Num()>5)return No(*T(TEXT("ui.bluff.too_many")));
 bool Indoors=S.Facility!=INDEX_NONE;
 if(!Indoors){const FString P=PlaceHere(S.Male);if(const FOKStreetPlace* Pl=P.IsEmpty()?nullptr:OKStreetFindPlace(P))Indoors=OKStreetInsidePlace(*Pl,S.Male);}
 if(!Indoors)return No(*T(TEXT("ui.bluff.indoors_only")));
 for(const int32 G:Group)if(!S.Agents.IsValidIndex(G-1)||FMath::Abs(S.Agents[G-1].Position.Z-S.Male.Z)>150.f||FVector::Dist2D(S.Agents[G-1].Position,S.Male)>500.f)return No(*T(TEXT("ui.bluff.gather_first")));
 if(S.Pair.Partner!=INDEX_NONE||S.PrivateGirl!=INDEX_NONE||S.OutingGirl!=INDEX_NONE)return No(*T(TEXT("ui.bluff.finish_activity")));
 return true;
}

void AOKStreetSocialScene::StartBluff(){
 auto& S=*State;FString Why;
 if(!BluffCanStart(&Why)){S.FreeTalkNotice=Why;S.Message=Why;RefreshUI();UE_LOG(LogTemp,Display,TEXT("Street bluff: cannot start (%s)"),*Why);return;}
 auto& B=S.Bluff;B=FOKStreetBluff();
 B.Seats.Add(0);B.Seats.Append(BluffGroup());
 // Who is at the table, for the rule: each heroine's name, whether she is his lover, whom at the table she distrusts.
 TArray<TSharedPtr<FJsonValue>> Players;
 for(int32 K=0;K<B.Seats.Num();++K){const int32 G=B.Seats[K];auto O=MakeShared<FJsonObject>();
  O->SetStringField(TEXT("name"),G==0?T(TEXT("ui.common.you")):S.Manifest.Characters[G].Name);
  O->SetBoolField(TEXT("lover"),G>0&&OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[G]),S.Manifest.TalkBudget).Stage==TEXT("lover"));
  TArray<TSharedPtr<FJsonValue>> Distrusts;if(G>0)for(int32 J=1;J<B.Seats.Num();++J)if(J!=K&&BondOf(G,B.Seats[J])<0.f)Distrusts.Add(MakeShared<FJsonValueNumber>(J));
  O->SetArrayField(TEXT("distrusts"),Distrusts);Players.Add(MakeShared<FJsonValueObject>(O));}
 auto Action=MakeShared<FJsonObject>();Action->SetStringField(TEXT("kind"),TEXT("deal"));Action->SetArrayField(TEXT("players"),Players);
 B.Intro=true; // their opening lines: the group's reply in a talk, else the rule's fixed lines (below)
 const TSharedPtr<FJsonObject> Answer=BluffStep(Action);
 if(!Answer.IsValid()){B=FOKStreetBluff();return;}
 B.Active=true;B.NextAt=SceneTime()+2.0;B.Where=S.Male;B.Place=PlaceHere(S.Male);
 S.Message=T(TEXT("ui.bluff.started"));
 for(const int32 G:B.Seats)if(G>0){auto& A=S.Agents[G-1];A.Held=true;S.Rotations[G]=OKStreetFacingMovement(S.Male-A.Position,S.Rotations[G]);}
 B.IntroUntil=SceneTime()+25.0;B.SpeakUntil=B.IntroUntil; // their opening lines are waited for (BluffSpeaking)
 S.FreeTalkCaptionPlayer=T(TEXT("ui.common.you"))+T(TEXT("ui.common.colon"))+T(TEXT("ui.common.quote_open"))+T(TEXT("ui.bluff.proposal"))+T(TEXT("ui.common.quote_close"));
 if(FreeTalkPartner()!=INDEX_NONE&&!S.FreeTalkBusy){
  S.SystemNote=true;S.FreeTalkDraft=T(TEXT("ui.bluff.prompt_intro"));SendFreeTalk();
 }else{
  TArray<int32> Seats;TArray<FString> Lines;
  for(const auto& V:Answer->GetArrayField(TEXT("say"))){const auto O=V->AsObject();Seats.Add(int32(O->GetNumberField(TEXT("seat"))));Lines.Add(O->GetStringField(TEXT("line")));}
  BluffSay(Seats,Lines);
 }
 TArray<FString> Names;for(const int32 G:B.Seats)if(G>0)Names.Add(S.Manifest.Characters[G].Name);
 UE_LOG(LogTemp,Display,TEXT("Street bluff: start with %s (%d seats, first %d)"),*FString::Join(Names,TEXT("、")),B.Seats.Num(),B.Turn);
 RefreshUI();
}

FString AOKStreetSocialScene::BluffSeatName(int32 Seat) const{
 const auto& B=State->Bluff;if(!B.Seats.IsValidIndex(Seat))return FString();
 return B.Seats[Seat]==0?T(TEXT("ui.common.you")):State->Manifest.Characters[B.Seats[Seat]].Name;
}

void AOKStreetSocialScene::BluffPlay(int32 Seat,const TArray<int32>& Cards,int32 Claim){
 if(!State->Bluff.Active)return;
 auto Action=MakeShared<FJsonObject>();Action->SetStringField(TEXT("kind"),TEXT("play"));Action->SetNumberField(TEXT("seat"),Seat);Action->SetArrayField(TEXT("cards"),OKBluffJsonCards(Cards));
 if(Claim>=0)Action->SetNumberField(TEXT("claim"),Claim);
 BluffStep(Action);
}

void AOKStreetSocialScene::BluffPass(){
 if(!State->Bluff.Active)return;
 auto Action=MakeShared<FJsonObject>();Action->SetStringField(TEXT("kind"),TEXT("pass"));Action->SetNumberField(TEXT("seat"),0);
 BluffStep(Action);
}

void AOKStreetSocialScene::BluffChallenge(int32 Seat){
 if(!State->Bluff.Active)return;
 auto Action=MakeShared<FJsonObject>();Action->SetStringField(TEXT("kind"),TEXT("challenge"));Action->SetNumberField(TEXT("seat"),Seat);
 BluffStep(Action);
}

void AOKStreetSocialScene::BluffNextTurn(){
 if(!State->Bluff.Active)return;
 auto Action=MakeShared<FJsonObject>();Action->SetStringField(TEXT("kind"),TEXT("window_closed"));
 BluffStep(Action);
}

void AOKStreetSocialScene::BluffToggle(int32 Card){
 auto& B=State->Bluff;if(!B.Active)return;
 if(B.Selected.Contains(Card))B.Selected.Remove(Card);else if(B.Selected.Num()<4&&B.Hands.IsValidIndex(0)&&B.Hands[0].Contains(Card))B.Selected.Add(Card); // only what he holds
 RefreshUI();
}

void AOKStreetSocialScene::BluffPlayerPlay(){
 auto& B=State->Bluff;if(!B.Active||B.Intro||B.Selected.IsEmpty())return;
 BluffPlay(0,B.Selected.Array(),B.Rank>=0?B.Rank:B.ClaimRank);
}

// A heroine's turn: what she plays is the rule's (games.rs bluff_play inside bluff_step).
void AOKStreetSocialScene::BluffHeroinePlay(int32 Seat){
 auto Action=MakeShared<FJsonObject>();Action->SetStringField(TEXT("kind"),TEXT("heroine_turn"));Action->SetNumberField(TEXT("seat"),Seat);
 if(!BluffStep(Action).IsValid())State->Bluff.NextAt=SceneTime()+3.0; // refused (should not be): not every tick
}

void AOKStreetSocialScene::TickBluff(){
 auto& S=*State;auto& B=S.Bluff;
 // Arrived to play (ArriveOuting): dealt once everyone is at the table, or given up after a while.
 if(!B.Active&&S.BluffDealBy>0){FString Why;
  if(BluffCanStart(&Why)){S.BluffDealBy=0;StartBluff();}
  else if(SceneTime()>S.BluffDealBy){S.BluffDealBy=0;S.FreeTalkNotice=Why;S.Message=Why;RefreshUI();UE_LOG(LogTemp,Display,TEXT("Street bluff: not dealt on arrival (%s)"),*Why);}}
 if(!B.Active)return;
 // Somebody went away, or he walked off: the game is over.
 for(int32 K=1;K<B.Seats.Num();++K){const int32 G=B.Seats[K];
  if(!(S.Party.Contains(G)||FreeTalkPartner()==G)||!S.Agents.IsValidIndex(G-1)||FVector::Dist2D(S.Agents[G-1].Position,S.Male)>800.f){EndBluff(T(TEXT("ui.bluff.left_table"),FStringFormatNamedArguments{{TEXT("who"),S.Manifest.Characters[G].Name}}),INDEX_NONE);return;}}
 if(FVector::Dist2D(S.Male,B.Where)>500.f){EndBluff(T(TEXT("ui.bluff.he_left")),INDEX_NONE);return;}
 for(int32 K=1;K<B.Seats.Num();++K)S.Agents[B.Seats[K]-1].Held=true;
 const double Now=SceneTime();
 if(B.Intro){if(Now>=B.IntroUntil||(!S.FreeTalkBusy&&!BluffSpeaking()&&Now>=B.IntroUntil-22.0)){B.Intro=false;B.NextAt=Now+1.0;RefreshUI();}return;}
 if(B.Window&&B.WindowUntil==0){if(BluffSpeaking())return;B.WindowUntil=Now+B.WindowLength;for(auto& KV:B.ChallengeAt)KV.Value+=Now;RefreshUI();return;}
 if(B.Window){
  int32 First=INDEX_NONE;double At=1e18;for(const auto& KV:B.ChallengeAt)if(KV.Value<At){At=KV.Value;First=KV.Key;}
  if(First!=INDEX_NONE&&Now>=At){BluffChallenge(First);return;}
  if(Now>=B.WindowUntil)BluffNextTurn();
  return;
 }
 if(B.Turn!=0&&Now>=B.NextAt&&!BluffSpeaking()&&!S.FreeTalkBusy)BluffHeroinePlay(B.Turn);
}

// The end: a winner (or nobody, if it broke off). The heroines had fun, those who played together grow a little closer, each
// remembers the game, and the group talk gets one line about it.
void AOKStreetSocialScene::EndBluff(const FString& Why,int32 Winner){
 auto& S=*State;auto& B=S.Bluff;if(!B.Active)return;B.Active=false;B.EndedAt=SceneTime();B.Selected.Reset();
 for(int32 K=1;K<B.Seats.Num();++K)if(S.Agents.IsValidIndex(B.Seats[K]-1)&&!S.Party.Contains(B.Seats[K])&&FreeTalkPartner()!=B.Seats[K])S.Agents[B.Seats[K]-1].Held=false;
 if(Winner==INDEX_NONE){S.Message=Why.IsEmpty()?T(TEXT("ui.bluff.ended")):Why;B.Log.Add(S.Message);
  for(int32 K=1;K<B.Seats.Num();++K){TArray<FString> Others;for(int32 J=1;J<B.Seats.Num();++J)if(J!=K)Others.Add(S.Manifest.Characters[B.Seats[J]].Name);
   MindObserve(B.Seats[K],TEXT("game"),OKMsg(TEXT("ui.memory.bluff_unfinished"),{{TEXT("names"),FString::Join(Others,OKT(TEXT("ui.memory.list_separator")))},{TEXT("with_others"),Others.Num()?TEXT("yes"):TEXT("no")},{TEXT("rounds"),B.Rounds}}),B.Place,0.3f);}
  // They are told it is over (as after a win): "come on, play your card" to him after he called the game off.
  if(FreeTalkPartner()!=INDEX_NONE)S.TellHer.Add(T(TEXT("ui.bluff.prompt_stopped"),FStringFormatNamedArguments{{TEXT("why"),S.Message}}));
  UE_LOG(LogTemp,Display,TEXT("Street bluff: ended (%s)"),*S.Message);RefreshUI();return;}
 int32 Liar=INDEX_NONE,Most=0;for(int32 K=0;K<B.Caught.Num();++K)if(B.Caught[K]>Most){Most=B.Caught[K];Liar=K;}
 const FString WinName=BluffSeatName(Winner);
 UE_LOG(LogTemp,Display,TEXT("Street bluff: %s wins after %d turns"),*WinName,B.Rounds);
 // What the game leaves behind is a rule (games.rs bluff_end).
 {
  auto Request=OKRulesRequest(TEXT("bluff_end"));Request->SetNumberField(TEXT("winner"),Winner-1);Request->SetStringField(TEXT("winner_name"),WinName);Request->SetStringField(TEXT("place"),B.Place);
  TArray<TSharedPtr<FJsonValue>> Rows;
  for(int32 K=1;K<B.Seats.Num();++K){const FString Id=OKStreetStableId(S.Manifest.Characters[B.Seats[K]]);auto O=MakeShared<FJsonObject>();
   O->SetStringField(TEXT("id"),Id);O->SetStringField(TEXT("name"),S.Manifest.Characters[B.Seats[K]].Name);O->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget)));O->SetNumberField(TEXT("caught"),B.Caught.IsValidIndex(K)?B.Caught[K]:0);
   Rows.Add(MakeShared<FJsonValueObject>(O));}
  Request->SetArrayField(TEXT("heroines"),Rows);
  if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){auto Candidate=S.Store;ApplyRulesEffects(Candidate,*Answer);CommitStore(MoveTemp(Candidate));}
 }
 if(FreeTalkPartner()!=INDEX_NONE){const FString MostCaught=Liar!=INDEX_NONE?T(TEXT("ui.bluff.most_caught"),FStringFormatNamedArguments{{TEXT("who"),Liar==0?T(TEXT("ui.common.him")):BluffSeatName(Liar)}}):FString();
  S.TellHer.Add(T(TEXT("ui.bluff.prompt_after"),FStringFormatNamedArguments{{TEXT("winner"),Winner==0?T(TEXT("ui.common.him")):WinName},{TEXT("most"),MostCaught}}));}
 RefreshUI();
}

// The right panel: the start button, or while playing whose turn it is, the cards everyone has left and the log. His own
// hand is held in front of him at the bottom of the screen (BluffHandWidget).
void AOKStreetSocialScene::BluffUI(const TSharedRef<SVerticalBox>& Box){
 auto& S=*State;auto& B=S.Bluff;
 auto Button=[this](const FString& Label,bool Enabled,TFunction<void()> Do){return SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(Enabled).Text(FText::FromString(Label)).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Do](){if(Weak.IsValid())Do();return FReply::Handled();});};
 if(!B.Active){
  const int32 Count=BluffGroup().Num();
  if(Count>=2&&Count<=5)Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[Button(T(TEXT("ui.bluff.start")),true,[this](){StartBluff();})];
  if(!B.Log.IsEmpty()&&SceneTime()-B.EndedAt<60.0)Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(B.Log.Last()))];
  return;
 }
 TArray<FString> Counts;for(int32 K=0;K<B.Seats.Num();++K)Counts.Add(FString::Printf(TEXT("%s %d"),*BluffSeatName(K),B.Hands.IsValidIndex(K)?B.Hands[K].Num():0));
 Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(T(TEXT("ui.bluff.panel_head"),FStringFormatNamedArguments{{TEXT("pile"),B.PileCount},{TEXT("counts"),FString::Join(Counts,*T(TEXT("ui.bluff.counts_sep")))}})))];
 Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(2)[Button(T(TEXT("ui.bluff.quit")),true,[this](){EndBluff(T(TEXT("ui.bluff.stopped")),INDEX_NONE);})];
 TArray<FString> Recent;for(int32 I=FMath::Max(0,B.Log.Num()-6);I<B.Log.Num();++I)Recent.Add(B.Log[I]);
 Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Join(Recent,TEXT("\n"))))];
}

namespace {
// A playing card face: white, rounded, the rank in the corner and the suit large; hearts and diamonds red.
const FButtonStyle* OKBluffCardStyle(){static const FButtonStyle Style=[](){auto B=FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
 B.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.97f,.97f,.95f,1),7.f,FLinearColor(.25f,.25f,.3f,1),1.5f));
 B.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1,.97f,.82f,1),7.f,FLinearColor(.95f,.7f,.2f,1),2.f));
 B.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.9f,.88f,.8f,1),7.f,FLinearColor(.95f,.7f,.2f,1),2.f));
 B.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.85f,.85f,.84f,1),7.f,FLinearColor(.35f,.35f,.4f,1),1.5f));
 B.SetNormalPadding(FMargin(4));B.SetPressedPadding(FMargin(4));return B;}();return &Style;}
// A rank key: the glass button with little side padding, so "10" fits in a narrow key.
const FButtonStyle* OKBluffKeyStyle(){static const FButtonStyle Style=[](){auto B=*OKStreetGlassButton();B.SetNormalPadding(FMargin(2,6));B.SetPressedPadding(FMargin(2,7,2,5));return B;}();return &Style;}
const FSlateBrush* OKBluffCardBack(){static const FSlateRoundedBoxBrush Brush(FLinearColor(.55f,.08f,.1f,1),5.f,FLinearColor(.95f,.9f,.85f,1),1.5f);return &Brush;}
}

// At the bottom of the screen, in front of him: the table (everyone's cards left, the pile, the last play), then what he
// will claim (the round's rank, or when he leads the rank he picks) apart from his hand of cards, fanned out below (a
// picked card raised; he can pick any time, the play goes down on his turn), then his moves: play, pass, call.
TSharedRef<SWidget> AOKStreetSocialScene::BluffHandWidget(){
 auto& S=*State;auto& B=S.Bluff;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 const bool MyTurn=!B.Intro&&!B.Window&&B.Turn==0;const bool Leading=B.Rank<0;const bool CanCall=!B.Intro&&B.Open&&B.LastSeat>0;
 const int32 Claim=Leading?B.ClaimRank:B.Rank;
 const TArray<int32> Hand=B.Hands.IsValidIndex(0)?B.Hands[0]:TArray<int32>();
 auto Label=[](const FString& T,int32 Size,FLinearColor C=FLinearColor::White,bool Bold=false){return SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(Bold?"Bold":"Regular",Size)).ColorAndOpacity(C).Text(FText::FromString(T));};
 const FLinearColor Gold(1,.83f,.35f),Dim(.7f,.75f,.82f);
 // The table: everyone's cards left as small backs (whose turn in gold), the pile and the last play.
 auto Table=SNew(SHorizontalBox);
 for(int32 K=1;K<B.Seats.Num();++K){const bool Turn=B.Turn==K&&!B.Window;
  Table->AddSlot().AutoWidth().Padding(6,0).VAlign(VAlign_Center)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(16).HeightOverride(22)[SNew(SBorder).BorderImage(OKBluffCardBack())]]
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0,0,0)[Label(FString::Printf(TEXT("%s %d"),*BluffSeatName(K),B.Hands.IsValidIndex(K)?B.Hands[K].Num():0),12,Turn?Gold:FLinearColor::White,Turn)]];}
 Table->AddSlot().AutoWidth().Padding(14,0,4,0).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(22).HeightOverride(30)[SNew(SBorder).BorderImage(OKBluffCardBack())]];
 FString Last=T(TEXT("ui.bluff.pile"),FStringFormatNamedArguments{{TEXT("n"),B.PileCount}});
 if(B.Open&&B.LastSeat!=INDEX_NONE)Last+=T(TEXT("ui.bluff.last_play"),FStringFormatNamedArguments{{TEXT("who"),BluffSeatName(B.LastSeat)},{TEXT("n"),B.LastCount},{TEXT("rank"),OKBluffRankName(B.LastRank)},{TEXT("hint"),B.LastSeat==0?FString():T(TEXT("ui.bluff.call_hint"))}});
 Table->AddSlot().AutoWidth().VAlign(VAlign_Center)[Label(Last,12)];
 // Whose turn, on a line of its own.
 FString Turn;
 if(B.Intro)Turn=T(TEXT("ui.bluff.intro"));
 else if(B.Window)Turn=B.LastSeat==0?T(TEXT("ui.bluff.waiting_on_you")):T(TEXT("ui.bluff.just_played"),FStringFormatNamedArguments{{TEXT("who"),BluffSeatName(B.LastSeat)}});
 else if(B.Turn!=0)Turn=T(TEXT("ui.bluff.turn_of"),FStringFormatNamedArguments{{TEXT("who"),BluffSeatName(B.Turn)},{TEXT("lead"),Leading?T(TEXT("ui.bluff.new_round")):FString()}});
 else Turn=Leading?T(TEXT("ui.bluff.your_lead")):T(TEXT("ui.bluff.your_follow"));
 // What he will claim, apart from what he holds: when he leads, the ranks to pick from.
 const FString Say=Claim>=0?(B.Selected.Num()>0?T(TEXT("ui.bluff.claim_n"),FStringFormatNamedArguments{{TEXT("n"),B.Selected.Num()}}):T(TEXT("ui.bluff.claim_unknown")))+TEXT(" ")+OKBluffRankName(Claim):T(TEXT("ui.bluff.pick_rank"));
 auto ClaimRow=SNew(SHorizontalBox);
 ClaimRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)[Label(Leading?T(TEXT("ui.bluff.you_claim")):T(TEXT("ui.bluff.round_claims"),FStringFormatNamedArguments{{TEXT("rank"),OKBluffRankName(B.Rank)},{TEXT("n"),B.RoundClaimed},{TEXT("over"),B.RoundClaimed>4?T(TEXT("ui.bluff.over_four")):FString()}}),13,Dim)];
 ClaimRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)[Label(Say,16,Gold,true)];
 if(Leading)for(int32 R=0;R<13;++R){const bool On=R==B.ClaimRank;
  ClaimRow->AddSlot().AutoWidth().Padding(1,0)[SNew(SBox).WidthOverride(40)[SNew(SButton).ButtonStyle(OKBluffKeyStyle()).HAlign(HAlign_Center).ContentPadding(FMargin(0,2))
   .OnClicked_Lambda([Weak,R](){if(Weak.IsValid()){Weak->State->Bluff.ClaimRank=R;Weak->RefreshUI();}return FReply::Handled();})
   [Label(OKBluffRankName(R),12,On?Gold:FLinearColor::White,On)]]];}
 // His hand: up to 64 px a card, closer together when he holds many so it stays within the width.
 const float CardW=60.f,CardH=88.f,Lift=20.f;const float Step=FMath::Min(64.f,880.f/FMath::Max(1,Hand.Num()));
 auto Fan=SNew(SHorizontalBox);
 for(int32 I=0;I<Hand.Num();++I){const int32 C=Hand[I];const bool On=B.Selected.Contains(C);const bool Red=(C/13)%4==1||(C/13)%4==2;
  const FLinearColor Ink=Red?FLinearColor(.8f,.06f,.08f):FLinearColor(.05f,.05f,.08f);
  Fan->AddSlot().AutoWidth().VAlign(VAlign_Bottom).Padding(I==0?0.f:Step-CardW,0,0,On?Lift:0)[SNew(SBox).WidthOverride(CardW).HeightOverride(CardH)
   [SNew(SButton).ButtonStyle(OKBluffCardStyle()).ToolTipText(FText::FromString(OKBluffCardName(C)))
    .OnClicked_Lambda([Weak,C](){if(Weak.IsValid())Weak->BluffToggle(C);return FReply::Handled();})
    [SNew(SVerticalBox)
     +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[Label(OKBluffRankName(C%13),13,Ink,true)]
     +SVerticalBox::Slot().FillHeight(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[Label(OKBluffSuitName(C),22,Ink)]]]];}
 auto Buttons=SNew(SHorizontalBox);
 auto Move=[&Buttons](const FString& T,bool Enabled,TFunction<void()> Do){Buttons->AddSlot().AutoWidth().Padding(4,0)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(Enabled).Text(FText::FromString(T)).OnClicked_Lambda([Do](){Do();return FReply::Handled();})];};
 Move(Claim>=0&&B.Selected.Num()>0?T(TEXT("ui.bluff.play_n"),FStringFormatNamedArguments{{TEXT("n"),B.Selected.Num()},{TEXT("rank"),OKBluffRankName(Claim)}}):T(TEXT("ui.bluff.play")),MyTurn&&B.Selected.Num()>=1&&Claim>=0,[Weak](){if(Weak.IsValid())Weak->BluffPlayerPlay();});
 Move(T(TEXT("ui.bluff.pass")),MyTurn&&!Leading,[Weak](){if(Weak.IsValid())Weak->BluffPass();});
 Move(T(TEXT("ui.bluff.call")),CanCall,[Weak](){if(Weak.IsValid())Weak->BluffChallenge(0);});
 if(!B.Selected.IsEmpty())Move(T(TEXT("ui.bluff.unpick")),true,[Weak](){if(Weak.IsValid()){Weak->State->Bluff.Selected.Reset();Weak->RefreshUI();}});
 return SNew(SBorder).BorderImage(OKStreetGlassPanel()).Padding(FMargin(14,8))[SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Table]
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,6,0,0)[Label(Turn,13,B.Turn==0&&!B.Window&&!B.Intro?Gold:FLinearColor::White,true)]
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,6,0,0)[SNew(SBorder).BorderImage(OKStreetGlassPanel()).Padding(FMargin(10,4))[ClaimRow]]
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0,8,0,0)[Label(T(TEXT("ui.bluff.my_hand"),FStringFormatNamedArguments{{TEXT("n"),Hand.Num()}}),12,Dim)]
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,2,0,0)[SNew(SBox).HeightOverride(CardH+Lift)[Fan]]
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,6,0,0)[Buttons]];
}
