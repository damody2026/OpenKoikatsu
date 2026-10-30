// Included by OKStreetSocialScene.cpp: Docs/STREET_SOCIAL_FINANCE_IMPROVEMENT_PLAN.md stages 1-3 — his bank (savings interest,
// time deposits, credit card, student loan, tuition/dorm/phone bills, scholarship), net worth and price alerts, and the
// heroines' own money (savings, interest, saving goals, tuition, their own A-share accounts, his advice).

static int32 OKBankDayOf(const FDateTime& D){return int32((FDateTime(D.GetYear(),D.GetMonth(),D.GetDay())-OKStreetCalendarEpoch()).GetDays());}

static void OKBankNote(FOKStreetStore& C,const FString& Text){
 C.Banking.Notes.Add(OKMsg(TEXT("ui.bank.date_text_line"), {{TEXT("date"), *OKStreetCalendarDateTime(C.CalendarDay,8.0).ToString(TEXT("%Y/%m/%d"))}, {TEXT("text"), *Text}}));
 while(C.Banking.Notes.Num()>40)C.Banking.Notes.RemoveAt(0);
}

// A bill paid from savings first, then the wallet; nothing is taken unless all of it can be.
static bool OKBankTake(FOKStreetStore& C,int64 Amount,const FString& Category,const FString& Note){return OKPrivateTake(C,Amount,Category,Note);}
// An account other than the wallet or savings changes (deposits, the card, the student loan, phone arrears): the
// journal is told (OKStreetSocialBooks.inl).
static void OKBankPost(FOKStreetStore& C,const TCHAR* Account,int64 Amount,const TCHAR* Category,const FString& Note){OKBookPost(C,TEXT("private"),Account,Amount,Category,Note);}

// ------------------------------------------------------------------------------------------------ net worth

int64 AOKStreetSocialScene::NetWorthOf(const FOKStreetStore& C,TArray<TPair<FString,int64>>* Parts){
 auto& S=*State;TArray<TPair<FString,int64>> P;
 P.Add({OKT(TEXT("ui.bank.wallet_label")),C.PlayerCash});P.Add({OKT(TEXT("ui.bank.demand_deposit_label")),C.PlayerBank});P.Add({OKT(TEXT("ui.bank.fixed_deposit_label")),OKStreetDepositsTotal(C.Banking)});
 FString Error;
 if(const auto Sum=Fin(TEXT("summary"),nullptr,Error)){
  const auto Acc=OKFinObj(Sum,TEXT("accounts"));
  P.Add({OKT(TEXT("ui.bank.a_shares_account_label")),int64(OKFinNum(OKFinObj(Acc,TEXT("cn")),TEXT("twd")))});P.Add({OKT(TEXT("ui.bank.us_account_converted_label")),int64(OKFinNum(OKFinObj(Acc,TEXT("us")),TEXT("twd")))});
  P.Add({OKT(TEXT("ui.bank.pending_bonus_label")),int64(OKFinNum(Sum,TEXT("unclaimed")))});
 }
 if(C.Company.Founded>=0)P.Add({OKT(TEXT("ui.bank.company_equity_label")),CompanyEquity(C)});if(C.Company.Estate.OwnerLoan)P.Add({OKT(TEXT("ui.bank.loan_to_company_label")),C.Company.Estate.OwnerLoan});
 int64 Owed=0;for(const auto& R:C.Relations)Owed+=R.OwesPlayer;if(Owed)P.Add({OKT(TEXT("ui.bank.her_owes_you_label")),Owed});
 int64 Held=0,Lent=0;for(const auto& E:C.Entrusts)if(E.Status==TEXT("open")){if(E.Kind==TEXT("entrust"))Held+=E.Principal;else Lent+=E.Principal;}
 if(Lent)P.Add({OKT(TEXT("ui.bank.her_recovery_capital_label")),Lent});if(Held)P.Add({OKT(TEXT("ui.bank.her_money_held_label")),-Held});
 if(C.Banking.Card.Balance)P.Add({OKT(TEXT("ui.bank.credit_card_debt_label")),-C.Banking.Card.Balance});
 if(C.Banking.Loan.Owed)P.Add({OKT(TEXT("ui.bank.student_loan_label")),-C.Banking.Loan.Owed});
 if(C.Banking.PhoneArrears)P.Add({OKT(TEXT("ui.bank.phone_arrears_label")),-C.Banking.PhoneArrears});
 int64 Total=0;for(const auto& X:P)Total+=X.Value;
 if(Parts)*Parts=MoveTemp(P);
 return Total;
}

// The tuition line on his task list for today, when it was not worked out today (a save just loaded, a new game): it
// was only set by the day's bank run, so after a load the list lost "pay tuition by 9/27" until the next morning.
void AOKStreetSocialScene::RefreshFeesDue() const{
 auto& S=*State;if(S.FeesDueDay==S.Store.CalendarDay)return;S.FeesDueDay=S.Store.CalendarDay;
 auto Request=OKRulesRequest(TEXT("bank_fees_due"));Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);Request->SetObjectField(TEXT("player_academic"),OKStreetAcademicJson(S.Store.PlayerAcademic));
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request))S.FeesDueText=Out->GetStringField(TEXT("fees_due"));
}

// ------------------------------------------------------------------------------------------------ daily (from NextDay)

