// Included by OKStreetSocialScene.cpp: plan §15 money outside the wallet. ok_mind's `fin` queries own the tickets,
// the 競技彩 book, the stock accounts and predictions (finance.json); this file moves TWD in and out of the wallet,
// turns their events into messages, memories and relation changes, and runs the heroines' own bets.

static double OKFinNum(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double Default=0){double V=Default;if(O)O->TryGetNumberField(K,V);return V;}
static FString OKFinStr(const TSharedPtr<FJsonObject>& O,const TCHAR* K){FString V;if(O)O->TryGetStringField(K,V);return V;}
static TArray<TSharedPtr<FJsonValue>> OKFinArr(const TSharedPtr<FJsonObject>& O,const TCHAR* K){const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(O&&O->TryGetArrayField(K,A))return *A;return {};}
static TSharedPtr<FJsonObject> OKFinObj(const TSharedPtr<FJsonObject>& O,const TCHAR* K){const TSharedPtr<FJsonObject>* V=nullptr;if(O&&O->TryGetObjectField(K,V))return *V;return nullptr;}
// Rust errors read "code: 中文說明"; show the explanation.
static FString OKFinError(const FString& E){FString L,R;if(E.Split(TEXT(": "),&L,&R))return R;return E;}
static FString OKFinMoney(double V){return V>=1e8?OKMsg(TEXT("ui.finance.amount_in_yi"), {{TEXT("amount_yi"), V/1e8}}):V>=1e4?OKMsg(TEXT("ui.finance.amount_in_wan"), {{TEXT("amount_wan"), V/1e4}}):FString::Printf(TEXT("%.0f"),V);}

TSharedPtr<FJsonObject> AOKStreetSocialScene::Fin(const FString& Op,TSharedPtr<FJsonObject> Args,FString& Error){
 auto& S=*State;Error.Reset();
 if(!S.Mind){Error=OKT(TEXT("ui.finance.finance_needs_dll"));return nullptr;}
 if(!Args)Args=MakeShared<FJsonObject>();
 Args=FinWallet(TEXT("to_fin"),Op,Args); // the region's money -> finance's yuan wallet
 const FDateTime Now=StreetNow();
 // "kind" routes the query to finance (ok-mind mind.rs); the op's own kind (market / limit, above / below) travels as fin_kind.
 {FString Own;if(Args->TryGetStringField(TEXT("kind"),Own))Args->SetStringField(TEXT("fin_kind"),Own);}
 Args->SetStringField(TEXT("kind"),TEXT("fin"));Args->SetStringField(TEXT("op"),Op);Args->SetStringField(TEXT("lang"),S.Lang); // names in the game's script (貴州茅臺 in a Simplified game)
 if(!Args->HasField(TEXT("date")))Args->SetStringField(TEXT("date"),Now.ToString(TEXT("%Y-%m-%d")));
 if(!Args->HasField(TEXT("minute")))Args->SetNumberField(TEXT("minute"),Now.GetHour()*60+Now.GetMinute());
 FString Text;FJsonSerializer::Serialize(Args.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
 FString Out;if(!S.Mind->Query(Text,Out,Error)){Error=OKFinError(Error);return nullptr;}
 TSharedPtr<FJsonObject> R;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Out),R)||!R){Error=TEXT("fin_json");return nullptr;}
 return FinWallet(TEXT("from_fin"),Op,R); // finance's yuan -> the region's money
}

// The journal's category of money in or out of the wallet for the finance apps (stake, prize, broker transfer).
static FString OKFinCategory(const FString& Why){
 if(Why.Contains(OKT(TEXT("ui.finance.deposit_label")))||Why.Contains(OKT(TEXT("ui.finance.withdraw_label"))))return TEXT("broker");
 if(Why.Contains(OKT(TEXT("ui.finance.claim_prize")))||Why.Contains(OKT(TEXT("ui.finance.prize_label"))))return TEXT("prize");
 if(Why.Contains(OKT(TEXT("ui.finance.lottery_char")))||Why.Contains(OKT(TEXT("ui.finance.scratch_card_label")))||Why.Contains(OKT(TEXT("ui.finance.bet_label")))||Why.Contains(OKT(TEXT("ui.finance.issue_ticket_label"))))return TEXT("lottery");
 return TEXT("service");
}

