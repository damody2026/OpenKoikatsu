// The company app (phone: 我的公司) and the quick row on a lot (included after the finance apps for their widgets).

TSharedRef<SWidget> AOKStreetSocialScene::BuildCompanyApp(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);const auto& D=OKStreetProperty();
 // A view (estate.rs company_view): his company as it would be today (a borrowed corner, this week's applicants), each
 // holding's figures and the market; the real store changes on the next action / day.
 auto C=S.Store;const TSharedPtr<FJsonObject> View=CompanyView(C);if(View.IsValid())OKCompanyReadRules(C.Company,View->GetObjectField(TEXT("company")));
 auto Find=[&](const TCHAR* List,const FString& Unit)->TSharedPtr<FJsonObject>{if(!View.IsValid())return nullptr;for(const auto& V:View->GetArrayField(List))if(V->AsObject()->GetStringField(TEXT("unit"))==Unit)return V->AsObject();return nullptr;};
 auto HNum=[&](const FString& Unit,const TCHAR* Field){const auto O=Find(TEXT("holdings"),Unit);return O.IsValid()?int64(O->GetNumberField(Field)):int64(0);};
 auto HStr=[&](const FString& Unit,const TCHAR* Field){const auto O=Find(TEXT("holdings"),Unit);return O.IsValid()?O->GetStringField(Field):FString();};
 auto MNum=[&](const FString& Unit,const TCHAR* Field){const auto O=Find(TEXT("market"),Unit);return O.IsValid()?int64(O->GetNumberField(Field)):int64(0);};
 const auto& Co=C.Company;const auto& E=Co.Estate;const int32 Day=C.CalendarDay;
 auto Box=SNew(SVerticalBox);auto Line=[&](TSharedRef<SWidget> W){Box->AddSlot().AutoHeight().Padding(2)[W];};
 auto Tabs=SNew(SHorizontalBox);
 for(const auto& T:TArray<TPair<FString,FString>>{{TEXT("overview"),OKT(TEXT("ui.company_app.overview_tab"))},{TEXT("estate"),OKT(TEXT("ui.company_app.real_estate_tab"))},{TEXT("people"),OKT(TEXT("ui.company_app.hr_tab"))},{TEXT("books"),OKT(TEXT("ui.company_app.ledger_tab"))},{TEXT("others"),OKT(TEXT("ui.company_app.others_company_label"))}}){const FString Id=T.Key;
  Tabs->AddSlot().AutoWidth().Padding(2)[OKFinButton(Weak,T.Value,[Weak,Id](){Weak->State->CompanyTab=Id;Weak->RefreshUI();},true,S.CompanyTab==Id?FLinearColor(1,.85f,.3f):FLinearColor::White)];}
 Line(Tabs);
 if(!D.Error.IsEmpty())Line(OKFinText(D.Error,FLinearColor(1,.4f,.4f)));
 // Her companies (and his job and offers there): open whether or not he has a company of his own.
 // His own payroll year (part-time pay anywhere), shown on every tab's top.
 {const auto& P=C.Personal;Line(OKFinText(OKMsg(TEXT("ui.company_app.personal_tax_year_summary"), {{TEXT("tax_year"), P.TaxYear>0?P.TaxYear:OKStreetCalendarDateTime(Day,12.0).GetYear()}, {TEXT("salary_income"), P.YtdIncome}, {TEXT("bonus"), P.YtdBonus}, {TEXT("social_insurance"), P.YtdSocial}, {TEXT("income_tax"), P.YtdTax}, {TEXT("housing_fund"), P.HousingFund}}),FLinearColor(.75f,.85f,1),10));}
 if(S.CompanyTab==TEXT("others")){
  if(C.HeroCompanies.IsEmpty())Line(OKFinText(OKT(TEXT("ui.company_app.no_heroine_company_note")),FLinearColor(.75f,.75f,.75f)));
  for(const auto& H:C.HeroCompanies){const int32 Girl=CharacterOf(H.Owner);const FString OwnerId=H.Owner;const FOKStreetHolding* Hq=H.Estate.Hq();
   Line(OKFinText(OKMsg(TEXT("ui.company_app.company_card_line"), {{TEXT("company_name"), *H.Name}, {TEXT("owner_name"), Girl!=INDEX_NONE?*S.Manifest.Characters[Girl].Name:*H.Owner}, {TEXT("hq_name"), Hq?*UnitOf(Hq->Unit).Name:OKT(TEXT("ui.company.no_estate_label"))}, {TEXT("headcount"), H.Estate.Workers.Num()}, {TEXT("last_profit"), H.LastProfit}}),FLinearColor(1,.75f,.85f),13,true));
   auto Row=SNew(SWrapBox).UseAllottedSize(true);
   if(C.PlayerJobs.Contains(TEXT("hc_")+H.Owner)){Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.part_time_offer_note"), {{TEXT("hourly_wage"), H.OfferWage}}),FLinearColor(.6f,1,.7f))];
    Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.quit_job_button")),[Weak,OwnerId](){Weak->HeroJobQuit(OwnerId);})];}
   else if(H.OfferUntil>=Day){Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.job_offer_summary"), {{TEXT("hourly_wage"), H.OfferWage}, {TEXT("reply_by_date"), *OKStreetCalendarLabel(H.OfferUntil,12.0)}}),FLinearColor(1,.85f,.3f))];
    Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.accept_button")),[Weak,OwnerId](){Weak->HeroOfferAnswer(OwnerId,true);},true,FLinearColor(.5f,1,.5f))];
    Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.decline_button")),[Weak,OwnerId](){Weak->HeroOfferAnswer(OwnerId,false);},true,FLinearColor(1,.5f,.5f))];}
   if(Hq){const FString Place=HoldingPlace(*Hq);Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.navigate_there_button")),[Weak,Place](){if(const auto* P=OKStreetFindPlace(Place)){Weak->CloseApp();Weak->NavigatePoint(P->Door,P->Name);}})];}
   Line(Row);}
  return SNew(SScrollBox)+SScrollBox::Slot()[Box];
 }
 if(Co.Founded<0){
  Line(OKFinText(OKT(TEXT("ui.company_app.no_company_yet_hint")),FLinearColor(1,.85f,.3f),13,true));
  Line(OKFinButton(Weak,OKT(TEXT("ui.company_app.found_company_button")),[Weak](){Weak->FoundCompany(FString(),TEXT("cafe"));Weak->RefreshUI();}));
  return SNew(SScrollBox)+SScrollBox::Slot()[Box];
 }
 const FString Tab=S.CompanyTab;
 auto Money=[](int64 V){return OKStreetMoneyText(V);};
 // An editable amount for offers and pay.
 auto AmountBox=[&](){return SNew(SBox).WidthOverride(110)[SNew(SEditableTextBox).Text(FText::FromString(S.CompanyAmount)).HintText(FText::FromString(OKT(TEXT("ui.company_app.monthly_salary_label"))))
  .OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->CompanyAmount=T.ToString();})];};
 if(Tab==TEXT("overview")){
  int64 Revenue=0,Costs=0,Wages=0;for(const auto& H:E.Holdings){Revenue+=HNum(H.Unit,TEXT("revenue"));Costs+=HNum(H.Unit,TEXT("costs"));}
  for(const auto& W:E.Workers)Wages+=W.Pay;for(const auto& St:Co.Staff)Wages+=St.Pay;
  int32 Cap=0,Head=E.Workers.Num()+Co.Staff.Num();for(const auto& H:E.Holdings)Cap+=int32(HNum(H.Unit,TEXT("capacity")));
  Line(OKFinText(OKMsg(TEXT("ui.company_app.company_founded_line"), {{TEXT("company_name"), *Co.Name}, {TEXT("founded_date"), *OKStreetCalendarLabel(Co.Founded,12.0)}, {TEXT("reputation"), E.Reputation}}),FLinearColor(1,.85f,.3f),15,true));
  Line(OKFinText(OKMsg(TEXT("ui.company_app.company_finance_summary"), {{TEXT("cash"), *Money(E.Cash)}, {TEXT("owner_loan"), *Money(E.OwnerLoan)}, {TEXT("retained"), *Money(E.Retained)}, {TEXT("capital"), *Money(E.Capital)}, {TEXT("last_profit"), *Money(Co.LastProfit)}}),FLinearColor(1,.85f,.3f),12,true));
  Line(OKFinText(OKMsg(TEXT("ui.company_app.personal_funds_line"), {{TEXT("bank_balance"), *Money(C.PlayerBank)}, {TEXT("cash_balance"), *Money(C.PlayerCash)}}),FLinearColor(.75f,.75f,.8f),10));
  // Money between him and the company: only under a name, each with its tax (Docs/STREET_ACCOUNTING_PLAN_20261012.md).
  {auto Row=SNew(SWrapBox).UseAllottedSize(true);auto Amount=[Weak](){return FCString::Atoi64(*Weak->State->CompanyAmount);};
   Row->AddSlot().Padding(2)[OKFinText(OKT(TEXT("ui.bank.amount_yen_prefix")))];Row->AddSlot().Padding(2)[AmountBox()];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.inject_capital_button")),[Weak,Amount](){Weak->CompanyInject(Amount());})];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.repay_owner_loan_hint")),[Weak,Amount](){Weak->CompanyRepayOwner(Amount());},E.OwnerLoan>0)];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.bonus_to_self_button")),[Weak,Amount](){Weak->CompanyBonus(Amount());})];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.dividend_button"), {{TEXT("max_dividend"), *Money(FMath::Max<int64>(0,FMath::Min(E.Retained,E.Cash)))}}),[Weak,Amount](){Weak->CompanyDividend(Amount());},FMath::Min(E.Retained,E.Cash)>0)];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.owner_pay_set_label"), {{TEXT("current_owner_pay"), *Money(E.OwnerPay)}}),[Weak,Amount](){Weak->CompanySetOwnerPay(Amount());})];
   const bool Auto=E.bAutoFund;Row->AddSlot().Padding(2)[OKFinButton(Weak,Auto?OKT(TEXT("ui.company_app.auto_borrow_on")):OKT(TEXT("ui.company_app.auto_borrow_off")),[Weak,Auto](){Weak->CompanyAutoFund(!Auto);})];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.ledger_book_title")),[Weak](){Weak->State->AccBook=TEXT("company");Weak->OpenApp(TEXT("accounts"),false);},true,FLinearColor(1,.85f,.3f))];
   Line(Row);}
  Line(OKFinText(OKMsg(TEXT("ui.company_app.monthly_forecast_line"), {{TEXT("revenue"), *Money(Revenue)}, {TEXT("costs"), *Money(Costs)}, {TEXT("wages"), *Money(Wages)}, {TEXT("misc_costs"), *Money(Local(TEXT("cash"),200)*Head)}, {TEXT("net_profit"), *Money(Revenue-Costs-Wages-Local(TEXT("cash"),200)*Head)}})));
  Line(OKFinText(OKMsg(TEXT("ui.company_app.headcount_capacity"), {{TEXT("headcount"), Head}, {TEXT("capacity"), Cap}})));
  for(const auto& H:E.Holdings){const FOKStreetUnitDef U=UnitOf(H.Unit);const auto* B=D.Building(H.Building);
   const FString Build=H.BuildStart<0?FString():Day<H.BuildDone?OKMsg(TEXT("ui.company_app.under_construction_label"), {{TEXT("building_name"), B?*B->Name:TEXT("")}, {TEXT("days_left"), H.BuildDone-Day}}):OKMsg(TEXT("ui.company_app.building_name_prefix"), {{TEXT("building_name"), B?*B->Name:TEXT("")}});
   Line(OKFinText(OKMsg(TEXT("ui.company_app.unit_row_summary"), {{TEXT("hq_marker"), H.bHq?OKT(TEXT("ui.company_app.hq_marker_label")):TEXT("")}, {TEXT("unit_name"), *U.Name}, {TEXT("ownership_label"), H.Mode==TEXT("own")?OKT(TEXT("ui.company_app.owned_mode_label")):H.Mode==TEXT("rent")?OKT(TEXT("ui.company_app.rent_mode_label")):OKT(TEXT("ui.company_app.borrowed_label"))}, {TEXT("build_name"), *Build}, {TEXT("headcount"), int32(HNum(H.Unit,TEXT("headcount")))}, {TEXT("capacity"), int32(HNum(H.Unit,TEXT("capacity")))}, {TEXT("loan_part"), H.Loan>0?*OKMsg(TEXT("ui.company_app.loan_balance_label"), {{TEXT("loan_balance"), *Money(H.Loan)}}):TEXT("")}}),FLinearColor(.85f,.9f,1)));}
 }
 else if(Tab==TEXT("estate")){
  Line(OKFinText(OKT(TEXT("ui.company_app.my_estates_tab")),FLinearColor(1,.85f,.3f),13,true));
  for(const auto& H:E.Holdings){const FOKStreetUnitDef U=UnitOf(H.Unit);const FString Unit=H.Unit;auto Row=SNew(SWrapBox).UseAllottedSize(true);
   Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.unit_ownership_label"), {{TEXT("unit_name"), *U.Name}, {TEXT("ownership_detail"), H.Mode==TEXT("own")?*OKMsg(TEXT("ui.company_app.owned_market_value_label"), {{TEXT("market_value"), *Money(HNum(H.Unit,TEXT("value")))}}):H.Mode==TEXT("rent")?*OKMsg(TEXT("ui.company_app.monthly_rent_line"), {{TEXT("rent"), *Money(H.Rent)}, {TEXT("leave_notice"), H.LeaveMonth>=0?OKT(TEXT("ui.company_app.lease_end_notified_suffix")):TEXT("")}}):OKT(TEXT("ui.company_app.borrowed_label"))}}))];
   if(U.bLot&&H.BuildStart<0)for(const auto& B:D.Buildings){const FString Bid=B.Id;Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.build_option_label"), {{TEXT("building_name"), *B.Name}, {TEXT("cost"), *Money(B.Cost)}, {TEXT("days"), B.Days}}),[Weak,Unit,Bid](){Weak->CompanyBuild(Unit,Bid);})];}
   if(!H.bHq&&HNum(H.Unit,TEXT("capacity"))>0&&HStr(H.Unit,TEXT("use"))!=TEXT("land"))Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.set_as_hq_button")),[Weak,Unit](){Weak->CompanySetHq(Unit);})];
   if(H.Mode!=TEXT("own")&&H.LeaveMonth<0)Row->AddSlot().Padding(2)[OKFinButton(Weak,H.Mode==TEXT("borrow")?FString(OKT(TEXT("ui.company_app.stop_borrowing_button"))):FString(OKT(TEXT("ui.company_app.end_lease_button"))),[Weak,Unit](){Weak->CompanyLeave(Unit);})];
   if(H.Mode==TEXT("own"))Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.sell_estimate_button"), {{TEXT("net_sale"), *Money(HNum(H.Unit,TEXT("sale_net")))}}),[Weak,Unit](){Weak->CompanySell(Unit);})];
   if(H.Loan>0)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.pay_off_mortgage_button"), {{TEXT("loan_balance"), *Money(H.Loan)}}),[Weak,Unit](){Weak->CompanyPayOff(Unit);})];
   // Renting his own place out to others (a heroine's company may take it).
   if(H.Mode==TEXT("own")){
    if(!H.Tenant.IsEmpty()){const int32 T=CharacterOf(H.Tenant);Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.leased_to_tenant_line"), {{TEXT("tenant_name"), T!=INDEX_NONE?*S.Manifest.Characters[T].Name:*H.Tenant}, {TEXT("lease_rent"), *Money(H.LeaseRent)}}),FLinearColor(.6f,1,.7f))];
     Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.withdraw_next_month_button")),[Weak,Unit](){Weak->CompanyLeaseOut(Unit,0);})];}
    else if(H.LeaseRent>0){Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.listed_for_rent_status"), {{TEXT("lease_rent"), *Money(H.LeaseRent)}}),FLinearColor(.6f,1,.7f))];Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.stop_renting_out_button")),[Weak,Unit](){Weak->CompanyLeaseOut(Unit,0);})];}
    else{const auto* B=D.Building(H.Building);const int64 Suggest=U.bLot?(B&&H.BuildDone>=0&&Day>=H.BuildDone?int64(U.Rent*2.5):0):FMath::Max<int64>(U.Rent,Local(TEXT("rent"),1500));
     if(Suggest>0)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.list_for_rent_hint"), {{TEXT("suggested_rent"), *Money(Suggest)}}),[Weak,Unit,Suggest](){const int64 R=FCString::Atoi64(*Weak->State->CompanyAmount);Weak->CompanyLeaseOut(Unit,R>0?R:Suggest);})];}
   }
   const FString Place=HoldingPlace(H);Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.navigate_there_button")),[Weak,Place](){if(const auto* P=OKStreetFindPlace(Place)){Weak->CloseApp();Weak->NavigatePoint(P->Door,P->Name);}})];
   Line(Row);}
  Line(OKFinText(OKT(TEXT("ui.company_app.market_listings_header")),FLinearColor(1,.85f,.3f),13,true));
  for(const auto& U:D.Units){if(E.Holdings.ContainsByPredicate([&](const FOKStreetHolding& H){return H.Unit==U.Id&&H.Mode==TEXT("own");}))continue;
   if(const FString User=UnitUser(C,U.Id);!User.IsEmpty()&&User!=TEXT("player")){const FOKStreetHeroCompany* By=C.HeroCompanies.FindByPredicate([&](const FOKStreetHeroCompany& X){return X.Owner==User;});
    Line(OKFinText(OKMsg(TEXT("ui.company_app.in_use_by_label"), {{TEXT("unit_name"), *U.Name}, {TEXT("user_name"), By?*By->Name:*User}}),FLinearColor(.6f,.6f,.6f)));continue;}
   const bool Renting=E.Holdings.ContainsByPredicate([&](const FOKStreetHolding& H){return H.Unit==U.Id;});const FString Unit=U.Id;auto Row=SNew(SWrapBox).UseAllottedSize(true);
   Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.plot_listing_row"), {{TEXT("unit_name"), *U.Name}, {TEXT("capacity_or_lot"), U.bLot?OKT(TEXT("ui.company_app.empty_lot_label")):*OKMsg(TEXT("ui.company_app.capacity_label"), {{TEXT("capacity"), U.Capacity}})}, {TEXT("area"), U.Area}, {TEXT("rent_part"), U.Rent>0?*OKMsg(TEXT("ui.company_app.monthly_rent_prefix"), {{TEXT("rent"), *Money(U.Rent)}}):TEXT("")}, {TEXT("traffic_percent"), U.Traffic*100.f}}))];
   if(U.Rent>0&&!Renting)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.rent_deposit_label"), {{TEXT("deposit"), *Money(MNum(U.Id,TEXT("deposit")))}}),[Weak,Unit](){Weak->CompanyRent(Unit);})];
   if(U.Price>0){const int64 Price=MNum(U.Id,TEXT("price"));
    Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.buy_price_button"), {{TEXT("price"), *Money(Price)}}),[Weak,Unit](){Weak->CompanyBuy(Unit,false);})];
    Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.mortgage_buy_label"), {{TEXT("down_payment"), *Money(MNum(U.Id,TEXT("down")))}, {TEXT("monthly_payment"), *Money(MNum(U.Id,TEXT("loan_payment")))}}),[Weak,Unit](){Weak->CompanyBuy(Unit,true);})];}
   const FString Place=U.bLot?U.Id:U.Place;Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.navigate_to_see_button")),[Weak,Place](){if(const auto* P=OKStreetFindPlace(Place)){Weak->CloseApp();Weak->NavigatePoint(P->Door,P->Name);}})];
   Line(Row);}
 }
 else if(Tab==TEXT("people")){
  TArray<const FOKStreetHolding*> Open;for(const auto& H:E.Holdings)if(HNum(H.Unit,TEXT("capacity"))>HNum(H.Unit,TEXT("headcount")))Open.Add(&H);
  Line(SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(OKT(TEXT("ui.company_app.bid_amount_prompt")))]+SHorizontalBox::Slot().AutoWidth().Padding(4,0)[AmountBox()]
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[OKFinText(Open.IsEmpty()?FString(OKT(TEXT("ui.company_app.no_vacancy_hint"))):OKMsg(TEXT("ui.company_app.new_hire_arrives"), {{TEXT("unit_name"), *UnitOf(Open[0]->Unit).Name}}),FLinearColor(.75f,.75f,.75f),10)]);
  Line(OKFinText(OKMsg(TEXT("ui.company_app.weekly_applicants_header"), {{TEXT("reputation"), E.Reputation}}),FLinearColor(1,.85f,.3f),13,true));
  for(const auto& W:E.Applicants){const FString Wid=W.Id;const bool No=E.Refused.Contains(W.Id);const auto* R=D.Role(W.Role);auto Row=SNew(SWrapBox).UseAllottedSize(true);
   Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.applicant_row"), {{TEXT("worker_name"), *W.Name}, {TEXT("role_name"), R?*R->Name:*W.Role}, {TEXT("skill"), W.Skill}, {TEXT("diligence"), W.Diligence}, {TEXT("loyalty"), W.Loyalty}, {TEXT("ask_pay"), *Money(W.Ask)}, {TEXT("declined_note"), No?OKT(TEXT("ui.company_app.declined_suffix")):TEXT("")}}),No?FLinearColor(.6f,.6f,.6f):FLinearColor::White)];
   if(!No&&!Open.IsEmpty()){const FString Unit=Open[0]->Unit;const int64 Ask=W.Ask;
    Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.interview_button")),[Weak,Wid,Unit,Ask](){const int64 Offer=FCString::Atoi64(*Weak->State->CompanyAmount);Weak->CompanyHire(Wid,Offer>0?Offer:Ask,Unit);})];
    Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.hire_at_ask_button"), {{TEXT("ask_pay"), *Money(Ask)}}),[Weak,Wid,Unit,Ask](){Weak->CompanyHire(Wid,Ask,Unit);})];}
   Line(Row);}
  Line(OKFinText(OKT(TEXT("ui.company_app.active_staff_header")),FLinearColor(1,.85f,.3f),13,true));
  for(const auto& St:Co.Staff){const int32 I=CharacterOf(St.Heroine);Line(OKFinText(OKMsg(TEXT("ui.company_app.heroine_staff_row"), {{TEXT("name"), I!=INDEX_NONE?*S.Manifest.Characters[I].Name:*St.Heroine}, {TEXT("role"), *St.Role}, {TEXT("pay"), *Money(St.Pay)}}),FLinearColor(1,.7f,.85f)));}
  for(const auto& W:E.Workers){const FString Wid=W.Id;const auto* R=D.Role(W.Role);auto Row=SNew(SWrapBox).UseAllottedSize(true);
   Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.employee_row"), {{TEXT("worker_name"), *W.Name}, {TEXT("role_name"), R?*R->Name:*W.Role}, {TEXT("skill"), W.Skill}, {TEXT("morale"), W.Morale}, {TEXT("low_morale_note"), W.Morale<35?OKT(TEXT("ui.company_app.wants_to_leave_label")):TEXT("")}, {TEXT("pay"), *Money(W.Pay)}, {TEXT("unit_name"), *UnitOf(W.Holding).Name}}),W.Morale<35?FLinearColor(1,.6f,.5f):FLinearColor::White)];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.raise_to_above_button")),[Weak,Wid](){Weak->CompanyRaise(Wid,FCString::Atoi64(*Weak->State->CompanyAmount));})];
   for(const auto* H:Open)if(H->Unit!=W.Holding){const FString Unit=H->Unit;Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.transfer_to_button"))+UnitOf(Unit).Name,[Weak,Wid,Unit](){Weak->CompanyAssign(Wid,Unit);})];}
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.company_app.layoff_severance_button"), {{TEXT("severance"), *Money(View.IsValid()?int64(View->GetObjectField(TEXT("severance"))->GetNumberField(W.Id)):0)}}),[Weak,Wid](){Weak->CompanyFire(Wid);})];
   Line(Row);}
 }
 else{
  Line(OKFinButton(Weak,OKT(TEXT("ui.company_app.open_ledger_book_button")),[Weak](){Weak->State->AccBook=TEXT("company");Weak->OpenApp(TEXT("accounts"),false);},true,FLinearColor(1,.85f,.3f)));
  Line(OKFinText(FString(OKT(TEXT("ui.company_app.ledger_hint"))),FLinearColor(1,.85f,.3f),13,true));
  for(int32 K=E.Ledger.Num()-1;K>=0&&K>=E.Ledger.Num()-40;--K){const auto& L=E.Ledger[K];
   Line(OKFinText(OKMsg(TEXT("ui.company_app.ledger_entry_row"), {{TEXT("day_label"), *OKStreetCalendarLabel(L.Day,12.0)}, {TEXT("note"), *L.Note}, {TEXT("amount"), *Money(L.Amount)}}),L.Amount<0?FLinearColor(1,.6f,.5f):FLinearColor(.6f,1,.6f),10));}
 }
 return SNew(SScrollBox)+SScrollBox::Slot()[Box];
}