void AOKStreetSocialScene::BankDaily(FOKStreetStore& C){
 // The bank's day is the rules' (bank.rs bank_daily): interest, deposits rolling over, the phone bill, the card statement,
 // interest and late fees, the student loan, tuition and dorm fees, the heroines' savings, goals and tuition.
 auto& S=*State;auto Request=OKRulesRequest(TEXT("bank_daily"));
 Request->SetNumberField(TEXT("day"),C.CalendarDay);Request->SetNumberField(TEXT("player_cash"),double(C.PlayerCash));Request->SetNumberField(TEXT("player_bank"),double(C.PlayerBank));
 const bool Charted=!C.Banking.NetWorth.IsEmpty()&&C.Banking.NetWorth.Last().Key>=C.CalendarDay-1;
 Request->SetNumberField(TEXT("net_worth"),Charted?0.0:double(NetWorthOf(C))); // the day that ended, for the chart (everything he owns)
 Request->SetObjectField(TEXT("banking"),OKStreetBankingJson(C.Banking));Request->SetObjectField(TEXT("player_academic"),OKStreetAcademicJson(C.PlayerAcademic));
 TArray<TSharedPtr<FJsonValue>> Heroines;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);const int32 X=OKStreetRelationIndex(C,Id,S.Manifest.TalkBudget,true);if(!C.Relations.IsValidIndex(X))continue;
  auto H=MakeShared<FJsonObject>();H->SetObjectField(TEXT("relation"),OKRulesRelationJson(C.Relations[X]));
  if(const TSharedPtr<FJsonObject> P=S.MindProfiles.FindRef(Id))H->SetObjectField(TEXT("profile"),P.ToSharedRef());
  if(const auto* A=C.Academics.Find(Id))H->SetObjectField(TEXT("academic"),OKStreetAcademicJson(*A));
  Heroines.Add(MakeShared<FJsonValueObject>(H));
 }
 Request->SetArrayField(TEXT("heroines"),Heroines);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 OKRulesReadBank(C,*Out);ApplyRulesEffects(C,*Out);S.FeesDueText=Out->GetStringField(TEXT("fees_due"));S.FeesDueDay=C.CalendarDay; // on the task list while open and unpaid
 const FString Message=Out->GetStringField(TEXT("message"));if(!Message.IsEmpty())S.Message=Message;
}

// ------------------------------------------------------------------------------------------------ his actions

// What he does at the bank, and paying by card (bank.rs bank_action): the rules check it and move the money.
TSharedPtr<FJsonObject> AOKStreetSocialScene::BankRequest(const FOKStreetStore& C,const FString& What,int64 Amount,int32 Arg) const{
 auto Request=OKRulesRequest(TEXT("bank_action"));Request->SetStringField(TEXT("what"),What);Request->SetNumberField(TEXT("amount"),double(Amount));Request->SetNumberField(TEXT("arg"),Arg);
 Request->SetNumberField(TEXT("day"),C.CalendarDay);Request->SetNumberField(TEXT("player_cash"),double(C.PlayerCash));Request->SetNumberField(TEXT("player_bank"),double(C.PlayerBank));
 Request->SetObjectField(TEXT("banking"),OKStreetBankingJson(C.Banking));Request->SetObjectField(TEXT("player_academic"),OKStreetAcademicJson(C.PlayerAcademic));
 return Request;
}

bool AOKStreetSocialScene::PayByCard(FOKStreetStore& C,int64 Amount,const FString& Place,const FString& Why,const TCHAR* Category){
 const TSharedRef<FJsonObject> Request=BankRequest(C,TEXT("card_charge"),Amount,0).ToSharedRef();Request->SetStringField(TEXT("place"),Place);Request->SetStringField(TEXT("why"),Why);Request->SetStringField(TEXT("category"),Category);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(*State,Request);if(!Out.IsValid()||!Out->GetBoolField(TEXT("charged")))return false;
 OKRulesReadBank(C,*Out);ApplyRulesEffects(C,*Out);return true;
}

void AOKStreetSocialScene::BankAction(const FString& What,int64 Amount,int32 Arg){
 auto& S=*State;auto C=S.Store;
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,BankRequest(C,What,Amount,Arg).ToSharedRef());if(!Out.IsValid())return;
 FString Notice;if(Out->TryGetStringField(TEXT("notice"),Notice)){S.FinNotice=Notice;PopulateApp();return;}
 FString Done;if(!Out->TryGetStringField(TEXT("done"),Done)||Done.IsEmpty())return;
 OKRulesReadBank(C,*Out);ApplyRulesEffects(C,*Out);
 if(CommitStore(MoveTemp(C))){S.FinNotice=Done;S.Message=Done;
  // Paid (with his money or the student loan): off the task list now, not at tomorrow's bank day ("pay tuition by
  // 9/27" stayed on the list after the loan had paid it).
  if(What==TEXT("fees_pay")||What==TEXT("loan_apply"))S.FeesDueText.Reset();}
 if(!S.App.IsEmpty())PopulateApp();RefreshUI();
}

// Phone cut off: calls and phone apps stop (online banking still works, so the arrears can be paid).
bool AOKStreetSocialScene::PhoneCutOff(FString* Why) const{
 const int64 A=State->Store.Banking.PhoneArrears;if(A<=0)return false;
 if(Why)*Why=OKMsg(TEXT("ui.bank.phone_suspended_notice"), {{TEXT("arrears"), A}});return true;
}

// The dorm is his while this term's fees are paid (or during the break).
bool AOKStreetSocialScene::DormLocked(FString* Why) const{
 const auto& S=*State;EOKStreetTermPhase Ph;const FDateTime Now=StreetNow();bool InWindow=false;FOKStreetTerm Fees;const FOKStreetTerm T=OKRulesTermAt(S,Now,Ph,&Fees,&InWindow);
 if(Ph==EOKStreetTermPhase::Break||S.Store.PlayerAcademic.FeesPaidTerm==T.Key)return false;
 if(InWindow&&Fees.Key==T.Key)return false; // still within the payment window
 if(Why)*Why=OKT(TEXT("ui.bank.dorm_unpaid_notice"));return true;
}