bool AOKStreetSocialScene::SpendCash(int64 Amount,const FString& Why){
 auto& S=*State;if(Amount<=0)return true;
 if(S.Store.PlayerCash<Amount){S.FinNotice=OKMsg(TEXT("ui.finance.wallet_insufficient"), {{TEXT("why"), *Why}, {TEXT("amount"), Amount}, {TEXT("wallet_cash"), S.Store.PlayerCash}});S.Message=S.FinNotice;return false;}
 auto Candidate=S.Store;OKCash(Candidate,-Amount,OKFinCategory(Why),Why);return CommitStore(MoveTemp(Candidate));
}

void AOKStreetSocialScene::GainCash(int64 Amount,const FString& Why){
 auto& S=*State;if(Amount<=0)return;auto Candidate=S.Store;OKCash(Candidate,Amount,OKFinCategory(Why),Why);
 if(CommitStore(MoveTemp(Candidate)))S.FinNotice=OKMsg(TEXT("ui.finance.wallet_gain_reason"), {{TEXT("reason"), *Why}, {TEXT("amount"), Amount}});
}

// One relation axis change for a heroine by stable id (plan §15 prediction results, trades she watches).
void AOKStreetSocialScene::FinRelation(const FString& Id,float Admiration,float Mystique,float Trust){
 auto& S=*State;auto Candidate=S.Store;const int32 R=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);if(!Candidate.Relations.IsValidIndex(R))return;
 auto& X=Candidate.Relations[R];X.Admiration=FMath::Clamp(X.Admiration+Admiration,0.f,100.f);X.Mystique=FMath::Clamp(X.Mystique+Mystique,0.f,100.f);X.Trust=FMath::Clamp(X.Trust+Trust,0.f,100.f);
 CommitStore(MoveTemp(Candidate));
}

void AOKStreetSocialScene::TickFinance(){
 auto& S=*State;if(!S.Mind||S.SelfTest)return;
 const double Wall=FPlatformTime::Seconds();if(Wall<S.FinTickAt)return;S.FinTickAt=Wall+2.0;
 FString Error;const auto R=Fin(TEXT("tick"),nullptr,Error);
 if(!R){if(!S.FinTickWarned){S.FinTickWarned=true;UE_LOG(LogTemp,Warning,TEXT("Street finance tick: %s"),*Error);}return;}
 S.FinClaims=int64(OKFinNum(R,TEXT("claims")));
 for(const auto& V:OKFinArr(R,TEXT("events")))if(const auto E=V->AsObject())HandleFinanceEvent(E);
 TickHeroineFinance();
 TickHeroineInvest();
 CheckBankruptcy();
 TickComputer();
 if(!S.App.IsEmpty()&&Wall>=S.AppRefreshAt)RefreshAppData(false);
}