// On a lot or in one of his holdings: what it costs, and the quick actions; with a girl at his own shop, a visit.
void AOKStreetSocialScene::BuildCompanyRow(const TSharedRef<SVerticalBox>& Panel){
 // The company's view (the rules over the whole store, ~3 ms) only where he holds a unit: every rebuild asked for it,
 // although most places have none of his.
 TSharedPtr<FJsonObject> View;bool ViewAsked=false;
 auto GetView=[&]()->const TSharedPtr<FJsonObject>&{if(!ViewAsked){ViewAsked=true;View=CompanyView(State->Store);}return View;};
 auto Holding=[&](const FString& Unit)->TSharedPtr<FJsonObject>{if(!GetView().IsValid())return nullptr;for(const auto& V:View->GetArrayField(TEXT("holdings")))if(V->AsObject()->GetStringField(TEXT("unit"))==Unit)return V->AsObject();return nullptr;};
 auto Market=[&](const FString& Unit)->int64{if(!GetView().IsValid())return 0;for(const auto& V:View->GetArrayField(TEXT("market")))if(V->AsObject()->GetStringField(TEXT("unit"))==Unit)return int64(V->AsObject()->GetNumberField(TEXT("price")));return 0;};
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);const FString Here=PlaceHere(S.Male);if(Here.IsEmpty())return;
 const auto& D=OKStreetProperty();auto Row=SNew(SWrapBox).UseAllottedSize(true);bool Any=false;
 for(const auto& U:D.Units){
  if((U.bLot?U.Id:U.Place)!=Here)continue;Any=true;const FString Unit=U.Id;
  const FOKStreetHolding* H=S.Store.Company.Estate.Holdings.FindByPredicate([&](const FOKStreetHolding& X){return X.Unit==U.Id;});
  if(const FString User=UnitUser(S.Store,U.Id);!H&&!User.IsEmpty()){const FOKStreetHeroCompany* By=S.Store.HeroCompanies.FindByPredicate([&](const FOKStreetHeroCompany& X){return X.Owner==User;});
   Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.accounts_app.quoted_name"), {{TEXT("name"), By?*By->Name:*User}}),FLinearColor(1,.75f,.85f),11,true)];continue;}
  if(!H){Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.unit_rent_sale_line"), {{TEXT("unit_name"), *U.Name}, {TEXT("rent_part"), U.Rent>0?*OKMsg(TEXT("ui.company_app.rent_out_price_label"), {{TEXT("monthly_rent"), U.Rent}}):TEXT("")}, {TEXT("sale_part"), U.Price>0?*OKMsg(TEXT("ui.company_app.sell_price_label"), {{TEXT("market_price"), Market(U.Id)}}):TEXT("")}}),FLinearColor(.6f,1,.7f),11)];
   if(S.Store.Company.Founded>=0){if(U.Rent>0)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.rent_now_button")),[Weak,Unit](){Weak->CompanyRent(Unit);})];if(U.Price>0)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.buy_now_button")),[Weak,Unit](){Weak->CompanyBuy(Unit,false);})];}}
  else{
   const TSharedPtr<FJsonObject> HV=Holding(H->Unit);const FString Use=HV.IsValid()?HV->GetStringField(TEXT("use")):FString();
   Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.company_app.your_unit_label"), {{TEXT("unit_name"), *U.Name}, {TEXT("use_label"), *Use}}),FLinearColor(1,.85f,.3f),11,true)];
   const int32 Girl=FreeTalkPartner()>0?FreeTalkPartner():S.Companion;
   if(Girl>0&&RemoteKind(Girl)==0&&HV.IsValid()&&HV->GetNumberField(TEXT("capacity"))>0)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.show_her_around_button")),[Weak,Girl,Unit](){Weak->CompanyShowAround(Girl,Unit);})];
  }
 }
 if(!Any)return;
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.company_app.company_label")),[Weak](){Weak->OpenApp(TEXT("company"),false);})];
 Panel->AddSlot().AutoHeight().Padding(2)[Row];
}