// ------------------------------------------------------------------------------------------------ heroines' own accounts

void AOKStreetSocialScene::TickHeroineInvest(){
 auto& S=*State;if(S.SelfTest||!S.Mind)return;
 const FDateTime Now=StreetNow();const int32 Minute=Now.GetHour()*60+Now.GetMinute();const int32 Day=S.Store.CalendarDay;
 if(Minute<9*60+20||Minute>13*60+20||int32(Now.GetDayOfWeek())>=5||Minute==S.HerInvestMinute)return;S.HerInvestMinute=Minute;
 // Whose turn it is, her style and the money in are rules (games.rs invest_plan); the account is the market service's.
 auto Plan=OKRulesRequest(TEXT("invest_plan"));Plan->SetNumberField(TEXT("day"),Day);Plan->SetNumberField(TEXT("minute"),Minute);Plan->SetNumberField(TEXT("weekday"),int32(Now.GetDayOfWeek()));
 Plan->SetBoolField(TEXT("everyone"),!FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_INVESTORS")).IsEmpty()); // development: every heroine with money takes her turn
 TArray<TSharedPtr<FJsonValue>> Rows;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){const FString Id=OKStreetStableId(S.Manifest.Characters[I]);auto H=MakeShared<FJsonObject>();
  H->SetNumberField(TEXT("actor"),I);H->SetStringField(TEXT("id"),Id);H->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget)));
  if(const auto P=ProfileOf(I))H->SetObjectField(TEXT("profile"),P.ToSharedRef());H->SetNumberField(TEXT("invest_total"),double(S.HerInvestTotal.FindRef(Id)));
  H->SetBoolField(TEXT("done"),S.HerInvestDay.FindRef(I)==Day);H->SetBoolField(TEXT("busy"),BusyWithPlayer(I));Rows.Add(MakeShared<FJsonValueObject>(H));}
 Plan->SetArrayField(TEXT("heroines"),Rows);
 const TSharedPtr<FJsonObject> Turns=OKRulesEval(S,Plan);if(!Turns)return;
 for(const auto& TV:Turns->GetArrayField(TEXT("turns"))){
  const auto T=TV->AsObject();const int32 I=int32(T->GetNumberField(TEXT("actor")));const FString Id=T->GetStringField(TEXT("id"));
  S.HerInvestDay.Add(I,Day);
  const FString Style=T->GetStringField(TEXT("style"));if(Style.IsEmpty())continue;
  const int64 Deposit=int64(T->GetNumberField(TEXT("deposit"))),FromCash=int64(T->GetNumberField(TEXT("from_cash")));
  FString Error;auto Args=MakeShared<FJsonObject>();Args->SetStringField(TEXT("heroine"),Id);Args->SetStringField(TEXT("style"),Style);Args->SetNumberField(TEXT("deposit"),double(Deposit));
  const auto Res=Fin(TEXT("heroine_invest"),Args,Error);if(!Res){UE_LOG(LogTemp,Warning,TEXT("Street heroine invest %s: %s"),*Id,*Error);continue;}
  if(Res->HasField(TEXT("skipped")))continue;
  auto C=S.Store;const int32 X=OKStreetRelationIndex(C,Id,S.Manifest.TalkBudget,true);if(!C.Relations.IsValidIndex(X))continue;auto& W=C.Relations[X];
  W.Cash-=FromCash;W.Bank-=Deposit-FromCash;W.Flags.AddUnique(TEXT("investor"));
  TArray<FString> Did;
  for(const auto& V:OKFinArr(Res,TEXT("actions"))){const auto A=V->AsObject();if(OKFinStr(A,TEXT("status"))!=TEXT("filled"))continue;
   const bool Buy=OKFinStr(A,TEXT("side"))==TEXT("buy");const FString Why=OKFinStr(A,TEXT("why"));
   const FString Text=OKMsg(TEXT("ui.bank.trade_done_line"), {{TEXT("action"), Buy?OKT(TEXT("ui.bank.buy_label")):OKT(TEXT("ui.bank.sell_label"))}, {TEXT("name"), *OKFinStr(A,TEXT("name"))}, {TEXT("qty"), OKFinNum(A,TEXT("qty"))}, {TEXT("price"), OKFinNum(A,TEXT("price"))}, {TEXT("reason_suffix"), Why==TEXT("advice")?OKT(TEXT("ui.bank.reason_his_advice")):Why==TEXT("take_profit")?OKT(TEXT("ui.bank.reason_take_profit")):Why==TEXT("stop_loss")?OKT(TEXT("ui.bank.reason_stop_loss")):Why==TEXT("held_long")?OKT(TEXT("ui.bank.reason_held_too_long")):TEXT("")}});
   Did.Add(Text);MindObserve(I,TEXT("stock"),OKT(TEXT("ui.bank.pay_with_own_money"))+Text,FString(),0.45f);}
  const double Total=OKFinNum(Res,TEXT("total")),Cash=OKFinNum(Res,TEXT("cash"));
  // Short of pocket money: she takes some back out.
  if(W.Cash<Local(TEXT("cash"),150)&&Cash>=300){auto Out=MakeShared<FJsonObject>();Out->SetStringField(TEXT("market"),TEXT("cn"));Out->SetStringField(TEXT("owner"),Id);Out->SetNumberField(TEXT("amount"),FMath::Min(Cash,1000.0));
   if(const auto O=Fin(TEXT("withdraw"),Out,Error)){W.Cash+=int64(OKFinNum(O,TEXT("twd")));Did.Add(OKT(TEXT("ui.bank.withdrew_stock_cash_note")));}}
  CommitStore(MoveTemp(C));
  S.HerInvestTotal.Add(Id,int64(Total));
  S.HerInvestText.Add(Id,OKMsg(TEXT("ui.bank.own_a_shares_summary"), {{TEXT("total"), Total}, {TEXT("today_trades"), Did.IsEmpty()?TEXT(""):*(OKT(TEXT("ui.bank.today_prefix"))+FString::Join(Did,OKT(TEXT("ui.bank.list_separator"))))}}));
  UE_LOG(LogTemp,Display,TEXT("Street heroine invest: %s style=%s deposit=%lld total=%.0f actions=%d"),*Id,*Style,Deposit,Total,Did.Num());
 }
}