void AOKStreetSocialScene::HandleFinanceEvent(const TSharedPtr<FJsonObject>& Event){
 auto& S=*State;const TSharedPtr<FJsonObject> E=FinWallet(TEXT("from_fin"),OKFinStr(Event,TEXT("kind")),Event); // finance's yuan -> the region's money
 const FString Kind=OKFinStr(E,TEXT("kind"));const FString Place=PlaceHere(S.Male);
 auto NearbyHeroines=[&](float Meters){TArray<int32> Out;for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(S.Agents.IsValidIndex(I-1)&&FVector::Dist2D(S.Agents[I-1].Position,S.Male)<Meters*100)Out.Add(I);return Out;};
 if(Kind==TEXT("lotto_draw")){S.DrawShow=OKFinObj(E,TEXT("draw"));S.DrawShowAt=FPlatformTime::Seconds();return;}
 if(Kind==TEXT("payout")){
  const int64 Amount=int64(OKFinNum(E,TEXT("amount")));const FString TicketOwner=OKFinStr(E,TEXT("owner"));const FString Prize=OKFinStr(E,TEXT("prize"));
  if(!TicketOwner.IsEmpty()){
   // A heroine's own ticket.
   const int32 I=CharacterOf(TicketOwner);if(I==INDEX_NONE)return;auto Candidate=S.Store;const int32 R=OKStreetRelationIndex(Candidate,TicketOwner,S.Manifest.TalkBudget,true);
   if(Candidate.Relations.IsValidIndex(R)){Candidate.Relations[R].Cash+=Amount;CommitStore(MoveTemp(Candidate));}
   MindObserve(I,TEXT("lottery"),OKMsg(TEXT("ui.finance.my_ticket_won"), {{TEXT("prize_name"), *Prize}, {TEXT("amount"), Amount}}),FString(),Amount>=Local(TEXT("cash"),10000)?0.85f:0.5f);
   return;
  }
  S.Message=OKMsg(TEXT("ui.finance.your_ticket_won"), {{TEXT("prize_name"), *Prize}, {TEXT("amount"), Amount}, {TEXT("claim_hint"), Amount>Local(TEXT("cash"),10000)?OKT(TEXT("ui.finance.claim_big_prize_hint")):OKT(TEXT("ui.finance.claim_at_counter_hint"))}});
  if(Amount>=Local(TEXT("cash"),10000)){
   MakeRumor(INDEX_NONE,TEXT("wealth"),OKMsg(TEXT("ui.finance.heard_he_won"), {{TEXT("prize_name"), *Prize}, {TEXT("amount"), Amount}}),FMath::Min(95.f,50.f+float(Amount)/float(Local(TEXT("cash"),20000))),false);
   for(int32 I:NearbyHeroines(15))MindObserve(I,TEXT("lottery"),OKMsg(TEXT("ui.finance.he_won_lottery"), {{TEXT("prize_name"), *Prize}, {TEXT("amount"), Amount}}),Place,0.8f);
  }
  return;
 }
 if(Kind==TEXT("sports_settled")){
  const FString TicketOwner=OKFinStr(E,TEXT("owner")),With=OKFinStr(E,TEXT("with")),Status=OKFinStr(E,TEXT("status"));const int64 Pay=int64(OKFinNum(E,TEXT("payout")));
  if(!TicketOwner.IsEmpty()){const int32 I=CharacterOf(TicketOwner);if(I!=INDEX_NONE&&Status==TEXT("won")){auto Candidate=S.Store;const int32 R=OKStreetRelationIndex(Candidate,TicketOwner,S.Manifest.TalkBudget,true);if(Candidate.Relations.IsValidIndex(R)){Candidate.Relations[R].Cash+=Pay;CommitStore(MoveTemp(Candidate));}MindObserve(I,TEXT("sports_bet"),OKMsg(TEXT("ui.finance.my_bet_won"), {{TEXT("amount"), Pay}}),FString(),0.6f);}return;}
  S.Message=Status==TEXT("won")?OKMsg(TEXT("ui.finance.sports_ticket_won"), {{TEXT("amount"), Pay}}):Status==TEXT("void")?OKT(TEXT("ui.finance.sports_ticket_refund")):OKT(TEXT("ui.finance.sports_ticket_lost"));
  if(!With.IsEmpty()){const int32 I=CharacterOf(With);if(I!=INDEX_NONE)HeroineNote(I,TEXT("sports_bet"),Status==TEXT("won")?OKMsg(TEXT("ui.finance.shared_bet_won"), {{TEXT("amount"), Pay}}):OKT(TEXT("ui.finance.shared_bet_lost")),FString(),0.6f);}
  return;
 }
 // A heroine's own account: her trades and dividends are hers, not his notices.
 if(!OKFinStr(E,TEXT("owner")).IsEmpty())return;
 if(Kind==TEXT("alert")){S.FinNotice=OKT(TEXT("ui.finance.price_alert_label"))+OKFinStr(E,TEXT("text"));S.Message=S.FinNotice;return;}
 if(Kind==TEXT("advice_result")){HandleAdviceResult(E);return;}
 if(Kind==TEXT("order_fill")||Kind==TEXT("corporate")){
  S.FinNotice=Kind==TEXT("corporate")?OKFinStr(E,TEXT("text")):OKMsg(TEXT("ui.finance.trade_filled"), {{TEXT("side"), OKFinStr(E,TEXT("side"))==TEXT("buy")?OKT(TEXT("ui.finance.buy_label")):OKT(TEXT("ui.finance.sell_label"))}, {TEXT("code"), *OKFinStr(E,TEXT("code"))}, {TEXT("qty"), OKFinNum(E,TEXT("qty"))}, {TEXT("price"), OKFinNum(E,TEXT("price"))}});
  if(S.App.IsEmpty())S.Message=S.FinNotice;
  if(S.WatchGirl!=INDEX_NONE&&Kind==TEXT("order_fill"))S.MindRecent.Add(OKStreetStableId(S.Manifest.Characters[S.WatchGirl]),OKT(TEXT("ui.finance.watching_him_order"))+S.FinNotice);
  return;
 }
 if(Kind==TEXT("prediction")){
  const bool Hit=E->GetBoolField(TEXT("hit"));const FString Text=OKFinStr(E,TEXT("text"));const int32 Streak=int32(OKFinNum(E,TEXT("streak")));
  TArray<FString> Ids;const FString Main=OKFinStr(E,TEXT("heroine"));if(!Main.IsEmpty())Ids.Add(Main);
  for(const auto& A:OKFinArr(E,TEXT("audience"))){FString Id;if(A->TryGetString(Id))Ids.AddUnique(Id);}
  // What it does to each who heard it is a rule (games.rs prediction).
  auto Request=OKRulesRequest(TEXT("prediction"));Request->SetBoolField(TEXT("hit"),Hit);Request->SetStringField(TEXT("text"),Text);Request->SetNumberField(TEXT("streak"),Streak);
  TArray<TSharedPtr<FJsonValue>> Relations;for(const FString& Id:Ids)if(CharacterOf(Id)!=INDEX_NONE)Relations.Add(MakeShared<FJsonValueObject>(OKRulesRelationJson(OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget))));
  Request->SetArrayField(TEXT("relations"),Relations);
  if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){auto C=S.Store;ApplyRulesEffects(C,*Answer);if(CommitStore(MoveTemp(C)))FlushRulesEffects();}
  S.Message=Hit?OKMsg(TEXT("ui.finance.prophecy_came_true"), {{TEXT("prophecy_text"), *Text}}):OKMsg(TEXT("ui.finance.prophecy_failed"), {{TEXT("prophecy_text"), *Text}});
  return;
 }
 if(Kind==TEXT("npc_trade_closed")){
  const FString Id=OKFinStr(E,TEXT("heroine"));const int32 I=CharacterOf(Id);if(I==INDEX_NONE)return;const int64 Out=int64(OKFinNum(E,TEXT("twd_out"))),Pnl=int64(OKFinNum(E,TEXT("pnl")));
  auto Candidate=S.Store;const int32 R=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);
  if(Candidate.Relations.IsValidIndex(R)){auto& X=Candidate.Relations[R];X.Cash+=Out;X.Fun=FMath::Clamp(X.Fun+(Pnl>=0?8.f:-10.f),0.f,100.f);CommitStore(MoveTemp(Candidate));}
  MindObserve(I,TEXT("stock"),OKMsg(TEXT("ui.finance.my_stock_result"), {{TEXT("stock_name"), *OKFinStr(E,TEXT("name"))}, {TEXT("gain_or_loss"), Pnl>=0?OKT(TEXT("ui.finance.gain_label")):OKT(TEXT("ui.finance.lose_label"))}, {TEXT("pnl_amount"), FMath::Abs(Pnl)}}),FString(),FMath::Abs(Pnl)>=1000?0.7f:0.45f);
  return;
 }
}