// Showing her his own place: a memory, and her idea of what he has.
void AOKStreetSocialScene::CompanyShowAround(int32 Girl,const FString& UnitId){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);const FOKStreetHolding* H=S.Store.Company.Estate.Holdings.FindByPredicate([&](const FOKStreetHolding& X){return X.Unit==UnitId;});if(!H)return;
 const FString Name=UnitOf(UnitId).Name;
 FString Use;int32 Head=0;if(const TSharedPtr<FJsonObject> View=CompanyView(S.Store))for(const auto& V:View->GetArrayField(TEXT("holdings")))if(V->AsObject()->GetStringField(TEXT("unit"))==UnitId){Use=V->AsObject()->GetStringField(TEXT("use"));Head=int32(V->AsObject()->GetNumberField(TEXT("headcount")));}
 MindObserve(Girl,TEXT("company"),OKMsg(TEXT("ui.company_app.tour_offer_line"), {{TEXT("unit_name"), *Name}, {TEXT("use_label"), *Use}, {TEXT("headcount"), Head}}),HoldingPlace(*H),0.6f);
 {auto C=S.Store;if(CompanyActOn(C,TEXT("show_around"),[&](FJsonObject& A){A.SetStringField(TEXT("heroine"),Id);}))CommitStore(MoveTemp(C));} // her idea of what he has (estate.rs)
 S.MindRecent.Add(Id,OKMsg(TEXT("ui.company_app.tour_just_finished"), {{TEXT("unit_name"), *Name}}));
 if(FreeTalkPartner()==Girl&&!S.FreeTalkBusy){S.SystemNote=true;S.FreeTalkDraft=OKMsg(TEXT("ui.company_app.tour_reaction_prompt"), {{TEXT("unit_name"), *Name}});SendFreeTalk();}
 RefreshUI();
}