// His advice about her account: she decides now; carried out on her next trading turn; judged a week later.
void AOKStreetSocialScene::AdviseHeroine(int32 Heroine,const FString& Code,const FString& Side){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Heroine)||Heroine<=0)return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Heroine]);const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 const auto P=ProfileOf(Heroine);
 // Whether she takes it is a rule (games.rs advice_answer).
 auto Ask=OKRulesRelationRequest(TEXT("advice_answer"),R,P);Ask->SetStringField(TEXT("key"),Code+Side);Ask->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Ask);const bool Yes=Answer&&Answer->GetBoolField(TEXT("yes"));
 FString Error;auto Args=MakeShared<FJsonObject>();Args->SetStringField(TEXT("heroine"),Id);Args->SetStringField(TEXT("code"),Code);Args->SetStringField(TEXT("side"),Side);Args->SetBoolField(TEXT("accepted"),Yes);
 const auto Res=Fin(TEXT("advise"),Args,Error);if(!Res){S.FinNotice=Error;PopulateApp();return;}
 FString Name=Code;{auto Q=MakeShared<FJsonObject>();Q->SetStringField(TEXT("market"),TEXT("cn"));Q->SetStringField(TEXT("code"),Code);if(const auto X=Fin(TEXT("quote"),Q,Error))Name=OKFinStr(X,TEXT("name"));}
 const FString What=FString::Printf(TEXT("%s%s"),Side==TEXT("buy")?OKT(TEXT("ui.bank.buy_label")):OKT(TEXT("ui.bank.sell_label")),*Name);
 if(Yes){auto C=S.Store;const int32 X=OKStreetRelationIndex(C,Id,S.Manifest.TalkBudget,true);if(C.Relations.IsValidIndex(X)){C.Relations[X].Flags.AddUnique(TEXT("investor"));CommitStore(MoveTemp(C));}}
 HeroineNote(Heroine,TEXT("stock_advice"),OKMsg(TEXT("ui.bank.advice_follow_recap"), {{TEXT("advice"), *What}, {TEXT("decision"), Yes?OKT(TEXT("ui.bank.decided_follow_advice")):OKT(TEXT("ui.bank.did_not_follow"))}}),PlaceHere(S.Male),0.5f);
 S.MindRecent.Add(Id,OKMsg(TEXT("ui.bank.advice_decision_recap"), {{TEXT("advice"), *What}, {TEXT("decision"), Yes?OKT(TEXT("ui.bank.agreed_follow_label")):OKT(TEXT("ui.bank.did_not_agree"))}}));
 S.FinNotice=OKMsg(TEXT("ui.bank.heroine_reply_line"), {{TEXT("heroine_name"), *S.Manifest.Characters[Heroine].Name}, {TEXT("reply"), Yes?*OKMsg(TEXT("ui.bank.her_agree_quote"), {{TEXT("advice"), *What}}):OKT(TEXT("ui.bank.her_no_follow_quote"))}});
 PopulateApp();RefreshUI();
}

void AOKStreetSocialScene::HandleAdviceResult(const TSharedPtr<FJsonObject>& E){
 auto& S=*State;const FString Id=OKFinStr(E,TEXT("heroine"));const int32 I=CharacterOf(Id);if(I==INDEX_NONE)return;
 // A week later: what it does to her is a rule (games.rs advice_result).
 auto Request=OKRulesHeroineRequest(S,TEXT("advice_result"),Id);Request->SetBoolField(TEXT("took"),E->GetBoolField(TEXT("accepted")));Request->SetBoolField(TEXT("right"),E->GetBoolField(TEXT("right")));
 Request->SetNumberField(TEXT("pct"),OKFinNum(E,TEXT("pct")));Request->SetStringField(TEXT("what"),FString::Printf(TEXT("%s%s"),OKFinStr(E,TEXT("side"))==TEXT("buy")?OKT(TEXT("ui.bank.buy_label")):OKT(TEXT("ui.bank.sell_label")),*OKFinStr(E,TEXT("name"))));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 auto C=S.Store;ApplyRulesEffects(C,*Answer);if(CommitStore(MoveTemp(C)))FlushRulesEffects();
}