// Plan §15.7.6 / §15.2: heroines buy scratch cards and trade on their own, once a game day, by rules.
void AOKStreetSocialScene::TickHeroineFinance(){
 auto& S=*State;const int32 Day=S.Store.CalendarDay;const int64 Now=OKStreetMindNow(S);const int32 Hour=int32((Now%1440)/60);
 if(Hour<11||Hour>=21||S.FinHeroDay==Day)return;S.FinHeroDay=Day;
 // Who buys scratch cards and for how much is a rule (games.rs scratch_who); the draw is the lottery service's.
 auto Request=OKRulesRequest(TEXT("scratch_who"));Request->SetNumberField(TEXT("day"),Day);
 TArray<TSharedPtr<FJsonValue>> Rows;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){const FString Id=OKStreetStableId(S.Manifest.Characters[I]);auto H=MakeShared<FJsonObject>();
  H->SetNumberField(TEXT("actor"),I);H->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget)));if(const auto P=ProfileOf(I))H->SetObjectField(TEXT("profile"),P.ToSharedRef());
  H->SetBoolField(TEXT("busy"),BusyWithPlayer(I));Rows.Add(MakeShared<FJsonValueObject>(H));}
 Request->SetArrayField(TEXT("heroines"),Rows);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 for(const auto& V:Answer->GetArrayField(TEXT("buy"))){
  const auto B=V->AsObject();const FString Id=B->GetStringField(TEXT("id"));
  FString Error;auto Args=MakeShared<FJsonObject>();Args->SetStringField(TEXT("heroine"),Id);Args->SetNumberField(TEXT("budget"),B->GetNumberField(TEXT("budget")));
  const auto Res=Fin(TEXT("npc_scratch"),Args,Error);if(!Res)continue;
  auto Result=OKRulesHeroineRequest(S,TEXT("scratch_result"),Id);Result->SetNumberField(TEXT("spent"),OKFinNum(Res,TEXT("spent")));Result->SetNumberField(TEXT("won"),OKFinNum(Res,TEXT("won")));
  if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Result)){auto C=S.Store;ApplyRulesEffects(C,*Out);CommitStore(MoveTemp(C));}
 }
 // Stocks: her own A-share account (TickHeroineInvest, OKStreetSocialBank.inl).
}