// Development: -StreetSocialCompanyTest=<dir> runs the company end to end on the live store (put back at the end):
// found, rent an office, hire two, buy a lot on a mortgage, build, two weeks pass, the month is settled, the money
// runs out (eviction, people leave, bankruptcy keeps the land), and the save reads back the same. Screenshots of
// the lot while it is built and when it stands. "COMPANY TEST" lines, then the game quits.
void AOKStreetSocialScene::TickCompanyTest(){
 auto& S=*State;FString Dir;if(!S.AllReady||!FParse::Value(FCommandLine::Get(),TEXT("StreetSocialCompanyTest="),Dir))return;
 static int32 Step=0,Passed=0,Failed=0;static double At=0;static FOKStreetStore Saved;static int64 Mark=0;
 const double Now=SceneTime();if(At==0)At=Now+6.0;if(Now<At)return;
 auto Check=[&](const TCHAR* Name,bool Ok,const FString& Detail){UE_LOG(LogTemp,Display,TEXT("COMPANY TEST %s: %s %s"),Name,Ok?TEXT("PASS"):TEXT("FAIL"),*Detail);if(Ok)++Passed;else ++Failed;};
 auto Shot=[&](const TCHAR* Name){FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("company.%s.png"),Name),false,false);};
 auto& E=S.Store.Company.Estate;const FString Lot=TEXT("lot_04");
 // From the street in front of the lot (its door is on the street side), looking back at it.
 auto LookAtLot=[&](){const FOKStreetUnitDef U=UnitOf(Lot);const FVector Front=FRotator(0,U.Yaw,0).Vector();const auto* P=OKStreetFindPlace(Lot);
  S.Male=P?P->Door+Front*250.f:FVector(U.Center.X,U.Center.Y,0)+Front*900.f;S.Male.Z=OKStreetFloorZ(S.Male);
  S.Yaw=(-Front).Rotation().Yaw+25.f;S.Pitch=-50;S.Distance=1900;S.LotWorldAt=0; // from above: street trees stand in front of lots
  UE_LOG(LogTemp,Display,TEXT("COMPANY TEST camera at %s place '%s'"),*S.Male.ToCompactString(),*PlaceHere(S.Male));};
 switch(Step){
 case 0:{S.BooksTickAt=0;TickBooks();Saved=S.Store;S.TestBooks=BooksSnapshot();Mark=OKBookNextSerial();auto C=S.Store;C.PlayerBank=2000000;C.Company=FOKStreetCompany();OKBookAdjustTo(S.Store,C);CommitStore(MoveTemp(C));
  Check(TEXT("property_data"),OKStreetProperty().Error.IsEmpty()&&OKStreetProperty().Units.Num()>0,OKStreetProperty().Error);
  FoundCompany(OKT(TEXT("ui.company_app.test_company_name")),TEXT("cafe"));Check(TEXT("found"),S.Store.Company.Founded>=0&&E.Hq()&&E.Hq()->Mode==TEXT("borrow"),S.Message);Step=1;break;}
 case 1:{CompanyRent(TEXT("dept_office"));const FOKStreetHolding* H=E.Holdings.FindByPredicate([](const FOKStreetHolding& X){return X.Unit==TEXT("dept_office");});
  Check(TEXT("rent"),H&&H->bHq&&H->Deposit>0&&!E.Holdings.ContainsByPredicate([](const FOKStreetHolding& X){return X.Mode==TEXT("borrow")&&X.bHq;}),S.FinNotice);
  {auto C=S.Store;CompanyActOn(C,TEXT("applicants"),[](FJsonObject&){});CommitStore(MoveTemp(C));}Check(TEXT("applicants"),E.Applicants.Num()>=3,FString::Printf(TEXT("%d"),E.Applicants.Num()));
  for(int32 K=0;K<2&&E.Applicants.Num()>0;++K)CompanyHire(E.Applicants[0].Id,E.Applicants[0].Ask,TEXT("dept_office"));
  Check(TEXT("hire"),E.Workers.Num()==2,S.FinNotice);Step=2;break;}
 case 2:{const int64 Before=S.Store.PlayerBank+S.Store.PlayerCash+E.Cash;CompanyBuy(Lot,true);const FOKStreetHolding* H=E.Holdings.FindByPredicate([&](const FOKStreetHolding& X){return X.Unit==Lot;});
  Check(TEXT("buy_mortgage"),H&&H->Mode==TEXT("own")&&H->Loan>0&&H->LoanPayment>0&&S.Store.PlayerBank+S.Store.PlayerCash+E.Cash<Before,S.FinNotice);
  CompanyBuild(Lot,TEXT("office_s"));H=E.Holdings.FindByPredicate([&](const FOKStreetHolding& X){return X.Unit==Lot;});
  Check(TEXT("build_start"),H&&H->BuildStart==S.Store.CalendarDay&&H->BuildPaid>0,S.FinNotice);
  // A few days into the work, for the picture.
  auto C=S.Store;for(int32 K=0;K<6;++K){++C.CalendarDay;CompanyDaily(C);}CommitStore(MoveTemp(C));LookAtLot();Step=3;At=Now+4;break;}
 case 3: Shot(TEXT("building"));Step=4;At=Now+1;break;
 case 4:{auto C=S.Store;for(int32 K=0;K<10;++K){++C.CalendarDay;CompanyDaily(C);}CommitStore(MoveTemp(C));
  const FOKStreetHolding* H=E.Holdings.FindByPredicate([&](const FOKStreetHolding& X){return X.Unit==Lot;});const auto* B=OKStreetProperty().Building(TEXT("office_s"));
  int64 Capacity=0;if(const TSharedPtr<FJsonObject> View=CompanyView(S.Store))for(const auto& V:View->GetArrayField(TEXT("holdings")))if(V->AsObject()->GetStringField(TEXT("unit"))==Lot)Capacity=int64(V->AsObject()->GetNumberField(TEXT("capacity")));
  Check(TEXT("build_done"),H&&B&&H->BuildPaid>=B->Cost&&Capacity==B->Capacity,FString::Printf(TEXT("paid %lld"),H?H->BuildPaid:0));
  LookAtLot();Step=5;At=Now+4;break;}
 case 5: Shot(TEXT("built"));Step=6;At=Now+1;break;
 case 6:{ // walk in: the door is open, the walls are not
  const FOKStreetUnitDef U=UnitOf(Lot);const FVector Front=FRotator(0,U.Yaw,0).Vector(),Side=FRotator(0,U.Yaw+90,0).Vector(),C(U.Center.X,U.Center.Y,0);
  const bool DoorOpen=!OKStreetBlockedPoint(C+Front*(550.f-100.f),20.f);const bool WallShut=OKStreetBlockedPoint(C+Front*(550.f-100.f)+Side*300.f,20.f);
  Check(TEXT("door_and_walls"),DoorOpen&&WallShut,FString::Printf(TEXT("door open %d wall %d"),DoorOpen,WallShut));
  S.Male=C+Front*150.f;S.Yaw=U.Yaw+200.f;S.Pitch=-10;S.Distance=260;Step=7;At=Now+3;break;}
 case 7: Shot(TEXT("inside"));Step=8;At=Now+1;break;
 case 8:{ // the month: revenue in, rent, mortgage, tax, wages
  auto C=S.Store;const int64 Loan=E.Holdings.FindByPredicate([&](const FOKStreetHolding& X){return X.Unit==Lot;})->Loan;const int32 Rows=C.Company.Estate.Ledger.Num();
  const bool Done=CompanySettleMonth(C);const FOKStreetHolding* H=C.Company.Estate.Holdings.FindByPredicate([&](const FOKStreetHolding& X){return X.Unit==Lot;});
  Check(TEXT("settle"),Done&&H&&H->Loan<Loan&&C.Company.Estate.Ledger.Num()>Rows+4&&C.Company.Estate.Workers.Num()==2,FString::Printf(TEXT("profit %lld loan %lld->%lld"),C.Company.LastProfit,Loan,H?H->Loan:0));
  CommitStore(MoveTemp(C));Step=9;break;}
 case 9:{ // the save reads back the same
  const FString Path=FPaths::ProjectSavedDir()/TEXT("StreetSocial/company-test.json");FString Error;FOKStreetStore Reloaded;
  const bool Wrote=OKStreetAtomicSave(Path,OKStreetWriteSave(S.Store),Error);const bool Read=Wrote&&OKStreetLoadSaveFile(Path,Reloaded,Error)==EOKStreetLoad::Ok;
  const auto& R=Reloaded.Company.Estate;
  Check(TEXT("save_roundtrip"),Read&&R.Holdings.Num()==E.Holdings.Num()&&R.Workers.Num()==E.Workers.Num()&&R.Ledger.Num()==E.Ledger.Num()&&R.Holdings.ContainsByPredicate([&](const FOKStreetHolding& X){return X.Unit==Lot&&X.Loan==E.Holdings.FindByPredicate([&](const FOKStreetHolding& Y){return Y.Unit==Lot;})->Loan&&X.Building==TEXT("office_s");}),Error);
  IFileManager::Get().Delete(*Path);IFileManager::Get().Delete(*(Path+TEXT(".bak")));Step=10;break;}
 case 10:{ // no money: the lease is lost after two months, people leave, the company dissolves, the land is kept
  auto C=S.Store;for(auto& H:C.Company.Estate.Holdings)if(H.Mode==TEXT("rent"))H.Rent=50000000;for(auto& W:C.Company.Estate.Workers)W.Pay=20000; // more than a month brings in, less than the land is worth
  C.PlayerBank=0;C.PlayerCash=0;C.Company.Estate.Cash=0;CompanySettleMonth(C);C.PlayerBank=0;C.PlayerCash=0;C.Company.Estate.Cash=0;CompanySettleMonth(C);
  const bool Evicted=!C.Company.Estate.Holdings.ContainsByPredicate([](const FOKStreetHolding& X){return X.Unit==TEXT("dept_office");});
  const bool Kept=C.Company.Estate.Holdings.ContainsByPredicate([&](const FOKStreetHolding& X){return X.Unit==Lot&&X.Mode==TEXT("own");});
  Check(TEXT("bankrupt"),Evicted&&C.Company.Founded<0&&C.Company.Estate.Workers.Num()==0,FString::Printf(TEXT("evicted %d founded %d workers %d"),Evicted,C.Company.Founded,C.Company.Estate.Workers.Num()));
  Check(TEXT("land_kept_or_auctioned"),Kept||C.PlayerBank>0,FString::Printf(TEXT("kept %d bank %lld"),Kept,C.PlayerBank));
  Step=11;break;}
 case 11: Check(TEXT("books_no_gap"),BookGaps(Mark)==0,TEXT(""));CommitStore(FOKStreetStore(Saved));BooksRestore(S.TestBooks);S.LotWorldAt=0;UE_LOG(LogTemp,Display,TEXT("COMPANY TEST done: %d passed, %d failed"),Passed,Failed);Step=12;At=Now+3;break;
 case 12: FPlatformMisc::RequestExitWithStatus(false,0);Step=99;break;
 default:break;
 }
}