// The heroine's account inside "和她談錢・工作": her holdings, and his advice.
void AOKStreetSocialScene::BuildHerInvestRows(const TSharedRef<SVerticalBox>& Box){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);const int32 Her=S.VentureHeroine;
 if(!S.Manifest.Characters.IsValidIndex(Her)||Her<=0)return;
 auto Line=[&](TSharedRef<SWidget> W){Box->AddSlot().AutoHeight().Padding(2)[W];};
 Line(OKFinText(OKMsg(TEXT("ui.bank.her_stock_account_title"), {{TEXT("name"), *S.Manifest.Characters[Her].Name}}),FLinearColor(1,.85f,.3f),13,true));
 const auto A=S.HerAccount;
 if(!A||OKFinArr(A,TEXT("positions")).IsEmpty())Line(OKFinText(OKMsg(TEXT("ui.bank.no_holdings_cash"), {{TEXT("cash"), OKFinNum(A,TEXT("cash"))}}),FLinearColor(.75f,.75f,.75f),11));
 else{Line(OKFinText(OKMsg(TEXT("ui.bank.total_and_cash"), {{TEXT("total"), OKFinNum(A,TEXT("total"))}, {TEXT("cash"), OKFinNum(A,TEXT("cash"))}}),FLinearColor::White,11));
  for(const auto& V:OKFinArr(A,TEXT("positions"))){const auto P=V->AsObject();const FString Code=OKFinStr(P,TEXT("code"));
   Line(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[OKFinText(OKMsg(TEXT("ui.bank.holding_line"), {{TEXT("symbol"), *OKFinStr(P,TEXT("symbol"))}, {TEXT("name"), *OKFinStr(P,TEXT("name"))}, {TEXT("qty"), OKFinNum(P,TEXT("qty"))}, {TEXT("avg"), OKFinNum(P,TEXT("avg"))}, {TEXT("price"), OKFinNum(P,TEXT("price"))}, {TEXT("pnl"), OKFinNum(P,TEXT("pnl"))}, {TEXT("pnl_pct"), OKFinNum(P,TEXT("pnl_pct"))}}),OKFinUpDown(OKFinNum(P,TEXT("pnl")),true),11)]
    +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.bank.suggest_sell_label")),[Weak,Her,Code](){Weak->AdviseHeroine(Her,Code,TEXT("sell"));})]);}}
 auto Row=SNew(SWrapBox).UseAllottedSize(true);
 for(const FString& Code:S.FinWatchCodes[0].IsEmpty()?TArray<FString>{TEXT("sh.601318"),TEXT("sh.600036"),TEXT("sz.000002")}:S.FinWatchCodes[0])
  {FString Name=Code;for(const auto& V:OKFinArr(S.FinWatch,TEXT("quotes")))if(const auto X=V->AsObject())if(OKFinStr(X,TEXT("code"))==Code)Name=OKFinStr(X,TEXT("name"));
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.bank.suggest_buy_prefix"))+Name,[Weak,Her,Code](){Weak->AdviseHeroine(Her,Code,TEXT("buy"));})];}
 Line(OKFinText(OKT(TEXT("ui.bank.suggest_buy_header")),FLinearColor(.75f,.75f,.75f),10));Line(Row);
}

// ------------------------------------------------------------------------------------------------ the bank app (ATM / 網銀)

TSharedRef<SWidget> AOKStreetSocialScene::BuildBankTab(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);const auto& B=S.Store.Banking;const FDateTime Today=StreetNow();
 const TSharedPtr<FJsonObject> Info=OKRulesBankInfo(S,OKBankDayOf(Today));if(!Info.IsValid())return SNew(SBox);
 const TSharedPtr<FJsonObject> Rate=Info->GetObjectField(TEXT("rates"));auto R=[&](const TCHAR* Name){return Rate->GetNumberField(Name);};auto Money=[&](const TCHAR* Name){return int64(Info->GetNumberField(Name));};
 auto Box=SNew(SVerticalBox);auto Line=[&](TSharedRef<SWidget> W){Box->AddSlot().AutoHeight().Padding(2)[W];};
 auto Gold=FLinearColor(1,.85f,.3f),Dim=FLinearColor(.75f,.75f,.75f);
 auto Amount=[&](){return SNew(SBox).WidthOverride(120)[SNew(SEditableTextBox).Text(FText::FromString(S.BankAmount)).OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->BankAmount=T.ToString();})];};
 const FString Tab=S.AtmTab;
 if(Tab==TEXT("deposit")){
  Line(OKFinText(OKMsg(TEXT("ui.bank.demand_deposit_summary"), {{TEXT("demand_balance"), S.Store.PlayerBank}, {TEXT("demand_rate"), R(TEXT("demand"))}, {TEXT("fixed_total"), OKStreetDepositsTotal(B)}}),Gold,13,true));
  Line(OKFinText(OKMsg(TEXT("ui.bank.deposit_rates_today"), {{TEXT("m3"), R(TEXT("m3"))}, {TEXT("m6"), R(TEXT("m6"))}, {TEXT("y1"), R(TEXT("y1"))}, {TEXT("y2"), R(TEXT("y2"))}, {TEXT("y3"), R(TEXT("y3"))}}),Dim,11));
  auto Row=SNew(SWrapBox).UseAllottedSize(true);Row->AddSlot().Padding(2)[OKFinText(OKT(TEXT("ui.bank.amount_yen_prefix")))];Row->AddSlot().Padding(2)[Amount()];
  for(const auto& V:Info->GetArrayField(TEXT("terms"))){const int32 M=int32(V->AsObject()->GetNumberField(TEXT("months")));Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.bank.deposit_term_option"), {{TEXT("months"), M}, {TEXT("rate"), V->AsObject()->GetNumberField(TEXT("rate"))}}),[Weak,M](){Weak->BankAction(TEXT("deposit_open"),Weak->MoneyIn(Weak->State->BankAmount),M);})];}
  Line(Row);
  for(const auto& D:B.Deposits){const int32 Id=D.Id;const int32 Mature=D.StartDay+FMath::RoundToInt(D.Months*365.f/12.f);
   Line(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[OKFinText(OKMsg(TEXT("ui.bank.deposit_summary_line"), {{TEXT("deposit_id"), D.Id}, {TEXT("amount"), D.Amount}, {TEXT("months"), D.Months}, {TEXT("rate"), D.Rate}, {TEXT("maturity_date"), *OKStreetCalendarDateTime(Mature,8.0).ToString(TEXT("%Y/%m/%d"))}, {TEXT("est_interest"), double(D.Amount)*D.Rate/100.0*D.Months/12.0}}),FLinearColor::White,11)]
    +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.bank.early_withdrawal_label")),[Weak,Id](){Weak->BankAction(TEXT("deposit_close"),0,Id);})]);}
 }else if(Tab==TEXT("card")){
  const auto& Cd=B.Card;
  if(!Cd.Issued)Line(OKFinButton(Weak,OKMsg(TEXT("ui.bank.apply_student_card"), {{TEXT("card_limit"), Money(TEXT("card_limit"))}}),[Weak](){Weak->BankAction(TEXT("card_apply"),0,0);}));
  else{
   Line(OKFinText(OKMsg(TEXT("ui.bank.credit_card_status"), {{TEXT("limit"), Cd.Limit}, {TEXT("balance"), Cd.Balance}, {TEXT("available"), FMath::Max<int64>(0,Cd.Limit-Cd.Balance)}, {TEXT("stopped_suffix"), Cd.Stopped?OKT(TEXT("ui.bank.card_stopped_suffix")):TEXT("")}}),Gold,13,true));
   if(Cd.DueDay>=0&&Cd.Statement>0)Line(OKFinText(OKMsg(TEXT("ui.bank.statement_due_notice"), {{TEXT("statement"), Cd.Statement}, {TEXT("min_due"), Cd.MinDue}, {TEXT("due_date"), *OKStreetCalendarDateTime(Cd.DueDay,8.0).ToString(TEXT("%m/%d"))}}),FLinearColor::White,11));
   Line(OKFinText(OKMsg(TEXT("ui.bank.late_record_notice"), {{TEXT("late_count"), Cd.Late}}),Dim,11));
   auto Row=SNew(SWrapBox).UseAllottedSize(true);
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.bank.pay_all_button")),[Weak](){Weak->BankAction(TEXT("card_pay"),-1,0);},Cd.Balance>0)];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.bank.pay_min_due"), {{TEXT("min_due"), Cd.MinDue}}),[Weak](){Weak->BankAction(TEXT("card_pay"),Weak->State->Store.Banking.Card.MinDue,0);},Cd.MinDue>0)];
   Row->AddSlot().Padding(2)[OKFinText(OKT(TEXT("ui.bank.custom_amount_prefix")))];Row->AddSlot().Padding(2)[Amount()];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.bank.pay_button")),[Weak](){Weak->BankAction(TEXT("card_pay"),Weak->MoneyIn(Weak->State->BankAmount),0);})];
   Line(Row);
  }
 }else if(Tab==TEXT("fees")){
  FOKStreetTerm T;const auto& A=S.Store.PlayerAcademic;EOKStreetTermPhase Ph;bool InWindow=false;OKRulesTermAt(S,Today,Ph,&T,&InWindow);
  if(InWindow)Line(OKFinText(OKMsg(TEXT("ui.bank.term_fee_line"), {{TEXT("term"), *T.Key}, {TEXT("tuition"), Money(TEXT("tuition"))}, {TEXT("dorm"), Money(TEXT("dorm"))}, {TEXT("total"), Money(TEXT("tuition"))+Money(TEXT("dorm"))}, {TEXT("paid_status"), A.FeesPaidTerm==T.Key?OKT(TEXT("ui.bank.paid_label")):A.Status!=TEXT("enrolled")?OKT(TEXT("ui.bank.no_fee_not_student")):OKT(TEXT("ui.bank.unpaid_label"))}, {TEXT("deadline"), *(T.Start+FTimespan::FromDays(14)).ToString(TEXT("%m/%d"))}}),Gold,13,true));
  else Line(OKFinText(OKT(TEXT("ui.bank.not_tuition_period_notice")),Dim,12));
  auto Row=SNew(SWrapBox).UseAllottedSize(true);
  Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.bank.pay_tuition_dorm")),[Weak](){Weak->BankAction(TEXT("fees_pay"),0,0);})];
  Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.bank.pay_with_student_loan")),[Weak](){Weak->BankAction(TEXT("loan_apply"),0,0);})];
  Line(Row);
  Line(OKFinText(OKMsg(TEXT("ui.bank.phone_plan_line"), {{TEXT("phone_fee"), Money(TEXT("phone_fee"))}, {TEXT("arrears"), B.PhoneArrears}}),FLinearColor::White,12));
  if(B.PhoneArrears>0)Line(OKFinButton(Weak,OKMsg(TEXT("ui.bank.pay_phone_arrears"), {{TEXT("arrears"), B.PhoneArrears}}),[Weak](){Weak->BankAction(TEXT("phone_pay"),0,0);}));
  Line(OKFinText(OKT(TEXT("ui.bank.scholarship_rules_note")),Dim,11));
  Line(OKFinText(OKMsg(TEXT("ui.bank.student_loan_status"), {{TEXT("owed"), B.Loan.Owed}, {TEXT("taken_this_year"), B.Loan.TakenThisYear}, {TEXT("year_cap"), Money(TEXT("loan_year_cap"))}}),Dim,11));
 }else if(Tab==TEXT("worth")){
  TArray<TPair<FString,int64>> Parts;const int64 Total=NetWorthOf(S.Store,&Parts);
  Line(OKFinText(OKMsg(TEXT("ui.bank.total_assets_amount"), {{TEXT("total"), Total}}),Gold,16,true));
  for(const auto& P:Parts)Line(OKFinText(OKMsg(TEXT("ui.bank.signed_amount_line"), {{TEXT("name"), *P.Key}, {TEXT("sign"), P.Value<0?TEXT("−"):TEXT("")}, {TEXT("amount"), FMath::Abs(P.Value)}}),P.Value<0?FLinearColor(1,.45f,.45f):FLinearColor::White,11));
  auto Range=SNew(SHorizontalBox);
  for(const int32 N:{30,365,0})Range->AddSlot().AutoWidth().Padding(2)[OKFinButton(Weak,N==30?OKT(TEXT("ui.bank.range_thirty_days")):N==365?OKT(TEXT("ui.bank.range_one_year")):OKT(TEXT("ui.bank.all_label")),[Weak,N](){Weak->State->WorthRange=N;Weak->PopulateApp();Weak->RefreshUI();},true,S.WorthRange==N?FLinearColor(1,.85f,.3f):FLinearColor::White)];
  Line(Range);
  if(!S.WorthChart)S.WorthChart=MakeShared<FOKChartData>();
  auto& Ch=*S.WorthChart;Ch=FOKChartData();Ch.Line=true;Ch.Title=OKT(TEXT("ui.bank.daily_total_assets"));
  const int32 From=S.WorthRange>0?S.Store.CalendarDay-S.WorthRange:0;
  for(const auto& W:B.NetWorth)if(W.Key>=From){const float V=float(W.Value);Ch.O.Add(V);Ch.H.Add(V);Ch.L.Add(V);Ch.C.Add(V);Ch.V.Add(0);Ch.Dates.Add(OKStreetCalendarDateTime(W.Key,8.0).ToString(TEXT("%Y-%m-%d")));}
  Ch.C.Add(float(Total));Ch.O.Add(float(Total));Ch.H.Add(float(Total));Ch.L.Add(float(Total));Ch.V.Add(0);Ch.Dates.Add(OKT(TEXT("ui.bank.now_label")));
  Ch.SessionPoints=Ch.C.Num();Ch.PrevClose=Ch.C[0];
  Box->AddSlot().AutoHeight().Padding(2)[SNew(SBox).HeightOverride(300)[SNew(SOKMarketChart).Data(S.WorthChart)]];
 }else if(Tab==TEXT("alerts")){
  Line(OKFinText(OKT(TEXT("ui.bank.price_alert_note")),Gold,13,true));
  for(const auto& V:OKFinArr(S.FinAlerts,TEXT("alerts"))){const auto X=V->AsObject();const int64 AId=int64(OKFinNum(X,TEXT("id")));const FString K=OKFinStr(X,TEXT("kind"));
   Line(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[OKFinText(OKMsg(TEXT("ui.appointments.appointment_row"), {{TEXT("mark"), OKFinStr(X,TEXT("market"))==TEXT("cn")?OKT(TEXT("ui.bank.a_shares_label")):OKT(TEXT("ui.bank.us_stocks_label"))}, {TEXT("label"), *OKFinStr(X,TEXT("code"))}, {TEXT("when"), *(K==TEXT("above")?OKMsg(TEXT("ui.bank.rose_to_price"), {{TEXT("price"), OKFinNum(X,TEXT("value"))}}):K==TEXT("below")?OKMsg(TEXT("ui.bank.fell_to_price"), {{TEXT("price"), OKFinNum(X,TEXT("value"))}}):K==TEXT("pct")?OKMsg(TEXT("ui.bank.daily_move_alert"), {{TEXT("threshold_pct"), OKFinNum(X,TEXT("value"))}}):K==TEXT("limit_up")?FString(OKT(TEXT("ui.bank.limit_up_label"))):FString(OKT(TEXT("ui.bank.limit_down_label"))))}}),FLinearColor::White,11)]
    +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.bank.delete_button")),[Weak,AId](){FString E;auto A=MakeShared<FJsonObject>();A->SetNumberField(TEXT("id"),double(AId));Weak->Fin(TEXT("alert_del"),A,E);Weak->State->FinAlerts=Weak->Fin(TEXT("alerts"),nullptr,E);Weak->PopulateApp();Weak->RefreshUI();})]);}
  if(OKFinArr(S.FinAlerts,TEXT("alerts")).IsEmpty())Line(OKFinText(OKT(TEXT("ui.bank.no_alerts_yet")),Dim,11));
 }else return SNullWidget::NullWidget;
 Line(OKFinText(OKT(TEXT("ui.bank.recent_bank_notices")),Gold,12,true));
 for(int32 K=B.Notes.Num()-1;K>=FMath::Max(0,B.Notes.Num()-8);--K)Line(OKFinText(B.Notes[K],Dim,10));
 return SNew(SScrollBox)+SScrollBox::Slot()[Box];
}

// ------------------------------------------------------------------------------------------------ the father he never met

// Two acts. 1) Broke — what he owns no longer covers what he owes (the student loan aside): the bank liquidates
// (time deposits closed, wallet and savings taken for the card and the phone), the card is stopped, word gets round,
// and a full-screen bankruptcy scene plays. Only his lovers learn of it (and of the rescue); nobody else hears. 2) The moment the scene closes, a father he never met — who has been
// watching over him all along — texts from an unknown number and sends the next multiple of ¥100,000 above what he
// still owed. Never before the bankruptcy.
void AOKStreetSocialScene::CheckBankruptcy(){
 auto& S=*State;if(S.SelfTest||!S.Mind)return;
 const int64 Now=OKStreetMindNow(S);const auto& B=S.Store.Banking;
 if(B.Bankrupt){
  if(Now/10!=S.BankruptLogTick){S.BankruptLogTick=Now/10;UE_LOG(LogTemp,Display,TEXT("Street bankrupt wait: now %lld rescue %lld app=%s"),Now,B.RescueAt,*S.App);}
  if(B.RescueAt>=0&&Now>=B.RescueAt&&S.App!=TEXT("bankrupt"))FatherRescue();
  return;}
 if(Now/60==S.BankruptCheckHour)return;S.BankruptCheckHour=Now/60;
 if(NetWorthOf(S.Store)+B.Loan.Owed<0)GoBankrupt();
}

// Liquidation and what his lovers make of it are rules (bank.rs bank_bankrupt); here the scene.
TSharedRef<FJsonObject> AOKStreetSocialScene::RulesLovers() const{
 const auto& S=*State;auto Request=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Lovers;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){if(!ContactLover(I))continue;const FString Id=OKStreetStableId(S.Manifest.Characters[I]);auto L=MakeShared<FJsonObject>();
  L->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget)));if(const auto P=ProfileOf(I))L->SetObjectField(TEXT("profile"),P.ToSharedRef());L->SetStringField(TEXT("name"),S.Manifest.Characters[I].Name);
  Lovers.Add(MakeShared<FJsonValueObject>(L));}
 Request->SetArrayField(TEXT("lovers"),Lovers);return Request;
}

