// The accounts app (phone: 會計帳簿, Docs/STREET_ACCOUNTING_PLAN_20261012.md): the journal of each book, the income
// statement, the cash flow, the balance sheet, a CSV export. Included after the company app (its widgets and rules).

namespace {
int32 OKAccMonthOf(int32 Day){const FDateTime D=OKStreetCalendarDateTime(Day,12.0);return D.GetYear()*12+D.GetMonth()-1;}
FString OKAccMonthLabel(int32 Key){return OKMsg(TEXT("ui.accounts_app.year_month_label"), {{TEXT("year"), Key/12}, {TEXT("month"), Key%12+1}});}
FString OKAccMoney(int64 V){return OKStreetMoneyText(V);}
// The accounts that are money in hand for the cash flow.
bool OKAccIsCash(const FString& Book,const FString& Account){return Account==TEXT("cash")||(Book==TEXT("private")&&Account==TEXT("bank"));}
}

// What a company is worth to its owner is the rules' (estate.rs company_view equity; CompanyEquity in Company.inl).

FString AOKStreetSocialScene::BookTitle(const FString& Book) const{
 const auto& C=State->Store;if(Book==TEXT("private"))return OKT(TEXT("ui.accounts_app.personal_account_label"));
 if(Book==TEXT("company"))return C.Company.Founded>=0?OKMsg(TEXT("ui.accounts_app.company_account_named"), {{TEXT("company_name"), *C.Company.Name}}):FString(OKT(TEXT("ui.accounts_app.company_account_label")));
 for(const auto& H:C.HeroCompanies)if(TEXT("hc_")+H.Owner==Book)return OKMsg(TEXT("ui.accounts_app.quoted_name"), {{TEXT("name"), *H.Name}});
 return Book;
}

// The whole book as CSV (opens in a spreadsheet: UTF-8 with BOM), next to the journal.
FString AOKStreetSocialScene::ExportBook(const FString& Book){
 FString Path=BookPath(Book);if(Path.IsEmpty())Path=FPaths::ProjectSavedDir()/TEXT("StreetSocial/accounts")/(Book+TEXT(".jsonl"));
 Path=FPaths::ChangeExtension(Path,TEXT("csv"));
 auto Q=[](const FString& V){return TEXT("\"")+V.Replace(TEXT("\""),TEXT("\"\""))+TEXT("\"");};
 FString Out=OKT(TEXT("ui.accounts_app.csv_header"));
 for(const auto& E:BookOf(Book)){const FDateTime T=OKStreetCalendarDateTime(E.Day,double(E.Minute%1440)/60.0);
  Out+=FString::Printf(TEXT("%s,%s,%s,%s,%s,%s,%lld,%lld\n"),*T.ToString(TEXT("%Y-%m-%d")),*T.ToString(TEXT("%H:%M")),*Q(OKBookAccountLabel(Book,E.Account)),*Q(OKBookCategoryLabel(E.Category)),*OKBookCategoryKind(E.Category),*Q(E.Note),E.Amount,E.Balance);}
 return FFileHelper::SaveStringToFile(Out,*Path,FFileHelper::EEncodingOptions::ForceUTF8)?Path:FString();
}