// Development: -StreetSocialHeroCompanyTest runs the heroines' companies and the Chinese payroll on the live store
// (put back at the end): rich heroines open businesses (one on his place, rented), hire, settle a month (rent to
// him, VAT, contributions), offer him a job, he works a shift (her company pays, his payslip withholds 五險一金 and
// tax), she closes after losing, and the save reads back. "HERO COMPANY TEST" lines, then the game quits.
void AOKStreetSocialScene::TickHeroCompanyTest(){
 auto& S=*State;if(!S.AllReady||!FParse::Param(FCommandLine::Get(),TEXT("StreetSocialHeroCompanyTest")))return;
 static int32 Step=0,Passed=0,Failed=0;static double At=0;static FOKStreetStore Saved;static int64 Mark=0;static FString Boss;
 const double Now=SceneTime();if(At==0)At=Now+6.0;if(Now<At)return;
 auto Check=[&](const TCHAR* Name,bool Ok,const FString& Detail){UE_LOG(LogTemp,Display,TEXT("HERO COMPANY TEST %s: %s %s"),Name,Ok?TEXT("PASS"):TEXT("FAIL"),*Detail);if(Ok)++Passed;else ++Failed;};
 const FString Lot=TEXT("lot_05");
 switch(Step){
 case 0:{S.BooksTickAt=0;TickBooks();Saved=S.Store;S.TestBooks=BooksSnapshot();Mark=OKBookNextSerial();auto C=S.Store;C.HeroCompanies.Reset();C.Company=FOKStreetCompany();C.PlayerBank=500000;
  // His built shop on a lot, offered for rent.
  C.Company.Founded=C.CalendarDay;C.Company.Name=OKT(TEXT("ui.company_app.test_label"));FOKStreetHolding Mine;Mine.Unit=Lot;Mine.Mode=TEXT("own");Mine.Building=TEXT("shop_s");Mine.BuildStart=C.CalendarDay-20;Mine.BuildDone=C.CalendarDay-5;Mine.BuildPaid=90000;Mine.LeaseRent=3000;C.Company.Estate.Holdings.Add(Mine);
  for(int32 I=1;I<=3&&I<S.Manifest.Characters.Num();++I){const int32 RI=OKStreetRelationIndex(C,OKStreetStableId(S.Manifest.Characters[I]),S.Manifest.TalkBudget,true);if(C.Relations.IsValidIndex(RI)){C.Relations[RI].Bank=900000;C.Relations[RI].Debt=0;C.Relations[RI].Stage=TEXT("friend");C.Relations[RI].Favor=167;}}
  OKBookAdjustTo(S.Store,C);
  // Days pass until someone opens (each day is a few percent).
  for(int32 K=0;K<80&&C.HeroCompanies.Num()<2;++K){++C.CalendarDay;CompanyDaily(C);}
  Check(TEXT("open"),C.HeroCompanies.Num()>=1&&C.HeroCompanies[0].Estate.Hq()!=nullptr,FString::Printf(TEXT("%d companies"),C.HeroCompanies.Num()));
  // His place: if nobody took it, a third heroine's shop does.
  if(C.Company.Estate.Holdings[0].Tenant.IsEmpty()){FString Id;for(int32 I=1;I<S.Manifest.Characters.Num()&&Id.IsEmpty();++I)if(!HeroCompanyOf(C,OKStreetStableId(S.Manifest.Characters[I])))Id=OKStreetStableId(S.Manifest.Characters[I]);if(!Id.IsEmpty()){FOKStreetHeroCompany N;N.Owner=Id;N.Kind=TEXT("shop");N.Name=OKT(TEXT("ui.company_app.test_shop_name"));N.Founded=C.CalendarDay;C.HeroCompanies.Add(N);CompanyActOn(C,TEXT("hero_take_place"),[&](FJsonObject& A){A.SetStringField(TEXT("owner"),Id);A.SetStringField(TEXT("kind"),TEXT("shop"));});}}
  const FString Tenant=C.Company.Estate.Holdings[0].Tenant;
  Check(TEXT("rents_his_place"),!Tenant.IsEmpty()&&HeroCompanyOf(C,Tenant)&&HeroCompanyOf(C,Tenant)->Estate.Holdings.ContainsByPredicate([&](const FOKStreetHolding& H){return H.Unit==Lot&&H.Landlord==TEXT("player");}),Tenant);
  Boss=C.HeroCompanies[0].Owner;
  for(int32 K=0;K<20;++K){++C.CalendarDay;CompanyDaily(C);} // past the building work
  int32 Hired=0;for(const auto& H:C.HeroCompanies)Hired+=H.Estate.Workers.Num();Check(TEXT("hires"),Hired>0,FString::Printf(TEXT("%d workers"),Hired));
  CommitStore(MoveTemp(C));SyncHeroJobs();Step=1;break;}
 case 1:{ // the month: VAT and contributions on her book, the rent to him
  auto C=S.Store;auto RentRows=[&]{int32 N=0;for(const auto& L:C.Company.Estate.Ledger)N+=L.Kind==TEXT("rent_income");return N;};const int32 Rows=RentRows();
  while(OKStreetCalendarDateTime(C.CalendarDay,12.0).GetDay()!=1){++C.CalendarDay;CompanyDaily(C);}
  const FOKStreetHeroCompany* H=HeroCompanyOf(C,Boss);
  const bool Vat=H&&H->Estate.Ledger.ContainsByPredicate([](const FOKStreetLedgerRow& L){return L.Kind==TEXT("vat");});
  const bool Rent=RentRows()>Rows; // this month's rent from the tenant, on top of what came in on moving day
  const bool Social=H&&(H->Estate.Workers.IsEmpty()||H->Estate.Ledger.ContainsByPredicate([](const FOKStreetLedgerRow& L){return L.Kind==TEXT("wage")&&L.Note.Contains(OKT(TEXT("ui.accounts_app.social_insurance_label")));}));
  Check(TEXT("month"),H&&H->PaidMonth>=0&&Vat&&Social,FString::Printf(TEXT("profit %lld vat %d social %d"),H?H->LastProfit:0,Vat,Social));
  Check(TEXT("rent_to_him"),Rent||C.Company.Estate.Holdings[0].Tenant.IsEmpty(),FString::Printf(TEXT("rent rows %d->%d"),Rows,RentRows()));
  CommitStore(MoveTemp(C));Step=2;break;}
 case 2:{ // her offer, his yes, a shift: her company pays, his payslip withholds
  {auto C=S.Store;if(FOKStreetHeroCompany* H=HeroCompanyOf(C,Boss)){H->OfferWage=40;H->OfferUntil=C.CalendarDay+7;}CommitStore(MoveTemp(C));}
  HeroOfferAnswer(Boss,true);SyncHeroJobs();
  const FString JobId=TEXT("hc_")+Boss;
  Check(TEXT("job"),S.Store.PlayerJobs.Contains(JobId)&&S.Catalog.FindJob(JobId)!=nullptr,JobId);
  const int64 Cash=S.Store.PlayerCash;const int64 HerFunds=[&]{const int32 RI=OKStreetRelationIndex(S.Store,Boss,S.Manifest.TalkBudget,true);return S.Store.Relations[RI].Bank+FMath::Max<int64>(0,S.Store.Relations[RI].Cash)+HeroCompanyOf(S.Store,Boss)->Estate.Cash;}();
  S.ShiftJob=JobId;S.ShiftAppointment=99999;S.ShiftStart=OKStreetMindNow(S)-240;S.ShiftEnd=OKStreetMindNow(S);S.ShiftChats=0;S.FreeTalkNotice.Reset();EndShift(false);
  const int64 Got=S.Store.PlayerCash-Cash;const int32 RI=OKStreetRelationIndex(S.Store,Boss,S.Manifest.TalkBudget,true);const int64 HerAfter=S.Store.Relations[RI].Bank+FMath::Max<int64>(0,S.Store.Relations[RI].Cash)+HeroCompanyOf(S.Store,Boss)->Estate.Cash;
  Check(TEXT("shift_paid_by_her"),HerAfter<HerFunds&&HeroCompanyOf(S.Store,Boss)->PlayerMinutes>=200,FString::Printf(TEXT("her %lld->%lld"),HerFunds,HerAfter));
  Check(TEXT("payslip"),Got>0&&Got<40*4&&S.FreeTalkNotice.Contains(OKT(TEXT("ui.accounts_app.social_insurance_label"))),FString::Printf(TEXT("got %lld of %d: %s"),Got,40*4,*S.FreeTalkNotice));
  // A full month of pay: his share is 17.5% and the housing fund gets both shares.
  FOKStreetStore T=S.Store;T.Personal.YtdIncome=T.Personal.YtdSocial=T.Personal.YtdTax=0;const int64 Fund=T.Personal.HousingFund;const int64 Before=T.PlayerCash;
  WithholdPlayerWage(T,8000,176*60);const int64 Social=T.Personal.YtdSocial;
  Check(TEXT("social_rates"),Social==1400&&T.Personal.HousingFund-Fund==1120&&Before-T.PlayerCash>=1400,FString::Printf(TEXT("social %lld fund +%lld withheld %lld"),Social,T.Personal.HousingFund-Fund,Before-T.PlayerCash));
  Step=3;break;}
 case 3:{ // three losing months: she closes; his place is free again
  auto C=S.Store;const FString Tenant=C.Company.Estate.Holdings[0].Tenant;FOKStreetHeroCompany* H=HeroCompanyOf(C,Tenant.IsEmpty()?Boss:Tenant);const FString Who=H?H->Owner:FString();
  while(OKStreetCalendarDateTime(C.CalendarDay,12.0).GetDay()!=1)++C.CalendarDay;
  // Nobody to work there and nobody to hire this week: a month with no revenue, the third losing one.
  H=HeroCompanyOf(C,Who);if(H){H->LossStreak=2;H->Estate.Workers.Reset();H->Estate.Applicants.Reset();H->Estate.ApplicantWeek=C.CalendarDay/7;H->PaidMonth=-1;}
  CompanyDaily(C);
  Check(TEXT("closes"),H==nullptr||!HeroCompanyOf(C,Who),Who);
  Check(TEXT("place_free"),C.Company.Estate.Holdings[0].Tenant.IsEmpty(),C.Company.Estate.Holdings[0].Tenant);
  CommitStore(MoveTemp(C));SyncHeroJobs();Check(TEXT("job_gone_with_company"),Who!=Boss||!S.Store.PlayerJobs.Contains(TEXT("hc_")+Boss),TEXT(""));Step=4;break;}
 case 4:{
  const FString Path=FPaths::ProjectSavedDir()/TEXT("StreetSocial/hero-company-test.json");FString Error;FOKStreetStore Reloaded;
  const bool Ok=OKStreetAtomicSave(Path,OKStreetWriteSave(S.Store),Error)&&OKStreetLoadSaveFile(Path,Reloaded,Error)==EOKStreetLoad::Ok;
  Check(TEXT("save_roundtrip"),Ok&&Reloaded.HeroCompanies.Num()==S.Store.HeroCompanies.Num()&&Reloaded.Personal.HousingFund==S.Store.Personal.HousingFund&&Reloaded.Personal.YtdTax==S.Store.Personal.YtdTax
   &&(Reloaded.HeroCompanies.IsEmpty()||Reloaded.HeroCompanies[0].Estate.Holdings.Num()==S.Store.HeroCompanies[0].Estate.Holdings.Num()),Error);
  IFileManager::Get().Delete(*Path);IFileManager::Get().Delete(*(Path+TEXT(".bak")));
  Check(TEXT("books_no_gap"),BookGaps(Mark)==0,TEXT(""));CommitStore(FOKStreetStore(Saved));BooksRestore(S.TestBooks);SyncHeroJobs();S.LotWorldAt=0;UE_LOG(LogTemp,Display,TEXT("HERO COMPANY TEST done: %d passed, %d failed"),Passed,Failed);Step=5;At=Now+3;break;}
 case 5: FPlatformMisc::RequestExitWithStatus(false,0);Step=99;break;
 default:break;
 }
}