// Plan §15 / §15.6 / §15.7: what the reply asked for besides words — a prediction, a trade at the computer, a bet at the shop.
void AOKStreetSocialScene::HandleFinanceReply(int32 Character,const TSharedPtr<FJsonObject>& Predict,const TSharedPtr<FJsonObject>& Trade,const TSharedPtr<FJsonObject>& Bet){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character))return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);const FString Place=PlaceHere(S.Male);
 if(Predict){
  auto Args=MakeShared<FJsonObject>();for(const auto& Pair:Predict->Values)Args->SetField(FString(*Pair.Key),Pair.Value);
  Args->SetStringField(TEXT("heroine"),Id);
  TArray<TSharedPtr<FJsonValue>> Audience;
  for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(I!=Character&&S.Agents.IsValidIndex(I-1)&&FVector::Dist2D(S.Agents[I-1].Position,S.Male)<800)Audience.Add(MakeShared<FJsonValueString>(OKStreetStableId(S.Manifest.Characters[I])));
  Args->SetArrayField(TEXT("audience"),Audience);
  FString Error;const auto R=Fin(TEXT("predict"),Args,Error);
  if(R&&R->GetBoolField(TEXT("ok"))){
   const FString Text=OKFinStr(R,TEXT("text")),Due=OKFinStr(R,TEXT("due"));
   HeroineNote(Character,TEXT("prediction"),OKMsg(TEXT("ui.finance.he_prophecy_stated"), {{TEXT("prophecy_text"), *Text}, {TEXT("due_date"), *Due}}),Place,0.5f);
   for(const auto& A:Audience){const int32 I=CharacterOf(A->AsString());if(I!=INDEX_NONE)HeroineNote(I,TEXT("prediction"),OKMsg(TEXT("ui.finance.heard_prophecy"), {{TEXT("prophecy_text"), *Text}}),Place,0.5f);}
   S.FinNotice=OKMsg(TEXT("ui.finance.prophecy_noted"), {{TEXT("prophecy_text"), *Text}, {TEXT("due_date"), *Due}});
  }
 }
 if(Trade){
  if(S.App!=TEXT("cn")&&S.App!=TEXT("us")){S.FinNotice=OKT(TEXT("ui.finance.order_needs_computer"));}
  else{
   FString Error;auto Q=MakeShared<FJsonObject>();Q->SetStringField(TEXT("type"),TEXT("stock"));Q->SetStringField(TEXT("name"),OKFinStr(Trade,TEXT("name")));
   const auto Found=Fin(TEXT("resolve"),Q,Error);
   if(!Found||!Found->GetBoolField(TEXT("found"))){S.FinNotice=OKMsg(TEXT("ui.finance.stock_not_found"), {{TEXT("stock_name"), *OKFinStr(Trade,TEXT("name"))}});}
   else{
    const FString M=OKFinStr(Found,TEXT("market"));
    if(M!=S.App)S.FinNotice=M==TEXT("cn")?OKT(TEXT("ui.finance.a_share_needs_app")):OKT(TEXT("ui.finance.us_stock_needs_buoy"));
    else{
     auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("market"),M);O->SetStringField(TEXT("code"),OKFinStr(Found,TEXT("code")));
     O->SetStringField(TEXT("side"),OKFinStr(Trade,TEXT("side"))==TEXT("sell")?TEXT("sell"):TEXT("buy"));O->SetStringField(TEXT("kind"),TEXT("market"));
     double Qty=OKFinNum(Trade,TEXT("qty"),M==TEXT("cn")?100:1);if(M==TEXT("cn")&&OKFinStr(Trade,TEXT("side"))!=TEXT("sell"))Qty=FMath::Max(100.0,FMath::RoundToDouble(Qty/100)*100);
     O->SetNumberField(TEXT("qty"),Qty);
     const auto R=Fin(TEXT("order"),O,Error);
     S.FinNotice=R?OKMsg(TEXT("ui.finance.stock_order_summary"), {{TEXT("side"), OKFinStr(O,TEXT("side"))==TEXT("buy")?OKT(TEXT("ui.finance.buy_label")):OKT(TEXT("ui.finance.sell_label"))}, {TEXT("stock_name"), *OKFinStr(Found,TEXT("name"))}, {TEXT("share_count"), Qty}, {TEXT("status"), *OKFinStr(R,TEXT("status"))}, {TEXT("note"), OKFinStr(R,TEXT("note")).IsEmpty()?TEXT(""):*(OKT(TEXT("ui.finance.comma"))+OKFinStr(R,TEXT("note")))}}):Error;
     S.FinCode=OKFinStr(Found,TEXT("code"));
     HeroineNote(Character,TEXT("stock_order"),OKMsg(TEXT("ui.finance.he_ordered_as_told"), {{TEXT("order_notice"), *S.FinNotice}}),Place,0.5f);
     RefreshAppData(true);
    }
   }
  }
 }
 if(Bet){
  if(Place!=TEXT("lottery")){S.FinNotice=OKT(TEXT("ui.finance.bet_needs_store"));}
  else{
   FString Error;auto Q=MakeShared<FJsonObject>();Q->SetStringField(TEXT("type"),TEXT("sports"));Q->SetStringField(TEXT("name"),OKFinStr(Bet,TEXT("name")));
   const auto Found=Fin(TEXT("resolve"),Q,Error);
   if(!Found||!Found->GetBoolField(TEXT("found"))){S.FinNotice=OKMsg(TEXT("ui.finance.no_upcoming_match"), {{TEXT("event_name"), *OKFinStr(Bet,TEXT("name"))}});}
   else{
    const bool Win=OKFinStr(Bet,TEXT("outcome"))!=TEXT("lose");const FString Side=OKFinStr(Found,TEXT("side"));
    const int64 Stake=FMath::Clamp<int64>(int64(OKFinNum(Bet,TEXT("stake"),20)),10,2000);
    if(SpendCash(Stake,OKT(TEXT("ui.finance.sports_lottery_label")))){
     auto Sel=MakeShared<FJsonObject>();Sel->SetStringField(TEXT("target"),OKFinStr(Found,TEXT("target")));Sel->SetStringField(TEXT("market"),TEXT("ml"));Sel->SetStringField(TEXT("pick"),Win==(Side==TEXT("home"))?TEXT("home"):TEXT("away"));
     auto B=MakeShared<FJsonObject>();B->SetNumberField(TEXT("stake"),double(Stake));B->SetArrayField(TEXT("selections"),{MakeShared<FJsonValueObject>(Sel)});B->SetStringField(TEXT("with"),Id);
     const auto R=Fin(TEXT("sports_bet"),B,Error);
     if(!R){GainCash(Stake,OKT(TEXT("ui.finance.bet_void_refund")));S.FinNotice=Error;}
     else{S.FinNotice=OKMsg(TEXT("ui.finance.bet_placed_summary"), {{TEXT("event_name"), *OKFinStr(Found,TEXT("name"))}, {TEXT("win_lose"), Win?OKT(TEXT("ui.finance.win_label")):OKT(TEXT("ui.finance.lose_game_label"))}, {TEXT("event_date"), *OKFinStr(Found,TEXT("date"))}, {TEXT("stake"), Stake}, {TEXT("odds"), OKFinNum(R,TEXT("odds"))}});
      HeroineNote(Character,TEXT("sports_bet"),OKT(TEXT("ui.finance.bet_together_label"))+S.FinNotice,Place,0.5f);}
    }
   }
  }
 }
 if(Predict||Trade||Bet){S.Message=S.FinNotice;RefreshUI();}
}