TSharedRef<SWidget> AOKStreetSocialScene::BuildAccountsApp(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 S.BooksTickAt=0;TickBooks(); // everything up to now is in the books
 const auto& C=S.Store;const int32 Now=OKAccMonthOf(C.CalendarDay);if(S.AccMonth<=0)S.AccMonth=Now;
 TArray<FString> Books={TEXT("private"),TEXT("company")};for(const auto& H:C.HeroCompanies)Books.Add(TEXT("hc_")+H.Owner);
 if(!Books.Contains(S.AccBook))S.AccBook=TEXT("private");
 const FString Book=S.AccBook;const TArray<FOKBookEntry>& List=BookOf(Book);const int32 Month=S.AccMonth;
 auto Box=SNew(SVerticalBox);auto Line=[&](TSharedRef<SWidget> W){Box->AddSlot().AutoHeight().Padding(2)[W];};
 const FLinearColor Gold(1,.85f,.3f),Dim(.7f,.7f,.75f),Plus(.6f,1,.6f),Minus(1,.6f,.5f);
 // Rows of buttons: a horizontal box (a wrap box in the scroll box asks for far too much height).
 auto Pick=[&](const FString& Label,bool On,TFunction<void()> Do){return OKFinButton(Weak,Label,[Weak,Do](){Do();Weak->RefreshUI();},true,On?Gold:FLinearColor::White);};
 {auto Row=SNew(SHorizontalBox);
  for(const FString& B:Books)Row->AddSlot().AutoWidth().Padding(2)[Pick(BookTitle(B),B==Book,[Weak,B](){Weak->State->AccBook=B;Weak->State->AccAccount.Reset();})];
  Line(Row);}
 {auto Row=SNew(SHorizontalBox);
  for(const auto& T:TArray<TPair<FString,FString>>{{TEXT("journal"),OKT(TEXT("ui.accounts_app.detail_label"))},{TEXT("pl"),OKT(TEXT("ui.accounts_app.income_statement"))},{TEXT("cash"),OKT(TEXT("ui.accounts_app.cash_flow_statement"))},{TEXT("balance"),OKT(TEXT("ui.accounts_app.balance_sheet"))}}){const FString Id=T.Key;
   Row->AddSlot().AutoWidth().Padding(2)[Pick(T.Value,S.AccTab==Id,[Weak,Id](){Weak->State->AccTab=Id;})];}
  Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(12,2,2,2)[OKFinButton(Weak,TEXT("◀"),[Weak](){--Weak->State->AccMonth;Weak->RefreshUI();})];
  Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2)[OKFinText(OKAccMonthLabel(Month),Gold,13,true)];
  Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2)[OKFinButton(Weak,TEXT("▶"),[Weak](){++Weak->State->AccMonth;Weak->RefreshUI();})];
  Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.accounts_app.this_month_label")),[Weak](){Weak->State->AccMonth=0;Weak->RefreshUI();})];
  Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(12,2,2,2)[OKFinButton(Weak,OKT(TEXT("ui.accounts_app.export_csv_button")),[Weak,Book](){const FString P=Weak->ExportBook(Book);Weak->State->FinNotice=P.IsEmpty()?FString(OKT(TEXT("ui.accounts_app.export_failed"))):OKT(TEXT("ui.accounts_app.exported_prefix"))+P;Weak->RefreshUI();})];
  Line(Row);}
 // Columns of a table row.
 auto Cells=[&](const TArray<TPair<FString,float>>& Cols,const FLinearColor& Color,int32 Size=10,bool Bold=false){auto Row=SNew(SHorizontalBox);
  for(const auto& Cell:Cols){if(Cell.Value<=0)Row->AddSlot().FillWidth(1).Padding(4,0)[OKFinText(Cell.Key,Color,Size,Bold)];else Row->AddSlot().AutoWidth().Padding(4,0)[SNew(SBox).WidthOverride(Cell.Value)[OKFinText(Cell.Key,Color,Size,Bold)]];}
  return Row;};
 TArray<const FOKBookEntry*> InMonth;for(const auto& E:List)if(OKAccMonthOf(E.Day)==Month)InMonth.Add(&E);
 if(Book==TEXT("private")){const auto& P=C.Personal;
  Line(OKFinText(OKMsg(TEXT("ui.accounts_app.yearly_income_summary"), {{TEXT("year"), P.TaxYear>0?P.TaxYear:OKStreetCalendarDateTime(S.Store.CalendarDay,12.0).GetYear()}, {TEXT("salary"), *OKAccMoney(P.YtdIncome)}, {TEXT("bonus"), *OKAccMoney(P.YtdBonus)}, {TEXT("social_insurance"), *OKAccMoney(P.YtdSocial)}, {TEXT("income_tax"), *OKAccMoney(P.YtdTax)}, {TEXT("housing_fund"), *OKAccMoney(P.HousingFund)}}),FLinearColor(.75f,.85f,1),10));}
 if(S.AccTab==TEXT("journal")){
  TSet<FString> Accounts;for(const auto* E:InMonth)Accounts.Add(E->Account);
  auto Row=SNew(SHorizontalBox);Row->AddSlot().AutoWidth().Padding(2)[Pick(OKT(TEXT("ui.accounts_app.all_accounts_label")),S.AccAccount.IsEmpty(),[Weak](){Weak->State->AccAccount.Reset();})];
  for(const FString& A:Accounts)Row->AddSlot().AutoWidth().Padding(2)[Pick(OKBookAccountLabel(Book,A),S.AccAccount==A,[Weak,A](){Weak->State->AccAccount=A;})];
  Line(Row);
  Line(Cells({{OKT(TEXT("ui.accounts_app.date_label")),100},{OKT(TEXT("ui.accounts_app.account_label")),80},{OKT(TEXT("ui.accounts_app.account_item_label")),110},{OKT(TEXT("ui.accounts_app.description_label")),0},{OKT(TEXT("ui.accounts_app.amount_label")),110},{OKT(TEXT("ui.accounts_app.balance_label")),120}},Gold,11,true));
  int32 Shown=0;
  for(int32 K=InMonth.Num()-1;K>=0&&Shown<500;--K){const FOKBookEntry& E=*InMonth[K];if(!S.AccAccount.IsEmpty()&&E.Account!=S.AccAccount)continue;++Shown;
   const FDateTime T=OKStreetCalendarDateTime(E.Day,double(E.Minute%1440)/60.0);
   Line(Cells({{T.ToString(TEXT("%m/%d %H:%M")),100},{OKBookAccountLabel(Book,E.Account),80},{OKBookCategoryLabel(E.Category),110},{E.Note,0},{OKAccMoney(E.Amount),110},{OKAccMoney(E.Balance),120}},
    E.Category==TEXT("unclassified")?FLinearColor(1,.9f,.3f):E.Amount<0?Minus:Plus));}
  if(Shown==0)Line(OKFinText(OKT(TEXT("ui.accounts_app.no_records_this_month")),Dim));
 }
 else if(S.AccTab==TEXT("pl")){
  // Income and expenses by category: this month and the year so far (only what changes the owner's wealth).
  const int32 YearStart=Month/12*12;TMap<FString,int64> Mon,Ytd;
  for(const auto& E:List){if(OKBookCategoryKind(E.Category)!=TEXT("pl"))continue;const int32 M=OKAccMonthOf(E.Day);if(M==Month)Mon.FindOrAdd(E.Category)+=E.Amount;if(M>=YearStart&&M<=Month)Ytd.FindOrAdd(E.Category)+=E.Amount;}
  TArray<FString> Cats;Ytd.GetKeys(Cats);Cats.Sort([&](const FString& A,const FString& B){return Ytd[A]>Ytd[B];});
  Line(Cells({{OKT(TEXT("ui.accounts_app.account_item_label")),0},{OKAccMonthLabel(Month),150},{OKMsg(TEXT("ui.accounts_app.cumulative_years_label"), {{TEXT("years"), Month/12}}),150}},Gold,11,true));
  int64 MonIn=0,MonOut=0,YtdIn=0,YtdOut=0;
  for(int32 Pass=0;Pass<2;++Pass){Line(OKFinText(Pass==0?OKT(TEXT("ui.accounts_app.income_label")):OKT(TEXT("ui.accounts_app.expense_label")),Gold,12,true));
   for(const FString& Cat:Cats){const int64 Y=Ytd[Cat],M=Mon.FindRef(Cat);if((Pass==0)!=(Y>0)||Y==0&&M==0)continue;
    Line(Cells({{OKT(TEXT("ui.accounts_app.ideographic_space"))+OKBookCategoryLabel(Cat),0},{OKAccMoney(M),150},{OKAccMoney(Y),150}},Pass==0?Plus:Minus));
    if(Pass==0){MonIn+=M;YtdIn+=Y;}else{MonOut+=M;YtdOut+=Y;}}}
  Line(Cells({{OKT(TEXT("ui.accounts_app.period_profit_label")),0},{OKAccMoney(MonIn+MonOut),150},{OKAccMoney(YtdIn+YtdOut),150}},MonIn+MonOut>=0?Plus:Minus,12,true));
  // Not income or expense: shown apart so nothing is hidden.
  TMap<FString,int64> Other;for(const auto* E:InMonth){const FString K=OKBookCategoryKind(E->Category);if(K!=TEXT("pl")&&K!=TEXT("transfer")&&OKAccIsCash(Book,E->Account))Other.FindOrAdd(E->Category)+=E->Amount;}
  if(!Other.IsEmpty()){Line(OKFinText(OKT(TEXT("ui.accounts_app.non_pnl_cash_note")),Dim,11,true));for(const auto& O:Other)Line(Cells({{OKT(TEXT("ui.accounts_app.ideographic_space"))+OKBookCategoryLabel(O.Key),0},{OKAccMoney(O.Value),150},{FString(),150}},Dim));}
 }
 else if(S.AccTab==TEXT("cash")){
  // Money in hand at the start of the month, what moved it by kind, and at the end.
  TMap<FString,int64> Last;for(const auto& E:List){if(OKAccMonthOf(E.Day)>=Month)break;Last.Add(E.Account,E.Balance);}
  int64 Open=0;for(const auto& L:Last)if(OKAccIsCash(Book,L.Key))Open+=L.Value;
  TMap<FString,int64> ByKind;TMap<FString,TMap<FString,int64>> ByCat;
  for(const auto* E:InMonth){if(!OKAccIsCash(Book,E->Account))continue;FString K=OKBookCategoryKind(E->Category);if(K==TEXT("opening")||K==TEXT("adjust"))K=TEXT("transfer");ByKind.FindOrAdd(K)+=E->Amount;ByCat.FindOrAdd(K).FindOrAdd(E->Category)+=E->Amount;}
  Line(Cells({{OKT(TEXT("ui.accounts_app.opening_cash_label")),0},{OKAccMoney(Open),150}},Gold,12,true));
  int64 Close=Open;
  for(const auto& K:TArray<TPair<FString,FString>>{{TEXT("pl"),OKT(TEXT("ui.accounts_app.operating_activities"))},{TEXT("investing"),OKT(TEXT("ui.accounts_app.investing_activities"))},{TEXT("financing"),OKT(TEXT("ui.accounts_app.financing_activities"))},{TEXT("transfer"),OKT(TEXT("ui.accounts_app.inter_account_transfer_label"))}}){
   const int64 V=ByKind.FindRef(K.Key);Close+=V;Line(Cells({{K.Value,0},{OKAccMoney(V),150}},V>=0?Plus:Minus,11,true));
   if(const auto* Cats=ByCat.Find(K.Key))for(const auto& X:*Cats)Line(Cells({{OKT(TEXT("ui.accounts_app.ideographic_space"))+OKBookCategoryLabel(X.Key),0},{OKAccMoney(X.Value),150}},Dim));}
  Line(Cells({{OKT(TEXT("ui.accounts_app.closing_cash_label")),0},{OKAccMoney(Close),150}},Gold,12,true));
 }
 else{
  auto Sheet=[&](const TCHAR* Title,const TArray<TPair<FString,int64>>& Rows){int64 T=0;Line(OKFinText(Title,Gold,12,true));for(const auto& R:Rows){T+=R.Value;Line(Cells({{OKT(TEXT("ui.accounts_app.ideographic_space"))+R.Key,0},{OKAccMoney(R.Value),160}},R.Value<0?Minus:FLinearColor::White));}return T;};
  if(Book==TEXT("private")){
   TArray<TPair<FString,int64>> Parts;NetWorthOf(C,&Parts);TArray<TPair<FString,int64>> Assets,Debts;
   for(const auto& P:Parts)(P.Value>=0?Assets:Debts).Add(P);
   Assets.Add({OKT(TEXT("ui.accounts_app.housing_fund_account_note")),C.Personal.HousingFund});
   const int64 A=Sheet(OKT(TEXT("ui.accounts_app.assets_label")),Assets),L=Sheet(OKT(TEXT("ui.accounts_app.liabilities_label")),Debts);
   Line(Cells({{OKT(TEXT("ui.accounts_app.net_assets_label")),0},{OKAccMoney(A+L),160}},Gold,13,true));
  }else{
   const FOKStreetEstate* E=Book==TEXT("company")?&C.Company.Estate:nullptr;
   for(const auto& H:C.HeroCompanies)if(TEXT("hc_")+H.Owner==Book)E=&H.Estate;
   if(E){const TSharedPtr<FJsonObject> View=CompanyView(C);
    auto Value=[&](const FString& Unit){if(View.IsValid()){TArray<TSharedPtr<FJsonValue>> All=View->GetArrayField(TEXT("holdings"));for(const auto& Hc:View->GetArrayField(TEXT("hero_companies")))All.Append(Hc->AsObject()->GetArrayField(TEXT("holdings")));
     for(const auto& V:All)if(V->AsObject()->GetStringField(TEXT("unit"))==Unit)return int64(V->AsObject()->GetNumberField(TEXT("value")));}return int64(0);};
    TArray<TPair<FString,int64>> Assets,Debts;Assets.Add({OKT(TEXT("ui.accounts_app.company_accounts_label")),E->Cash});
    for(const auto& H:E->Holdings){if(H.Mode==TEXT("own"))Assets.Add({UnitOf(H.Unit).Name+OKT(TEXT("ui.accounts_app.market_value_note")),Value(H.Unit)});if(H.Mode==TEXT("rent")&&H.Deposit>0)Assets.Add({UnitOf(H.Unit).Name+OKT(TEXT("ui.accounts_app.deposit_label")),H.Deposit});
     if(H.Loan>0)Debts.Add({UnitOf(H.Unit).Name+OKT(TEXT("ui.accounts_app.mortgage_label")),-H.Loan});}
    if(E->Debt>0)Debts.Add({OKT(TEXT("ui.accounts_app.unpaid_wages_label")),-int64(E->Debt)});if(E->OwnerLoan>0)Debts.Add({OKT(TEXT("ui.accounts_app.shareholder_loan_label")),-E->OwnerLoan});
    if(Book==TEXT("company"))for(const auto& H:C.HeroCompanies)for(const auto& X:H.Estate.Holdings)if(X.Landlord==TEXT("player")&&X.Deposit>0)Debts.Add({OKMsg(TEXT("ui.accounts_app.quoted_name_deposit"), {{TEXT("name"), *H.Name}}),-X.Deposit});
    const int64 A=Sheet(OKT(TEXT("ui.accounts_app.assets_label")),Assets),L=Sheet(OKT(TEXT("ui.accounts_app.liabilities_label")),Debts);
    Line(OKFinText(OKT(TEXT("ui.accounts_app.equity_label")),Gold,12,true));
    Line(Cells({{OKT(TEXT("ui.accounts_app.paid_in_capital_label")),0},{OKAccMoney(E->Capital),160}},FLinearColor::White));
    Line(Cells({{OKT(TEXT("ui.accounts_app.retained_earnings_label")),0},{OKAccMoney(E->Retained),160}},FLinearColor::White));
    Line(Cells({{OKT(TEXT("ui.accounts_app.property_gain_other_label")),0},{OKAccMoney(A+L-E->Capital-E->Retained),160}},Dim));
    Line(Cells({{OKT(TEXT("ui.accounts_app.net_worth_short_label")),0},{OKAccMoney(A+L),160}},Gold,13,true));}
  }
 }
 return SNew(SScrollBox)+SScrollBox::Slot()[Box];
}