void AOKStreetSocialScene::GoBankrupt(){
 auto& S=*State;
 auto Request=RulesLovers();Request->SetStringField(TEXT("rule"),TEXT("bank_bankrupt"));
 Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);Request->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));Request->SetNumberField(TEXT("player_cash"),double(S.Store.PlayerCash));Request->SetNumberField(TEXT("player_bank"),double(S.Store.PlayerBank));
 Request->SetNumberField(TEXT("net_worth"),double(NetWorthOf(S.Store)));Request->SetStringField(TEXT("player_name"),S.Manifest.Characters.IsValidIndex(0)?S.Manifest.Characters[0].Name:FString());
 Request->SetObjectField(TEXT("banking"),OKStreetBankingJson(S.Store.Banking));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 auto C=S.Store;ApplyRulesEffects(C,*Answer);OKRulesReadBank(C,*Answer);
 if(!CommitStore(MoveTemp(C)))return;
 FlushRulesEffects();
 S.BankruptLines.Reset();for(const auto& V:Answer->GetArrayField(TEXT("lines")))S.BankruptLines.Add(V->AsString());
 S.BankruptShowAt=FPlatformTime::Seconds();
 UE_LOG(LogTemp,Display,TEXT("Street bankrupt: short %lld (bankruptcy #%d), rescue at minute %lld"),S.Store.Banking.BankruptShortfall,S.Store.Banking.BankruptCount,S.Store.Banking.RescueAt);
 OpenApp(TEXT("bankrupt"),false);
}