// Plan §15.6: where a computer can be used now, with the reason when it cannot.
FString AOKStreetSocialScene::ComputerHere(FString* Why) const{
 const auto& S=*State;const FString Place=PlaceHere(S.Male);const FDateTime Now=StreetNow();const int32 H=Now.GetHour();const bool Weekday=int32(Now.GetDayOfWeek())<5;
 auto No=[&](const TCHAR* Reason){if(Why)*Why=Reason;return FString();};
 if(Place==TEXT("boys_dorm"))return Place;
 if(Place==TEXT("library")){if(Now.GetDayOfWeek()==EDayOfWeek::Monday)return No(OKT(TEXT("ui.finance.library_closed_monday")));if(H<8||H>=21)return No(OKT(TEXT("ui.finance.library_computer_hours")));return Place;}
 if(Place==TEXT("school")){if(H<8||H>=22)return No(OKT(TEXT("ui.finance.computer_room_hours")));if(Weekday&&((H>=10&&H<12)||(H>=14&&H<16)))return No(OKT(TEXT("ui.finance.computer_room_in_class")));return Place;}
 if(Place==TEXT("hotel"))return Place;
 return No(OKT(TEXT("ui.finance.no_computer_here")));
}

// Library: 2 hours a sitting; closing time and class time log him out, each with a 5-minute warning (plan §15.6).
void AOKStreetSocialScene::TickComputer(){
 auto& S=*State;if((S.App!=TEXT("cn")&&S.App!=TEXT("us"))||S.AppReadOnly)return;
 const int64 Now=OKStreetMindNow(S);FString Why;
 if(ComputerHere(&Why).IsEmpty()){CloseApp();S.Message=Why.IsEmpty()?OKT(TEXT("ui.finance.left_computer")):Why+OKT(TEXT("ui.finance.logged_out_suffix"));return;}
 const FDateTime Clock=StreetNow();const int32 Minute=Clock.GetHour()*60+Clock.GetMinute();
 int32 EndAt=24*60+1;
 if(S.PcPlace==TEXT("library"))EndAt=FMath::Min<int32>(21*60,int32(S.PcStartMinute%1440)+120);
 if(S.PcPlace==TEXT("school")){EndAt=22*60;if(int32(Clock.GetDayOfWeek())<5)for(const int32 C:{10*60,14*60})if(Minute<C){EndAt=FMath::Min(EndAt,C);}}
 if(Minute>=EndAt-5&&Minute<EndAt&&!S.PcWarned){S.PcWarned=true;S.FinNotice=OKT(TEXT("ui.finance.logout_warning"));S.Message=S.FinNotice;}
 if(S.PcPlace==TEXT("hotel")&&Now-S.PcStartMinute>=60*S.PcPaidHours){if(!SpendCash(Local(TEXT("cash"),40),OKT(TEXT("ui.finance.business_center_rate")))){CloseApp();return;}++S.PcPaidHours;}
}