// ------------------------------------------------------------------------------------------------ tests

// The journal files as they are (a test changes the live books, then puts them back).
TMap<FString,FString> AOKStreetSocialScene::BooksSnapshot(){
 TMap<FString,FString> Out;const FString Dir=FPaths::GetPath(BookPath(TEXT("private")));if(Dir.IsEmpty())return Out;
 TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(Dir/TEXT("*.jsonl")),true,false);
 for(const FString& F:Files){FString Text;FFileHelper::LoadFileToString(Text,*(Dir/F));Out.Add(Dir/F,Text);}
 return Out;
}
void AOKStreetSocialScene::BooksRestore(const TMap<FString,FString>& Files){
 auto& S=*State;const FString Dir=FPaths::GetPath(BookPath(TEXT("private")));
 if(!Dir.IsEmpty()){TArray<FString> Now;IFileManager::Get().FindFiles(Now,*(Dir/TEXT("*.jsonl")),true,false);for(const FString& F:Now)if(!Files.Contains(Dir/F))IFileManager::Get().Delete(*(Dir/F));}
 for(const auto& F:Files)FFileHelper::SaveStringToFile(F.Value,*F.Key,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 S.Books.Reset();S.BooksLoaded.Reset();S.BookSeen.Reset();S.Store.BookPending.Reset();
}

// Development: -StreetSocialAccountsTest runs the books on the live save (store and journal files put back at the
// end): the capital moves into the company, a shortfall is lent by him, the loan comes back, a bonus (one-time and
// cumulative), a dividend (and one past the profit refused), a shift, a time deposit, a lottery ticket; then every
// book's balances match the accounts, nothing is unclassified, the income statement adds up, CSV, save round trip.
// "ACCOUNTS TEST" lines, then the game quits.
void AOKStreetSocialScene::TickAccountsTest(){
 auto& S=*State;if(!S.AllReady||!FParse::Param(FCommandLine::Get(),TEXT("StreetSocialAccountsTest")))return;
 static int32 Step=0,Passed=0,Failed=0;static double At=0;static FOKStreetStore Saved;static TMap<FString,FString> Files;static int64 Mark=0,Tax=0;static FString Shots;
 const double Now=SceneTime();if(At==0)At=Now+6.0;if(Now<At)return;At=Now+0.6;
 auto Check=[&](const TCHAR* Name,bool Ok,const FString& Detail){UE_LOG(LogTemp,Display,TEXT("ACCOUNTS TEST %s: %s %s"),Name,Ok?TEXT("PASS"):TEXT("FAIL"),*Detail);if(Ok)++Passed;else ++Failed;};
 auto Count=[&](const FString& Book,const FString& Cat){int32 N=0;for(const auto& E:BookOf(Book))if(E.Serial>Mark&&E.Category==Cat)++N;return N;};
 auto Sum=[&](const FString& Book,const FString& Account,const FString& Cat){int64 N=0;for(const auto& E:BookOf(Book))if(E.Serial>Mark&&E.Account==Account&&E.Category==Cat)N+=E.Amount;return N;};
 auto& E=S.Store.Company.Estate;
 switch(Step){
 case 0:{S.BooksTickAt=0;TickBooks();Saved=S.Store;Files=BooksSnapshot();Mark=OKBookNextSerial();
  auto C=S.Store;C.HeroCompanies.Reset();C.Company=FOKStreetCompany();C.PlayerBank=300000;C.PlayerCash=2000;C.Personal=FOKStreetPersonal();OKBookAdjustTo(S.Store,C);CommitStore(MoveTemp(C));
  Step=1;break;}
 case 1:{const int64 Bank=S.Store.PlayerBank;FoundCompany(OKT(TEXT("ui.accounts_app.ledger_test_label")),TEXT("cafe"));
  Check(TEXT("capital_moves"),E.Cash==50000&&E.Capital==50000&&S.Store.PlayerBank==Bank-50000,FString::Printf(TEXT("company %lld bank %lld->%lld"),E.Cash,Bank,S.Store.PlayerBank));Step=2;break;}
 case 2:{Check(TEXT("capital_journaled"),Sum(TEXT("private"),TEXT("bank"),TEXT("capital"))==-50000&&Sum(TEXT("company"),TEXT("cash"),TEXT("capital"))==50000,FString());
  auto C=S.Store;const bool Paid=OKStreetCompanyPay(C,80000,TEXT("overhead"),OKT(TEXT("ui.accounts_app.test_expense_label")));CommitStore(MoveTemp(C));
  Check(TEXT("owner_lends"),Paid&&E.OwnerLoan==30000&&E.Cash==0,FString::Printf(TEXT("loan %lld cash %lld"),E.OwnerLoan,E.Cash));Step=3;break;}
 case 3:{Check(TEXT("loan_journaled"),Sum(TEXT("private"),TEXT("owner_loan"),TEXT("owner_loan"))==30000&&Sum(TEXT("company"),TEXT("owner_loan"),TEXT("owner_loan"))==-30000,FString());
  auto C=S.Store;OKPlayerWallet(C).Receive(200000,TEXT("revenue"),OKT(TEXT("ui.accounts_app.test_revenue_label")));C.Company.Estate.Retained=150000;CommitStore(MoveTemp(C));
  const int64 Bank=S.Store.PlayerBank;CompanyRepayOwner(0);
  Check(TEXT("loan_repaid"),E.OwnerLoan==0&&S.Store.PlayerBank==Bank+30000,FString::Printf(TEXT("loan %lld bank +%lld"),E.OwnerLoan,S.Store.PlayerBank-Bank));Step=4;break;}
 case 4:{const int64 Bank=S.Store.PlayerBank;{auto Request=OKRulesRequest(TEXT("bonus_tax"));Request->SetNumberField(TEXT("bonus"),60000);if(const auto Out=OKRulesEval(S,Request))Tax=int64(Out->GetNumberField(TEXT("tax")));}CompanyBonus(60000);
  Check(TEXT("bonus_one_time"),Tax==5790&&S.Store.PlayerBank==Bank+60000-Tax&&S.Store.Personal.BonusYear==S.Store.Personal.TaxYear,FString::Printf(TEXT("tax %lld bank +%lld"),Tax,S.Store.PlayerBank-Bank));
  const int64 Ytd=S.Store.Personal.YtdTax;CompanyBonus(12000);
  Check(TEXT("bonus_cumulative"),S.Store.Personal.YtdIncome==12000&&S.Store.Personal.YtdTax>=Ytd,FString::Printf(TEXT("ytd income %lld tax %lld->%lld"),S.Store.Personal.YtdIncome,Ytd,S.Store.Personal.YtdTax));Step=5;break;}
 case 5:{const int64 Retained=E.Retained;CompanyDividend(Retained+1);Check(TEXT("dividend_capped"),E.Retained==Retained,FString::Printf(TEXT("retained %lld"),E.Retained));
  const int64 Bank=S.Store.PlayerBank;CompanyDividend(10000);
  Check(TEXT("dividend_taxed"),E.Retained==Retained-10000&&S.Store.PlayerBank==Bank+8000,FString::Printf(TEXT("bank +%lld"),S.Store.PlayerBank-Bank));Step=6;break;}
 case 6:{if(!S.Catalog.Jobs.IsEmpty()){S.ShiftJob=S.Catalog.Jobs[0].Id;S.ShiftAppointment=99999;S.ShiftStart=OKStreetMindNow(S)-120;S.ShiftEnd=OKStreetMindNow(S);S.ShiftChats=0;EndShift(false);}
  BankAction(TEXT("deposit_open"),1000,3);SpendCash(10,OKT(TEXT("ui.accounts_app.scratch_card_label")));
  // A month of a profitable company: VAT, the mortgage as interest and principal, his salary (his payslip), the
  // corporate income tax on the profit, and what he lent it repaid.
  {auto C=S.Store;CompanyEnsure(C);auto& Ce=C.Company.Estate;const auto& D=OKStreetProperty();const FOKCompanyWallet W=OKPlayerWallet(C);
   W.FromOwner(5000,TEXT("owner_loan"),OKT(TEXT("ui.accounts_app.test_loan_label")));Ce.OwnerPay=3000;Ce.LossCarry=0;
   FOKStreetHolding Lot;Lot.Unit=TEXT("lot_02");Lot.Mode=TEXT("own");Lot.Since=C.CalendarDay;Lot.Bought=300000;Lot.Loan=100000;{auto Request=OKRulesRequest(TEXT("loan_payment"));Request->SetNumberField(TEXT("principal"),100000);if(const auto Out=OKRulesEval(S,Request))Lot.LoanPayment=int64(Out->GetNumberField(TEXT("payment")));}Ce.Holdings.Add(Lot);
   const FString Hq=Ce.Hq()?Ce.Hq()->Unit:FString();
   for(int32 K=0;K<3;++K){FOKStreetWorker Wk;Wk.Id=FString::Printf(TEXT("t%d"),K);Wk.Name=OKT(TEXT("ui.accounts_app.test_employee_label"));Wk.Role=D.Roles.IsEmpty()?FString():D.Roles[0].Id;Wk.Holding=Hq;Wk.Skill=300;Wk.Diligence=90;Wk.Morale=95;Wk.Pay=800;Ce.Workers.Add(Wk);}
   const int32 Rows=Ce.Ledger.Num();const int64 Income=C.Personal.YtdIncome;
   CompanySettleMonth(C);
   auto Row=[&](const TCHAR* Kind){int64 V=0;bool Any=false;for(int32 K=Rows;K<Ce.Ledger.Num();++K)if(Ce.Ledger[K].Kind==Kind){V+=Ce.Ledger[K].Amount;Any=true;}return Any?V:MIN_int64;};
   int64 Profit=0;for(int32 K=Rows;K<Ce.Ledger.Num();++K){const auto& L=Ce.Ledger[K];if(L.Kind!=TEXT("cit")&&OKBookCategoryKind(L.Kind)==TEXT("pl"))Profit+=L.Amount;}
   const FOKStreetHolding* After=Ce.Holdings.FindByPredicate([](const FOKStreetHolding& X){return X.Unit==TEXT("lot_02");});
   const int64 Revenue=Row(TEXT("revenue")),Vat=Row(TEXT("vat")),Interest=Row(TEXT("loan_interest")),Principal=Row(TEXT("loan_principal")),Cit=Row(TEXT("cit"));
   Check(TEXT("month_vat"),Revenue>0&&Vat==-int64(Revenue*D.VatSmall),FString::Printf(TEXT("revenue %lld vat %lld"),Revenue,Vat));
   Check(TEXT("month_mortgage_split"),Interest<0&&Principal<0&&After&&After->Loan==100000+Principal&&-(Interest+Principal)==Lot.LoanPayment,FString::Printf(TEXT("interest %lld principal %lld"),Interest,Principal));
   Check(TEXT("month_owner_pay"),Row(TEXT("owner_pay"))!=MIN_int64&&C.Personal.YtdIncome==Income+3000&&S.PayslipNote.Contains(OKT(TEXT("ui.accounts_app.social_insurance_label"))),S.PayslipNote);
   Check(TEXT("month_cit"),Profit>0&&Cit==-int64(Profit*D.CitSmall),FString::Printf(TEXT("profit %lld cit %lld"),Profit,Cit));
   Check(TEXT("month_owner_repaid"),Ce.OwnerLoan==0&&Row(TEXT("owner_repay"))==-5000,FString::Printf(TEXT("owner loan %lld"),Ce.OwnerLoan));
   CommitStore(MoveTemp(C));}
  Step=7;break;}
 case 7:{
  Check(TEXT("private_flows"),Count(TEXT("private"),TEXT("wage"))>=1&&Count(TEXT("private"),TEXT("social"))>=1&&Count(TEXT("private"),TEXT("deposit_term"))==2&&Count(TEXT("private"),TEXT("lottery"))==1,
   FString::Printf(TEXT("wage %d social %d deposit %d lottery %d"),Count(TEXT("private"),TEXT("wage")),Count(TEXT("private"),TEXT("social")),Count(TEXT("private"),TEXT("deposit_term")),Count(TEXT("private"),TEXT("lottery"))));
  const int32 Gaps=BookGaps(Mark);Check(TEXT("nothing_unclassified"),Gaps==0,FString::Printf(TEXT("%d"),Gaps));
  TMap<FString,TMap<FString,int64>> Bal;OKBookBalances(S.Store,Bal);bool Match=true;FString Off;
  for(const auto& B:Bal){TMap<FString,int64> Last;OKBookLastBalances(BookOf(B.Key),Last);for(const auto& A:B.Value)if(Last.FindRef(A.Key)!=A.Value){Match=false;Off+=FString::Printf(TEXT("%s/%s %lld!=%lld "),*B.Key,*A.Key,Last.FindRef(A.Key),A.Value);}}
  Check(TEXT("balances_match"),Match,Off);
  // The income statement: income less expenses is the sum of what changed his wealth.
  int64 Pl=0,In=0,Out=0;TMap<FString,int64> ByCat;for(const auto& X:BookOf(TEXT("private")))if(X.Serial>Mark&&OKBookCategoryKind(X.Category)==TEXT("pl")){Pl+=X.Amount;ByCat.FindOrAdd(X.Category)+=X.Amount;}
  for(const auto& X:ByCat)(X.Value>0?In:Out)+=X.Value;
  Check(TEXT("income_statement"),In+Out==Pl&&ByCat.FindRef(TEXT("bonus"))==72000&&ByCat.FindRef(TEXT("dividend_in"))==10000&&ByCat.FindRef(TEXT("dividend_tax"))==-2000,FString::Printf(TEXT("in %lld out %lld"),In,Out));
  const FString Csv=ExportBook(TEXT("company"));TArray<FString> Lines;FFileHelper::LoadFileToStringArray(Lines,*Csv);
  Check(TEXT("csv"),!Csv.IsEmpty()&&Lines.Num()==BookOf(TEXT("company")).Num()+1,Csv);IFileManager::Get().Delete(*Csv);
  const FString Path=FPaths::ProjectSavedDir()/TEXT("StreetSocial/accounts-test.json");FString Error;FOKStreetStore Reloaded;
  const bool Ok=OKStreetAtomicSave(Path,OKStreetWriteSave(S.Store),Error)&&OKStreetLoadSaveFile(Path,Reloaded,Error)==EOKStreetLoad::Ok;
  Check(TEXT("save_roundtrip"),Ok&&Reloaded.Company.Estate.Cash==E.Cash&&Reloaded.Company.Estate.Retained==E.Retained&&Reloaded.Company.Estate.Capital==E.Capital&&Reloaded.Personal.YtdTax==S.Store.Personal.YtdTax&&Reloaded.Personal.BonusYear==S.Store.Personal.BonusYear,Error);
  IFileManager::Get().Delete(*Path);IFileManager::Get().Delete(*(Path+TEXT(".bak")));
  // -StreetSocialAccountsShots=<dir>: the app's pages, while the test's books are still there.
  if(FParse::Value(FCommandLine::Get(),TEXT("StreetSocialAccountsShots="),Shots)){S.AccBook=TEXT("company");S.AccTab=TEXT("journal");S.AccMonth=0;OpenApp(TEXT("accounts"),false);Step=20;At=Now+2;break;}
  Step=30;break;}
 case 20:case 21:case 22:case 23:case 24:case 25:{
  static const TCHAR* Pages[][2]={{TEXT("company"),TEXT("journal")},{TEXT("company"),TEXT("pl")},{TEXT("company"),TEXT("balance")},{TEXT("private"),TEXT("journal")},{TEXT("private"),TEXT("cash")},{TEXT("private"),TEXT("balance")}};
  const int32 K=Step-20;FScreenshotRequest::RequestScreenshot(Shots/FString::Printf(TEXT("accounts.%s.%s.png"),Pages[K][0],Pages[K][1]),true,false); // with the UI: the app is a widget
  Step=K+1<6?40+K+1:30;At=Now+1.5;break;}
 case 41:case 42:case 43:case 44:case 45:{static const TCHAR* Next[][2]={{TEXT(""),TEXT("")},{TEXT("company"),TEXT("pl")},{TEXT("company"),TEXT("balance")},{TEXT("private"),TEXT("journal")},{TEXT("private"),TEXT("cash")},{TEXT("private"),TEXT("balance")}};
  const int32 K=Step-40;S.AccBook=Next[K][0];S.AccTab=Next[K][1];RefreshUI();Step=20+K;At=Now+2;break;}
 case 30:{CloseApp();CommitStore(FOKStreetStore(Saved));BooksRestore(Files);S.ShiftJob.Reset();
  UE_LOG(LogTemp,Display,TEXT("ACCOUNTS TEST done: %d passed, %d failed"),Passed,Failed);Step=31;At=Now+3;break;}
 case 31:FPlatformMisc::RequestExitWithStatus(false,0);Step=99;break;
 default:break;
 }
}