TSharedRef<SWidget> AOKStreetSocialScene::BuildBankruptApp(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Box=SNew(SVerticalBox);const double Step=1.8;
 auto Shown=[Weak,Step](int32 K){return TAttribute<EVisibility>::CreateLambda([Weak,Step,K](){return Weak.IsValid()&&FPlatformTime::Seconds()-Weak->State->BankruptShowAt>=Step*K?EVisibility::Visible:EVisibility::Hidden;});};
 Box->AddSlot().AutoHeight().Padding(0,30,0,20)[SNullWidget::NullWidget];
 for(int32 K=0;K<S.BankruptLines.Num();++K){const bool Last=K+1==S.BankruptLines.Num();
  Box->AddSlot().AutoHeight().Padding(40,8).HAlign(HAlign_Left)[SNew(SBox).Visibility(Shown(K))[OKFinText(S.BankruptLines[K],K==4?FLinearColor(1,.35f,.35f):Last?FLinearColor(.85f,.85f,.9f):FLinearColor(.95f,.95f,.95f),Last?14:16,K==4)]];}
 Box->AddSlot().AutoHeight().Padding(0,40).HAlign(HAlign_Center)[SNew(SBox).Visibility(Shown(S.BankruptLines.Num()+1))[OKFinText(OKT(TEXT("ui.bank.bankrupt_label")),FLinearColor(.9f,.1f,.1f),72,true)]];
 Box->AddSlot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).Visibility(Shown(S.BankruptLines.Num()+2))[OKFinText(OKT(TEXT("ui.bank.press_esc_hint")),FLinearColor(.6f,.6f,.6f),11)]];
 return SNew(SScrollBox)+SScrollBox::Slot()[Box];
}