// Plan §15.7.1: the counter menu needs him standing at the lottery shop's counter (within 2.5 m of its anchor).
bool AOKStreetSocialScene::AtLotteryCounter() const{
 const auto* P=OKStreetFindPlace(TEXT("lottery"));if(!P||PlaceHere(State->Male)!=TEXT("lottery"))return false;
 for(const auto& A:P->Anchors)if(A.Name==TEXT("counter"))return FVector::Dist2D(A.Position,State->Male)<250.f;
 return true;
}

// The panel shows the counter button only near it: rebuild when he steps up to or away from the counter.
void AOKStreetSocialScene::TickCounterProximity(){
 auto& S=*State;const bool Near=AtLotteryCounter();if(Near==S.NearCounter)return;S.NearCounter=Near;
 if(!Near&&S.App==TEXT("lottery")&&!S.AppReadOnly)CloseApp();else RefreshUI();
}

FString AOKStreetSocialScene::FinanceSceneText() const{
 const auto& S=*State;
 if(S.App==TEXT("cn"))return S.AppReadOnly?OKT(TEXT("ui.finance.phone_watch_a_share")):OKT(TEXT("ui.finance.computer_a_share_open"));
 if(S.App==TEXT("us"))return S.AppReadOnly?OKT(TEXT("ui.finance.phone_watch_us")):OKT(TEXT("ui.finance.computer_us_open"));
 if(PlaceHere(S.Male)==TEXT("lottery"))return OKT(TEXT("ui.finance.store_offers_games"));
 return FString();
}