// The father he never met: his gift, the text, what his lovers learn (bank.rs bank_father_rescue).
void AOKStreetSocialScene::FatherRescue(){
 auto& S=*State;
 auto Request=RulesLovers();Request->SetStringField(TEXT("rule"),TEXT("bank_father_rescue"));
 Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);Request->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));Request->SetNumberField(TEXT("player_bank"),double(S.Store.PlayerBank));
 Request->SetObjectField(TEXT("banking"),OKStreetBankingJson(S.Store.Banking));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 auto C=S.Store;ApplyRulesEffects(C,*Answer);OKRulesReadBank(C,*Answer);
 if(!CommitStore(MoveTemp(C)))return;
 FlushRulesEffects();
 S.Message=Answer->GetStringField(TEXT("message"));S.FinNotice=S.Message;
 UE_LOG(LogTemp,Display,TEXT("Street father: gift %lld for shortfall %lld (gift #%d)"),int64(Answer->GetNumberField(TEXT("gift"))),S.Store.Banking.BankruptShortfall,S.Store.Banking.FatherGifts);
 RefreshUI();
}

// Price alerts on the stock shown in a trading screen.
void AOKStreetSocialScene::AddAlert(const FString& Kind){
 auto& S=*State;FString Error;auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("market"),S.App);A->SetStringField(TEXT("code"),S.FinCode);A->SetStringField(TEXT("type"),Kind);
 A->SetNumberField(TEXT("value"),Kind==TEXT("pct")?(S.AlertValue.IsEmpty()?5.0:FCString::Atod(*S.AlertValue)):FCString::Atod(*S.AlertValue));
 if(Fin(TEXT("alert_add"),A,Error))S.FinNotice=OKT(TEXT("ui.bank.alert_set_done"));else S.FinNotice=Error.Replace(TEXT("alert: "),TEXT(""));
 S.FinAlerts=Fin(TEXT("alerts"),nullptr,Error);PopulateApp();RefreshUI();
}
