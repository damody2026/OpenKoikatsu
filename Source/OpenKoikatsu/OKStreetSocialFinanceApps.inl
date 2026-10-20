// Included by OKStreetSocialScene.cpp: the full-screen apps of plan §15 — 星運彩券行 counter, 幸運刮刮卡, 星運彩官網,
// the notebook of "memories of the future", ATM / 網銀, and the two terminals (乘風行情 for A shares, 浮標 Buoy for US).
// The frame is built once per open; lists live in boxes that RefreshAppData refills, so typing is never interrupted.

// Wrap: long text (a phone message) breaks into lines in its box instead of running past the edge.
static TSharedRef<SWidget> OKFinText(const FString& T,FLinearColor C=FLinearColor::White,int32 Size=11,bool Bold=false,bool Wrap=false){
 return SNew(STextBlock).ColorAndOpacity(C).Font(FCoreStyle::GetDefaultFontStyle(Bold?"Bold":"Regular",Size)).AutoWrapText(Wrap).Text(FText::FromString(T));
}
static TSharedRef<SWidget> OKFinButton(TWeakObjectPtr<AOKStreetSocialScene> Weak,const FString& T,TFunction<void()> Do,bool Enabled=true,FLinearColor C=FLinearColor::White){
 // Fires on press: the UI may be rebuilt between press and release (loading progress, events), which would drop a click.
 return SNew(SButton).ButtonStyle(OKStreetGlassButton()).ClickMethod(EButtonClickMethod::MouseDown).IsEnabled(Enabled).OnClicked_Lambda([Weak,Do,T](){if(Weak.IsValid()){UE_LOG(LogTemp,Display,TEXT("Street app button: %s"),*T);const FString Before=Weak->FinMessageNow();Do();if(Weak.IsValid())Weak->FinClickDone(Before);}return FReply::Handled();})[SNew(STextBlock).ColorAndOpacity(C).Text(FText::FromString(T))];
}
static FLinearColor OKFinUpDown(double Change,bool RedUp){
 if(FMath::Abs(Change)<1e-9)return FLinearColor(.8f,.8f,.8f);
 const bool Up=Change>0;return (Up==RedUp)?FLinearColor(1.f,.25f,.25f):FLinearColor(.2f,.85f,.4f);
}
static const FSlateBrush* OKFinSolid(){return FCoreStyle::Get().GetBrush("WhiteBrush");}

FString AOKStreetSocialScene::FinMessageNow() const{return State->Message;}

void AOKStreetSocialScene::FinClickDone(const FString& MessageBefore){
 auto& S=*State;if(S.Message.IsEmpty()||S.Message==MessageBefore)return;
 // The press answered with a message only (most refusals): show it where he is looking, then refresh.
 S.FinNotice=S.Message;RefreshUI();
}

void AOKStreetSocialScene::OpenApp(const FString& Name,bool ReadOnly){
 auto& S=*State;UE_LOG(LogTemp,Display,TEXT("Street app open: %s readonly=%d"),*Name,ReadOnly);
 {FString Why;if(ReadOnly&&Name!=TEXT("atm")&&PhoneCutOff(&Why)){S.Message=Why;RefreshUI();return;}}
 if((Name==TEXT("cn")||Name==TEXT("us"))&&!ReadOnly){
  FString Why;S.PcPlace=ComputerHere(&Why);if(S.PcPlace.IsEmpty()){S.Message=Why;RefreshUI();return;}
  S.PcStartMinute=OKStreetMindNow(S);S.PcWarned=false;S.PcPaidHours=0;
  if(S.PcPlace==TEXT("hotel")&&!SpendCash(Local(TEXT("cash"),40),OKT(TEXT("ui.finance.business_center_rate")))){RefreshUI();return;}
  if(S.PcPlace==TEXT("hotel"))S.PcPaidHours=1;
  // Plan §15.6 一起看盤: she sits beside him; it counts as an interaction.
  S.WatchGirl=FreeTalkPartner();S.WatchStart=FPlatformTime::Seconds();
  if(S.WatchGirl!=INDEX_NONE)HeroineNote(S.WatchGirl,TEXT("watch_market"),OKMsg(TEXT("ui.finance_apps.sit_watch_market"), {{TEXT("market"), Name==TEXT("cn")?OKT(TEXT("ui.bank.a_shares_label")):OKT(TEXT("ui.bank.us_stocks_label"))}}),PlaceHere(S.Male),0.45f);
 }
 if(S.App.IsEmpty()){S.AppSavedMouse=S.MouseUI;S.MouseUI=true;ApplyMouseMode(true);}
 S.App=Name;S.AppReadOnly=ReadOnly;S.FinNotice.Reset();
 if(Name==TEXT("cn")||Name==TEXT("us")){
  const int32 M=Name==TEXT("cn")?0:1;if(S.FinWatchCodes[M].IsEmpty())S.FinWatchCodes[M]=M==0?TArray<FString>{TEXT("sh.600519"),TEXT("sh.601318"),TEXT("sz.000858"),TEXT("sh.600036"),TEXT("sz.000002"),TEXT("sz.002594"),TEXT("sz.300059")}:TArray<FString>{TEXT("AAPL"),TEXT("MSFT"),TEXT("NVDA"),TEXT("AMZN"),TEXT("GOOGL"),TEXT("TSLA"),TEXT("NFLX")};
  if(S.FinCode.IsEmpty()||(M==0)!=S.FinCode.Contains(TEXT(".")))S.FinCode=S.FinWatchCodes[M][0];
  S.FinMarket=Name;S.FinPeriod=TEXT("intraday");S.FinOrderQty=M==0?TEXT("100"):TEXT("1");
 }
 RefreshAppData(true);
 RefreshUI();
}

void AOKStreetSocialScene::CloseApp(){
 auto& S=*State;if(S.App.IsEmpty())return;UE_LOG(LogTemp,Display,TEXT("Street app close: %s"),*S.App);
 if(S.WatchGirl!=INDEX_NONE&&FPlatformTime::Seconds()-S.WatchStart>20){
  // Watching long enough teaches her (plan §15.6 教她炒股): from then on she may invest by herself.
  const FString Id=OKStreetStableId(S.Manifest.Characters[S.WatchGirl]);auto Candidate=S.Store;const int32 R=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);
  if(Candidate.Relations.IsValidIndex(R)&&!Candidate.Relations[R].Flags.Contains(TEXT("learned_stocks"))){Candidate.Relations[R].Flags.Add(TEXT("learned_stocks"));CommitStore(MoveTemp(Candidate));MindObserve(S.WatchGirl,TEXT("watch_market"),OKT(TEXT("ui.finance_apps.he_taught_trading")),PlaceHere(S.Male),0.55f);}
 }
 S.WatchGirl=INDEX_NONE;S.App.Reset();S.AppReadOnly=false;S.MouseUI=S.AppSavedMouse;ApplyMouseMode(true);RefreshUI();
}

void AOKStreetSocialScene::RefreshAppData(bool Full){
 auto& S=*State;if(S.App.IsEmpty())return;FString Error;
 S.AppRefreshAt=FPlatformTime::Seconds()+((S.App==TEXT("cn")||S.App==TEXT("us"))?1.0:4.0);
 auto Args=[](std::initializer_list<TPair<const TCHAR*,FString>> L){auto O=MakeShared<FJsonObject>();for(const auto& P:L)O->SetStringField(P.Key,P.Value);return O;};
 if(S.App==TEXT("lottery")||S.App==TEXT("web")){
  auto A=MakeShared<FJsonObject>();A->SetNumberField(TEXT("history"),S.App==TEXT("web")?100:10);
  if(auto R=Fin(TEXT("lotto_info"),A,Error))S.FinLotto=R;
  auto B=MakeShared<FJsonObject>();B->SetNumberField(TEXT("days"),10);B->SetNumberField(TEXT("back"),3);
  if(Full||S.App==TEXT("web"))if(auto R=Fin(TEXT("sports_board"),B,Error))S.FinBoard=R;
  if(auto R=Fin(TEXT("tickets"),nullptr,Error))S.FinTickets=R;
 }
 if(S.App==TEXT("notebook")&&Full){auto A=MakeShared<FJsonObject>();A->SetNumberField(TEXT("days"),30);if(auto R=Fin(TEXT("notebook"),A,Error))S.FinNotebook=R;}
 if(S.App==TEXT("atm")){if(auto R=Fin(TEXT("summary"),nullptr,Error))S.FinSummary=R;if(auto R=Fin(TEXT("tickets"),nullptr,Error))S.FinTickets=R;if(Full)S.FinAlerts=Fin(TEXT("alerts"),nullptr,Error);}
 if(S.App==TEXT("venture")&&S.Manifest.Characters.IsValidIndex(S.VentureHeroine)&&S.VentureHeroine>0)
  S.HerAccount=Fin(TEXT("account"),Args({{TEXT("market"),TEXT("cn")},{TEXT("owner"),OKStreetStableId(S.Manifest.Characters[S.VentureHeroine])}}),Error);
 if(S.App==TEXT("cn")||S.App==TEXT("us")){
  const FString M=S.App;const int32 Mi=M==TEXT("cn")?0:1;
  if(auto R=Fin(TEXT("quote"),Args({{TEXT("market"),M},{TEXT("code"),S.FinCode}}),Error))S.FinQuote=R;else S.FinNotice=Error;
  auto W=Args({{TEXT("market"),M}});TArray<TSharedPtr<FJsonValue>> Codes;for(const FString& C:S.FinWatchCodes[Mi])Codes.Add(MakeShared<FJsonValueString>(C));W->SetArrayField(TEXT("codes"),Codes);
  if(auto R=Fin(TEXT("quotes"),W,Error))S.FinWatch=R;
  if(Full)S.FinAlerts=Fin(TEXT("alerts"),Args({{TEXT("market"),M}}),Error);
  if(auto R=Fin(TEXT("account"),Args({{TEXT("market"),M}}),Error))S.FinAccount=R;
  const bool Live=S.FinQuote&&OKFinObj(S.FinQuote,TEXT("session"))&&OKFinObj(S.FinQuote,TEXT("session"))->GetBoolField(TEXT("open"));
  if(Full||S.FinPeriod==TEXT("intraday")||Live){
   auto C=Args({{TEXT("market"),M},{TEXT("code"),S.FinCode}});FString Period=S.FinPeriod;int32 Count=120;
   if(M==TEXT("us")){ // Buoy ranges map onto daily, weekly or monthly bars
    if(Period==TEXT("5D")){Period=TEXT("d");Count=5;}else if(Period==TEXT("1M")){Period=TEXT("d");Count=22;}else if(Period==TEXT("6M")){Period=TEXT("d");Count=126;}
    else if(Period==TEXT("YTD")){Period=TEXT("d");Count=FMath::Max(5,StreetNow().GetDayOfYear()*5/7);}else if(Period==TEXT("1Y")){Period=TEXT("d");Count=252;}else if(Period==TEXT("5Y")){Period=TEXT("w");Count=260;}else if(Period==TEXT("MAX")){Period=TEXT("m");Count=400;}
   }
   C->SetStringField(TEXT("period"),Period==TEXT("1D")?TEXT("intraday"):Period);C->SetNumberField(TEXT("count"),Count);
   if(auto R=Fin(TEXT("chart"),C,Error)){
    if(!S.FinChart)S.FinChart=MakeShared<FOKChartData>();auto& D=*S.FinChart;
    D.RedUp=M==TEXT("cn");D.Dark=M==TEXT("cn");D.O.Reset();D.H.Reset();D.L.Reset();D.C.Reset();D.V.Reset();D.Dates.Reset();D.Flags.Reset();
    const bool Intraday=R->HasField(TEXT("points"));D.Line=Intraday||M==TEXT("us");
    if(Intraday){
     D.PrevClose=float(OKFinNum(R,TEXT("prev_close")));D.LimitUp=float(OKFinNum(R,TEXT("limit_up")));D.LimitDown=float(OKFinNum(R,TEXT("limit_down")));D.SessionPoints=int32(OKFinNum(R,TEXT("minutes"),270))+1;
     const int32 Open=M==TEXT("cn")?9*60:9*60+30;int32 K=0;
     for(const auto& V:OKFinArr(R,TEXT("points"))){const float P=float(V->AsNumber());D.O.Add(P);D.H.Add(P);D.L.Add(P);D.C.Add(P);D.V.Add(1);D.Dates.Add(FString::Printf(TEXT("%02d:%02d"),(Open+K)/60,(Open+K)%60));++K;}
     D.Title=OKMsg(TEXT("ui.finance_apps.intraday_date"), {{TEXT("date"), *OKFinStr(R,TEXT("date"))}});
    }else{
     for(const auto& V:OKFinArr(R,TEXT("bars"))){const auto B=V->AsObject();D.O.Add(float(OKFinNum(B,TEXT("o"))));D.H.Add(float(OKFinNum(B,TEXT("h"))));D.L.Add(float(OKFinNum(B,TEXT("l"))));D.C.Add(float(OKFinNum(B,TEXT("c"))));D.V.Add(float(OKFinNum(B,TEXT("v"))));D.Dates.Add(OKFinStr(B,TEXT("date")));}
     D.SessionPoints=D.C.Num();D.PrevClose=D.Line&&D.C.Num()?D.O[0]:0.f;D.LimitUp=D.LimitDown=0;
     D.Title=M==TEXT("cn")?(S.FinPeriod==TEXT("d")?OKT(TEXT("ui.finance_apps.daily_k_tab")):S.FinPeriod==TEXT("w")?OKT(TEXT("ui.finance_apps.weekly_k_tab")):OKT(TEXT("ui.finance_apps.monthly_k_tab"))):S.FinPeriod;
     // 前世記憶 flags on index big days (only those already in the past; the chart never holds future bars).
     if(S.FinNotebook)for(const auto& Y:OKFinArr(S.FinNotebook,TEXT("index_years")))for(const auto& Big:OKFinArr(Y->AsObject(),TEXT("big_days"))){const int32 I=D.Dates.IndexOfByKey(OKFinStr(Big->AsObject(),TEXT("date")));if(I!=INDEX_NONE)D.Flags.Add(I);}
    }
   }
  }
  if(Full||FPlatformTime::Seconds()>S.FinMoversAt){S.FinMoversAt=FPlatformTime::Seconds()+20;if(auto R=Fin(TEXT("movers"),Args({{TEXT("market"),M}}),Error))S.FinMovers=R;}
  // Watching together: limit-up and big swings are worth a reaction (plan §15.6).
  if(S.WatchGirl!=INDEX_NONE&&S.FinQuote){const double Pct=OKFinNum(S.FinQuote,TEXT("pct"));const FString Key=S.FinCode+OKFinStr(S.FinQuote,TEXT("date"));
   if((S.FinQuote->HasField(TEXT("at_limit_up"))&&S.FinQuote->GetBoolField(TEXT("at_limit_up")))||FMath::Abs(Pct)>=7){if(!S.FinReacted.Contains(Key)){S.FinReacted.Add(Key);S.MindRecent.Add(OKStreetStableId(S.Manifest.Characters[S.WatchGirl]),OKMsg(TEXT("ui.finance_apps.saw_stock_move_together"), {{TEXT("stock_name"), *OKFinStr(S.FinQuote,TEXT("name"))}, {TEXT("direction"), Pct>0?OKT(TEXT("ui.finance_apps.big_rise")):OKT(TEXT("ui.finance_apps.big_fall"))}, {TEXT("pct"), Pct}}));}}}
 }
 PopulateApp();
}

TSharedRef<SWidget> AOKStreetSocialScene::BuildAppOverlay(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 S.AppBoxes.SetNum(6);for(auto& B:S.AppBoxes)B=SNew(SVerticalBox);
 const bool Cn=S.App==TEXT("cn"),Us=S.App==TEXT("us"),Web=S.App==TEXT("web");
 const FLinearColor Bg=S.App==TEXT("bankrupt")?FLinearColor(.01f,.0f,.0f,.99f):Cn?FLinearColor(.015f,.015f,.02f,.98f):Us?FLinearColor(.96f,.97f,.98f,.98f):Web?FLinearColor(.99f,.96f,.92f,.98f):FLinearColor(.08f,.03f,.03f,.96f);
 const FLinearColor Ink=Us||Web?FLinearColor(.1f,.1f,.15f):FLinearColor::White;
 FString Title=S.App==TEXT("bankrupt")?OKT(TEXT("ui.accounts_app.ideographic_space")):S.App==TEXT("phone")?OKT(TEXT("ui.finance_apps.phone_label")):S.App==TEXT("company")?OKT(TEXT("ui.finance_apps.my_company_label")):S.App==TEXT("accounts")?OKT(TEXT("ui.company_app.ledger_book_title")):S.App==TEXT("lottery")?(S.AppReadOnly?OKT(TEXT("ui.finance_apps.my_lottery_label")):OKT(TEXT("ui.finance_apps.lottery_counter_sign"))):S.App==TEXT("scratch")?OKT(TEXT("ui.accounts_app.scratch_card_label")):Web?OKT(TEXT("ui.finance_apps.official_site_link")):S.App==TEXT("notebook")?OKT(TEXT("ui.finance_apps.notebook_title")):S.App==TEXT("atm")?OKT(TEXT("ui.finance_apps.atm_online_bank")):S.App==TEXT("dept")?OKT(TEXT("ui.finance_apps.dept_office")):S.App==TEXT("timetable")?OKT(TEXT("ui.finance_apps.my_timetable")):S.App==TEXT("venture")?(S.Manifest.Characters.IsValidIndex(S.VentureHeroine)&&S.VentureHeroine>0?OKMsg(TEXT("ui.finance_apps.talk_money_work"), {{TEXT("heroine_name"), *S.Manifest.Characters[S.VentureHeroine].Name}}):FString(OKT(TEXT("ui.finance_apps.my_company_label")))):Cn?OKT(TEXT("ui.finance_apps.a_share_app_label")):OKT(TEXT("ui.finance_apps.buoy_brokerage_name"));
 if(S.AppReadOnly&&S.App!=TEXT("venture")&&S.App!=TEXT("lottery")&&S.App!=TEXT("timetable"))Title+=OKT(TEXT("ui.finance_apps.mobile_view_only"));
 auto Header=SNew(SHorizontalBox)
  +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[OKFinText(Title,Cn?FLinearColor(1,.3f,.2f):Us?FLinearColor(.05f,.45f,.9f):Web?FLinearColor(.75f,.05f,.05f):FLinearColor(1,.85f,.3f),18,true)]
  +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[SNew(STextBlock).ColorAndOpacity(Ink).Text_Lambda([Weak](){if(!Weak.IsValid())return FText::GetEmpty();const FDateTime N=Weak->StreetNow();return FText::FromString(OKMsg(TEXT("ui.finance_apps.wallet_balance_line"), {{TEXT("time"), *N.ToString(TEXT("%Y-%m-%d %H:%M"))}, {TEXT("cash"), Weak->State->Store.PlayerCash}}));})]
  +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.close_esc_button")),[Weak](){Weak->CloseApp();})];
 TSharedRef<SWidget> Body=SNullWidget::NullWidget;
 if(S.App==TEXT("lottery"))Body=BuildLotteryApp();
 else if(S.App==TEXT("scratch"))Body=BuildScratchApp();
 else if(Web)Body=BuildWebApp();
 else if(S.App==TEXT("notebook"))Body=SNew(SScrollBox)+SScrollBox::Slot()[S.AppBoxes[0].ToSharedRef()];
 else if(S.App==TEXT("atm"))Body=BuildAtmApp();
 else if(S.App==TEXT("venture"))Body=BuildVentureApp();
 else if(S.App==TEXT("bankrupt"))Body=BuildBankruptApp();
 else if(S.App==TEXT("dept")||S.App==TEXT("timetable"))Body=BuildCampusApp(S.App==TEXT("dept"));
 else if(S.App==TEXT("phone"))Body=BuildPhoneApp(); // OKStreetSocialPhoneInbox.inl
 else if(S.App==TEXT("company"))Body=BuildCompanyApp(); // OKStreetSocialCompanyApp.inl
 else if(S.App==TEXT("accounts"))Body=BuildAccountsApp(); // OKStreetSocialAccountsApp.inl
 else Body=BuildTerminalApp(Cn);
 PopulateApp();
 return SNew(SBox).WidthOverride(1320).HeightOverride(820)[SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(Bg).Padding(12)[SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[Header]
  +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)[SNew(STextBlock).ColorAndOpacity(FLinearColor(1,.8f,.2f)).AutoWrapText(true).Text_Lambda([Weak](){return Weak.IsValid()?FText::FromString(Weak->State->FinNotice):FText::GetEmpty();})]
  +SVerticalBox::Slot().FillHeight(1)[Body]]];
}

// ------------------------------------------------------------------------------------------------ 星運彩券行

TSharedRef<SWidget> AOKStreetSocialScene::BuildLotteryApp(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Tabs=SNew(SHorizontalBox);
 if(S.AppReadOnly&&S.LotTab!=TEXT("mine")&&S.LotTab!=TEXT("board"))S.LotTab=TEXT("mine");
 // 競技彩 first: real results are what a man who lived it once can use (plan §15).
 const TArray<TPair<FString,FString>> AllTabs={TPair<FString,FString>(TEXT("sports"),OKT(TEXT("ui.finance.sports_lottery_label"))),TPair<FString,FString>(TEXT("star649"),OKT(TEXT("ui.finance_apps.starlight_lotto_name"))),TPair<FString,FString>(TEXT("twinstar"),OKT(TEXT("ui.finance_apps.double_star_name"))),TPair<FString,FString>(TEXT("scratch"),OKT(TEXT("ui.accounts_app.scratch_card_label"))),TPair<FString,FString>(TEXT("mine"),OKT(TEXT("ui.finance_apps.my_tickets_claim"))),TPair<FString,FString>(TEXT("board"),OKT(TEXT("ui.finance_apps.draw_board")))};
 const TArray<TPair<FString,FString>> PhoneTabs={TPair<FString,FString>(TEXT("mine"),OKT(TEXT("ui.finance_apps.my_lottery_label"))),TPair<FString,FString>(TEXT("board"),OKT(TEXT("ui.finance_apps.draw_results")))};
 for(const auto& T:S.AppReadOnly?PhoneTabs:AllTabs){
  const FString Key=T.Key;Tabs->AddSlot().AutoWidth().Padding(2)[OKFinButton(Weak,(S.LotTab==Key?TEXT("▶ "):TEXT(""))+T.Value,[Weak,Key](){auto& L=*Weak->State;L.LotTab=Key;if(Key==TEXT("star649")||Key==TEXT("twinstar")){L.LotGame=Key;L.LotPicks.Reset();}Weak->RefreshUI();})];
 }
 TSharedRef<SVerticalBox> Static=SNew(SVerticalBox);
 if(S.LotTab==TEXT("sports")){
  Static->AddSlot().AutoHeight()[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(OKT(TEXT("ui.finance_apps.per_lot_amount_label")))]
   +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SBox).WidthOverride(120)[SNew(SEditableTextBox).Text(FText::FromString(S.BetStake)).OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->BetStake=T.ToString();})]]
   +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[OKFinButton(Weak,OKT(TEXT("ui.finance.bet_label")),[Weak](){Weak->PlaceBetSlip();})]
   +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.clear_slip_button")),[Weak](){Weak->State->BetSlip.Reset();Weak->PopulateApp();})]];
 }
 return SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[Tabs]
  +SVerticalBox::Slot().FillHeight(1)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(.62f)[SNew(SScrollBox)+SScrollBox::Slot()[S.AppBoxes[0].ToSharedRef()]]
   +SHorizontalBox::Slot().FillWidth(.38f).Padding(10,0,0,0)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Static]+SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[S.AppBoxes[1].ToSharedRef()]]]];
}

void AOKStreetSocialScene::BuyLotto(int32 Quick){
 auto& S=*State;FString Error;
 if(S.AppReadOnly||!AtLotteryCounter()){S.FinNotice=OKT(TEXT("ui.finance_apps.lottery_at_counter_only"));return;}
 const bool Twin=S.LotGame==TEXT("twinstar");const int64 Price=Local(TEXT("price"),Twin?20:10);
 auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("game"),S.LotGame);A->SetNumberField(TEXT("draws"),S.LotDraws);
 if(Quick>0)A->SetNumberField(TEXT("quick"),Quick);
 else{
  if(S.LotPicks.Num()!=6){S.FinNotice=OKT(TEXT("ui.finance_apps.pick_six_prompt"));return;}
  TArray<int32> Nums=S.LotPicks.Array();Nums.Sort();TArray<TSharedPtr<FJsonValue>> J;for(int32 N:Nums)J.Add(MakeShared<FJsonValueNumber>(N));
  auto Line=MakeShared<FJsonObject>();Line->SetArrayField(TEXT("numbers"),J);Line->SetNumberField(TEXT("zone2"),S.LotZone2);A->SetArrayField(TEXT("lines"),{MakeShared<FJsonValueObject>(Line)});
 }
 const int32 Partner=FreeTalkPartner();if(Partner!=INDEX_NONE)A->SetStringField(TEXT("with"),OKStreetStableId(S.Manifest.Characters[Partner]));
 const int64 Cost=Price*FMath::Max(1,Quick)*S.LotDraws;
 if(!SpendCash(Cost,Twin?OKT(TEXT("ui.finance_apps.double_star_name")):OKT(TEXT("ui.finance_apps.starlight_lotto_name"))))return;
 const auto R=Fin(TEXT("lotto_buy"),A,Error);
 if(!R){GainCash(Cost,OKT(TEXT("ui.finance_apps.ticket_not_issued_refund")));S.FinNotice=Error;return;}
 TArray<FString> Lines;for(const auto& L:OKFinArr(R,TEXT("lines"))){TArray<FString> N;for(const auto& V:OKFinArr(L->AsObject(),TEXT("numbers")))N.Add(FString::Printf(TEXT("%02d"),int32(V->AsNumber())));Lines.Add(FString::Join(N,TEXT(" "))+(Twin?OKMsg(TEXT("ui.finance_apps.zone2_plus_suffix"), {{TEXT("zone2_extra"), int32(OKFinNum(L->AsObject(),TEXT("zone2")))}}):FString()));}
 S.FinNotice=OKMsg(TEXT("ui.finance_apps.lottery_ticket_issued"), {{TEXT("lines"), *FString::Join(Lines,OKT(TEXT("ui.finance_apps.separator_bar")))}, {TEXT("draw_count"), S.LotDraws}, {TEXT("first_draw_date"), *OKFinStr(R,TEXT("first_draw"))}, {TEXT("cost"), Cost}});
 auto Candidate=S.Store;Candidate.Inventory.Add(FString::Printf(TEXT("lottery_ticket:%s"),*S.LotGame));CommitStore(MoveTemp(Candidate));
 if(Partner!=INDEX_NONE)HeroineNote(Partner,TEXT("lottery_together"),OKMsg(TEXT("ui.finance_apps.bought_together"), {{TEXT("product"), Twin?OKT(TEXT("ui.finance_apps.double_star_name")):OKT(TEXT("ui.finance_apps.starlight_lotto_name"))}, {TEXT("lines"), *FString::Join(Lines,OKT(TEXT("ui.finance_apps.separator_bar")))}}),TEXT("lottery"),0.45f);
 S.LotPicks.Reset();RefreshAppData(true);
}

void AOKStreetSocialScene::PlaceBetSlip(){
 auto& S=*State;FString Error;
 if(S.AppReadOnly||!AtLotteryCounter()){S.FinNotice=OKT(TEXT("ui.finance_apps.bet_at_counter_only"));return;}
 if(S.BetSlip.IsEmpty()){S.FinNotice=OKT(TEXT("ui.finance_apps.bet_slip_empty"));return;}
 const int64 Stake=MoneyIn(S.BetStake);if(Stake<Local(TEXT("cash"),10)||Stake>Local(TEXT("cash"),2000)){S.FinNotice=OKT(TEXT("ui.finance_apps.per_lot_range_note"));return;}
 if(!SpendCash(Stake,OKT(TEXT("ui.finance.sports_lottery_label"))))return;
 auto B=MakeShared<FJsonObject>();B->SetNumberField(TEXT("stake"),double(Stake));TArray<TSharedPtr<FJsonValue>> Sel;for(const auto& X:S.BetSlip)Sel.Add(MakeShared<FJsonValueObject>(X));B->SetArrayField(TEXT("selections"),Sel);
 const int32 Partner=FreeTalkPartner();if(Partner!=INDEX_NONE)B->SetStringField(TEXT("with"),OKStreetStableId(S.Manifest.Characters[Partner]));
 const auto R=Fin(TEXT("sports_bet"),B,Error);
 if(!R){GainCash(Stake,OKT(TEXT("ui.finance.bet_void_refund")));S.FinNotice=Error;PopulateApp();return;}
 S.FinNotice=OKMsg(TEXT("ui.finance_apps.ticket_summary_header"), {{TEXT("ticket_no"), OKFinNum(R,TEXT("ticket"))}, {TEXT("match_count"), S.BetSlip.Num()}, {TEXT("odds"), OKFinNum(R,TEXT("odds"))}, {TEXT("max_payout"), OKFinNum(R,TEXT("max_payout"))}});
 if(Partner!=INDEX_NONE)HeroineNote(Partner,TEXT("sports_bet"),OKT(TEXT("ui.finance_apps.he_bought_sports_bet"))+S.FinNotice,TEXT("lottery"),0.4f);
 S.BetSlip.Reset();RefreshAppData(true);
}

// Her lucky numbers (profile tastes), her birthday and the day they met fill the pick (plan §15.7.2 和女主一起選號).
void AOKStreetSocialScene::PickHerNumbers(){
 auto& S=*State;const int32 Her=FreeTalkPartner();if(Her==INDEX_NONE)return;const auto P=ProfileOf(Her);const int32 Max=S.LotGame==TEXT("twinstar")?38:49;
 S.LotPicks.Reset();
 const TSharedPtr<FJsonObject>* Tastes=nullptr;if(P&&P->TryGetObjectField(TEXT("tastes"),Tastes))for(const auto& V:OKFinArr(*Tastes,TEXT("lucky_numbers"))){const int32 N=int32(V->AsNumber());if(N>=1&&N<=Max&&S.LotPicks.Num()<6)S.LotPicks.Add(N);}
 TArray<FString> B;OKStreetProfileString(P,nullptr,TEXT("birthday")).ParseIntoArray(B,TEXT("-"));
 if(B.Num()==3){for(const int32 N:{FCString::Atoi(*B[1]),FCString::Atoi(*B[2])})if(N>=1&&N<=Max&&S.LotPicks.Num()<6)S.LotPicks.Add(N);}
 const FDateTime Now=StreetNow();for(const int32 N:{Now.GetDay(),Now.GetMonth(),7,17,23,36})if(N>=1&&N<=Max&&S.LotPicks.Num()<6)S.LotPicks.Add(N);
 TArray<int32> Nums=S.LotPicks.Array();Nums.Sort();TArray<FString> T;for(int32 N:Nums)T.Add(FString::FromInt(N));
 S.FinNotice=OKMsg(TEXT("ui.finance_apps.her_picked_numbers"), {{TEXT("numbers"), *FString::Join(T,OKT(TEXT("ui.bank.list_separator")))}});
 HeroineNote(Her,TEXT("lottery_together"),OKMsg(TEXT("ui.finance_apps.picked_numbers_for_him"), {{TEXT("numbers"), *FString::Join(T,OKT(TEXT("ui.bank.list_separator")))}}),TEXT("lottery"),0.5f);
 PopulateApp();
}

void AOKStreetSocialScene::StartScratch(const FString& Theme){
 auto& S=*State;FString Error;int64 Price=Local(TEXT("price"),20);
 if(S.AppReadOnly||!AtLotteryCounter()){S.FinNotice=OKT(TEXT("ui.finance_apps.scratch_at_counter_only"));return;}
 for(const auto& V:OKFinArr(S.FinLotto,TEXT("scratch")))if(OKFinStr(V->AsObject(),TEXT("id"))==Theme)Price=Local(TEXT("price"),int64(OKFinNum(V->AsObject(),TEXT("price"),20)));
 if(!SpendCash(Price,OKT(TEXT("ui.accounts_app.scratch_card_label"))))return;
 auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("theme"),Theme);A->SetNumberField(TEXT("count"),1);
 const auto R=Fin(TEXT("scratch_buy"),A,Error);if(!R){GainCash(Price,OKT(TEXT("ui.finance_apps.ticket_not_issued_refund")));S.FinNotice=Error;return;}
 S.ScratchTheme=Theme;
 const auto T=OKFinArr(R,TEXT("tickets"));S.ScratchTicket=T.Num()?int64(T[0]->AsNumber()):0;
 S.Scratch=MakeShared<FOKScratchState>();S.Scratch->Price=int32(Price);
 for(const auto& V:OKFinArr(S.FinLotto,TEXT("scratch")))if(OKFinStr(V->AsObject(),TEXT("id"))==Theme)S.Scratch->Theme=OKFinStr(V->AsObject(),TEXT("name"));
 S.App=TEXT("scratch");RefreshUI();
}

TSharedRef<SWidget> AOKStreetSocialScene::BuildScratchApp(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);TSharedPtr<SOKScratchCard> Card;
 auto Widget=SAssignNew(Card,SOKScratchCard).Card(S.Scratch)
  .OnStart_Lambda([Weak](){if(!Weak.IsValid())return;auto& L=*Weak->State;FString Error;auto A=MakeShared<FJsonObject>();A->SetNumberField(TEXT("ticket"),double(L.ScratchTicket));
   // The card is decided the moment it is scratched (plan §15.7.3).
   if(const auto R=Weak->Fin(TEXT("scratch_reveal"),A,Error)){const auto T=OKFinObj(R,TEXT("ticket"));L.Scratch->Cells.Reset();for(const auto& V:OKFinArr(T,TEXT("cells")))L.Scratch->Cells.Add(int64(V->AsNumber()));L.Scratch->Win=int64(OKFinNum(R,TEXT("win")));}})
  .OnDone_Lambda([Weak](){if(!Weak.IsValid())return;auto& L=*Weak->State;L.FinNotice=L.Scratch->Win>0?OKMsg(TEXT("ui.finance_apps.scratch_won_amount"), {{TEXT("win_amount"), L.Scratch->Win}}):OKT(TEXT("ui.finance_apps.not_won_message"));
   const int32 Her=Weak->FreeTalkPartner();if(Her!=INDEX_NONE)Weak->HeroineNote(Her,TEXT("lottery_together"),L.Scratch->Win>0?OKMsg(TEXT("ui.finance_apps.he_scratch_won"), {{TEXT("amount"), L.Scratch->Win}}):OKT(TEXT("ui.finance_apps.he_scratch_lost")),TEXT("lottery"),0.35f);});
 TWeakPtr<SOKScratchCard> WeakCard=Card;
 return SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(10)[Widget]
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().Padding(4)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.scratch_all")),[WeakCard](){if(auto C=WeakCard.Pin())C->RevealAll();})]
   +SHorizontalBox::Slot().AutoWidth().Padding(4)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.buy_another")),[Weak](){auto& L=*Weak->State;const FString Theme=L.ScratchTheme;Weak->StartScratch(Theme);})]
   +SHorizontalBox::Slot().AutoWidth().Padding(4)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.return_to_counter")),[Weak](){Weak->State->App=TEXT("lottery");Weak->State->LotTab=TEXT("mine");Weak->RefreshAppData(true);Weak->RefreshUI();})]];
}

// ------------------------------------------------------------------------------------------------ 星運彩官網

TSharedRef<SWidget> AOKStreetSocialScene::BuildWebApp(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Menu=SNew(SHorizontalBox);
 for(const auto& T:{TPair<FString,FString>(TEXT("home"),OKT(TEXT("ui.finance_apps.home_page"))),TPair<FString,FString>(TEXT("draws"),OKT(TEXT("ui.finance_apps.draw_announcement"))),TPair<FString,FString>(TEXT("rules"),OKT(TEXT("ui.finance_apps.how_to_play"))),TPair<FString,FString>(TEXT("sports"),OKT(TEXT("ui.finance.sports_lottery_label"))),TPair<FString,FString>(TEXT("trend"),OKT(TEXT("ui.finance_apps.chart_tab"))),TPair<FString,FString>(TEXT("shop"),OKT(TEXT("ui.finance_apps.store_info"))),TPair<FString,FString>(TEXT("care"),OKT(TEXT("ui.finance_apps.responsible_lottery_title")))}){
  const FString Key=T.Key;Menu->AddSlot().AutoWidth().Padding(2)[OKFinButton(Weak,T.Value,[Weak,Key](){Weak->State->WebTab=Key;Weak->PopulateApp();},true,S.WebTab==Key?FLinearColor(1,.85f,.2f):FLinearColor::White)];
 }
 return SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(FLinearColor(.7f,.04f,.04f)).Padding(8)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,16,0)[OKFinText(OKT(TEXT("ui.finance_apps.star_lottery_brand")),FLinearColor(1,.85f,.2f),20,true)]
   +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Menu]]]
  +SVerticalBox::Slot().FillHeight(1).Padding(0,8)[SNew(SScrollBox)+SScrollBox::Slot()[S.AppBoxes[0].ToSharedRef()]]
  +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(FLinearColor(.25f,.05f,.05f)).Padding(6)[OKFinText(OKT(TEXT("ui.finance_apps.age_gate_footer")),FLinearColor(1,.9f,.8f),10)]];
}

// ------------------------------------------------------------------------------------------------ ATM / 網銀

TSharedRef<SWidget> AOKStreetSocialScene::BuildAtmApp(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Tabs=SNew(SWrapBox).UseAllottedSize(true);
 for(const auto& T:TArray<TPair<FString,FString>>{{TEXT("cash"),OKT(TEXT("ui.finance_apps.deposit_withdraw_claim"))},{TEXT("deposit"),OKT(TEXT("ui.finance_apps.fixed_deposit"))},{TEXT("card"),OKT(TEXT("ui.finance_apps.credit_card_label"))},{TEXT("fees"),OKT(TEXT("ui.finance_apps.bills_loans_tab"))},{TEXT("worth"),OKT(TEXT("ui.finance_apps.total_assets"))},{TEXT("alerts"),OKT(TEXT("ui.finance_apps.price_alert_title"))}}){
  const FString Key=T.Key;Tabs->AddSlot().Padding(2)[OKFinButton(Weak,T.Value,[Weak,Key](){Weak->State->AtmTab=Key;Weak->State->FinNotice.Reset();FString E;if(Key==TEXT("alerts"))Weak->State->FinAlerts=Weak->Fin(TEXT("alerts"),nullptr,E);Weak->RefreshUI();},true,S.AtmTab==Key?FLinearColor(1,.85f,.3f):FLinearColor::White)];}
 if(S.AtmTab!=TEXT("cash"))return SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Tabs]+SVerticalBox::Slot().FillHeight(1).Padding(0,6)[BuildBankTab()];
 auto Move=[Weak](int64 Amount,bool Deposit){auto& L=*Weak->State;auto C=L.Store;
  if(Deposit)Amount=Amount<0?C.PlayerCash:FMath::Min(Amount,C.PlayerCash);else Amount=Amount<0?C.PlayerBank:FMath::Min(Amount,C.PlayerBank);
  const int64 Sign=Deposit?1:-1;OKCash(C,-Sign*Amount,TEXT("atm"),Deposit?OKT(TEXT("ui.finance_apps.deposit_current_account")):OKT(TEXT("ui.finance_apps.withdraw_from_current")));OKBank(C,Sign*Amount,TEXT("atm"),Deposit?OKT(TEXT("ui.finance_apps.deposit_from_wallet")):OKT(TEXT("ui.finance_apps.withdraw_to_wallet")));
  if(Amount>0&&Weak->CommitStore(MoveTemp(C)))L.FinNotice=FString(Deposit?OKT(TEXT("ui.finance_apps.deposit_label")):OKT(TEXT("ui.finance_apps.withdraw_label_short")))+TEXT(" ")+OKStreetMoneyText(Amount);Weak->PopulateApp();};
 auto Row=SNew(SWrapBox).UseAllottedSize(true);
 Row->AddSlot().Padding(2)[Tabs];
 for(const int64 A:{200,1000,2000,10000})Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.finance_apps.withdraw_amount"), {{TEXT("amount"), A}}),[Move,A](){Move(A,false);})];
 for(const int64 A:{200,1000,2000,-1})Row->AddSlot().Padding(2)[OKFinButton(Weak,A<0?OKT(TEXT("ui.finance_apps.deposit_all")):OKMsg(TEXT("ui.finance_apps.deposit_amount"), {{TEXT("amount"), A}}),[Move,A](){Move(A,true);})];
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.claim_deposit_large_prize")),[Weak](){FString Error;auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("where"),TEXT("atm"));if(const auto R=Weak->Fin(TEXT("claim"),A,Error)){const int64 Paid=int64(OKFinNum(R,TEXT("paid")));if(Paid>0){auto C=Weak->State->Store;OKBank(C,Paid,TEXT("prize"),OKT(TEXT("ui.finance_apps.lottery_prize_credited")));Weak->CommitStore(MoveTemp(C));Weak->State->FinNotice=OKMsg(TEXT("ui.finance_apps.prize_deposited"), {{TEXT("amount"), Paid}});}else Weak->State->FinNotice=OKT(TEXT("ui.finance_apps.no_pending_prize"));}Weak->RefreshAppData(true);},true,FLinearColor(1,.85f,.3f))];
 return SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Row]+SVerticalBox::Slot().FillHeight(1).Padding(0,8)[SNew(SScrollBox)+SScrollBox::Slot()[S.AppBoxes[0].ToSharedRef()]];
}

// ------------------------------------------------------------------------------------------------ terminals

TSharedRef<SWidget> AOKStreetSocialScene::BuildTerminalApp(bool Cn){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 const FLinearColor Ink=Cn?FLinearColor(.85f,.85f,.9f):FLinearColor(.1f,.1f,.15f);
 if(!S.FinChart)S.FinChart=MakeShared<FOKChartData>();
 auto Periods=SNew(SHorizontalBox);
 const TArray<TPair<FString,FString>> P=Cn?TArray<TPair<FString,FString>>{{TEXT("intraday"),OKT(TEXT("ui.finance_apps.intraday_label"))},{TEXT("d"),OKT(TEXT("ui.finance_apps.daily_k_tab"))},{TEXT("w"),OKT(TEXT("ui.finance_apps.weekly_k_tab"))},{TEXT("m"),OKT(TEXT("ui.finance_apps.monthly_k_tab"))}}
  :TArray<TPair<FString,FString>>{{TEXT("1D"),TEXT("1D")},{TEXT("5D"),TEXT("5D")},{TEXT("1M"),TEXT("1M")},{TEXT("6M"),TEXT("6M")},{TEXT("YTD"),TEXT("YTD")},{TEXT("1Y"),TEXT("1Y")},{TEXT("5Y"),TEXT("5Y")},{TEXT("MAX"),TEXT("MAX")}};
 for(const auto& X:P){const FString Key=X.Key;Periods->AddSlot().AutoWidth().Padding(1)[OKFinButton(Weak,X.Value,[Weak,Key](){Weak->State->FinPeriod=Key;Weak->RefreshAppData(true);},true,S.FinPeriod==Key||(S.FinPeriod==TEXT("intraday")&&Key==TEXT("1D"))?FLinearColor(1,.8f,.2f):FLinearColor::White)];}
 auto Search=SNew(SHorizontalBox)
  +SHorizontalBox::Slot().FillWidth(1)[SNew(SEditableTextBox).HintText(FText::FromString(Cn?OKT(TEXT("ui.finance_apps.search_hint_enter_code")):OKT(TEXT("ui.finance_apps.search_hint_enter_symbol")))).Text(FText::FromString(S.FinSearchText))
   .OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->FinSearchText=T.ToString();})
   .OnTextCommitted_Lambda([Weak](const FText& T,ETextCommit::Type How){if(!Weak.IsValid()||How!=ETextCommit::OnEnter)return;FString Error;auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("market"),Weak->State->App);A->SetStringField(TEXT("q"),T.ToString());Weak->State->FinSearch=Weak->Fin(TEXT("search"),A,Error);Weak->PopulateApp();})];
 // Order entry (computer only).
 TSharedRef<SWidget> Trade=SNullWidget::NullWidget;
 if(!S.AppReadOnly){
  auto Side=[Weak](const TCHAR* Key,const TCHAR* Label,FLinearColor C){return OKFinButton(Weak,Label,[Weak,Key=FString(Key)](){Weak->State->FinOrderSide=Key;Weak->PopulateApp();},true,C);};
  auto Kind=[Weak](const TCHAR* Key,const TCHAR* Label){return OKFinButton(Weak,Label,[Weak,Key=FString(Key)](){Weak->State->FinOrderKind=Key;Weak->PopulateApp();});};
  Trade=SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth()[Side(TEXT("buy"),Cn?OKT(TEXT("ui.finance_apps.buy_hotkey_f1")):OKT(TEXT("ui.finance_apps.buy_action")),Cn?FLinearColor(1,.3f,.3f):FLinearColor(.1f,.7f,.3f))]
    +SHorizontalBox::Slot().AutoWidth()[Side(TEXT("sell"),Cn?OKT(TEXT("ui.finance_apps.sell_hotkey_f2")):OKT(TEXT("ui.finance.sell_label")),Cn?FLinearColor(.3f,.9f,.4f):FLinearColor(.9f,.2f,.2f))]
    +SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)[Kind(TEXT("market"),OKT(TEXT("ui.finance_apps.market_order_label")))]
    +SHorizontalBox::Slot().AutoWidth()[Kind(TEXT("limit"),OKT(TEXT("ui.finance_apps.limit_order")))]
    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0,4,0)[OKFinText(OKT(TEXT("ui.finance_apps.price_label")),Ink)]
    +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(90)[SNew(SEditableTextBox).Text(FText::FromString(S.FinOrderPrice)).OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->FinOrderPrice=T.ToString();})]]
    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0,4,0)[OKFinText(Cn?OKT(TEXT("ui.finance_apps.share_count_lot_label")):OKT(TEXT("ui.finance_apps.share_count_label")),Ink)]
    +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(90)[SNew(SEditableTextBox).Text(FText::FromString(S.FinOrderQty)).OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->FinOrderQty=T.ToString();})]]
    +SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.place_order_label")),[Weak](){Weak->SubmitOrder();},true,FLinearColor(1,.85f,.3f))]]
   +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(Cn?OKT(TEXT("ui.finance_apps.a_share_fee_rule")):OKT(TEXT("ui.finance_apps.fx_fee_margin_rule")),Ink)]
    +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SBox).WidthOverride(100)[SNew(SEditableTextBox).Text(FText::FromString(S.FinTransfer)).OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->FinTransfer=T.ToString();})]]
    +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.deposit_wallet_to_account")),[Weak](){Weak->TransferMarket(true);})]
    +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.withdraw_account_to_wallet")),[Weak](){Weak->TransferMarket(false);})]];
 }else Trade=OKFinText(OKT(TEXT("ui.finance_apps.phone_limits_notice")),Ink);
 // Switch market (both read-only on the phone; at a computer both trade).
 auto Switch=SNew(SHorizontalBox)
  +SHorizontalBox::Slot().AutoWidth().Padding(1)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.a_share_app_name")),[Weak](){Weak->OpenApp(TEXT("cn"),Weak->State->AppReadOnly);},!Cn,FLinearColor(1,.4f,.3f))]
  +SHorizontalBox::Slot().AutoWidth().Padding(1)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.us_stock_buoy_title")),[Weak](){Weak->OpenApp(TEXT("us"),Weak->State->AppReadOnly);},Cn,FLinearColor(.3f,.6f,1))];
 return SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight()[Switch]
  +SVerticalBox::Slot().AutoHeight()[S.AppBoxes[5].ToSharedRef()] // index ticker / market cards
  +SVerticalBox::Slot().FillHeight(1)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(260)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Search]+SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[S.AppBoxes[0].ToSharedRef()]]]]
   +SHorizontalBox::Slot().FillWidth(1).Padding(8,0)[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[S.AppBoxes[1].ToSharedRef()]
    +SVerticalBox::Slot().AutoHeight()[Periods]
    +SVerticalBox::Slot().FillHeight(1)[SNew(SOKMarketChart).Data(S.FinChart)]]
   +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(230)[SNew(SScrollBox)+SScrollBox::Slot()[S.AppBoxes[2].ToSharedRef()]]]]
  +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[Trade]
  +SVerticalBox::Slot().AutoHeight().MaxHeight(190).Padding(0,4,0,0)[SNew(SScrollBox)+SScrollBox::Slot()[S.AppBoxes[3].ToSharedRef()]];
}

void AOKStreetSocialScene::SubmitOrder(){
 auto& S=*State;FString Error;
 auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("market"),S.App);O->SetStringField(TEXT("code"),S.FinCode);O->SetStringField(TEXT("side"),S.FinOrderSide);O->SetStringField(TEXT("kind"),S.FinOrderKind);
 O->SetNumberField(TEXT("qty"),FCString::Atod(*S.FinOrderQty));if(S.FinOrderKind==TEXT("limit"))O->SetNumberField(TEXT("price"),FCString::Atod(*S.FinOrderPrice));
 const auto R=Fin(TEXT("order"),O,Error);
 S.FinNotice=R?OKMsg(TEXT("ui.finance_apps.order_status_line"), {{TEXT("order_no"), OKFinNum(R,TEXT("order"))}, {TEXT("status"), *OKFinStr(R,TEXT("status")).Replace(TEXT("filled"),OKT(TEXT("ui.finance_apps.filled_status"))).Replace(TEXT("open"),OKT(TEXT("ui.finance_apps.open_status"))).Replace(TEXT("rejected"),OKT(TEXT("ui.finance_apps.not_filled")))}, {TEXT("note"), OKFinStr(R,TEXT("note")).IsEmpty()?TEXT(""):*(OKT(TEXT("ui.finance_apps.open_paren"))+OKFinStr(R,TEXT("note"))+OKT(TEXT("ui.finance_apps.close_paren")))}}):Error;
 if(R&&S.WatchGirl!=INDEX_NONE){const double Value=FCString::Atod(*S.FinOrderQty)*OKFinNum(R,TEXT("price"));if(Value>=2000)S.MindRecent.Add(OKStreetStableId(S.Manifest.Characters[S.WatchGirl]),OKMsg(TEXT("ui.finance_apps.he_placed_big_order"), {{TEXT("code"), *S.FinCode}}));}
 RefreshAppData(true);
}

void AOKStreetSocialScene::TransferMarket(bool Deposit){
 auto& S=*State;FString Error;auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("market"),S.App);
 if(Deposit){
  const int64 Twd=MoneyIn(S.FinTransfer);if(Twd<=0){S.FinNotice=OKT(TEXT("ui.finance_apps.enter_amount_prompt"));return;}
  if(!SpendCash(Twd,OKT(TEXT("ui.finance.deposit_label"))))return;A->SetNumberField(TEXT("twd"),double(Twd));
  const auto R=Fin(TEXT("deposit"),A,Error);if(!R){GainCash(Twd,OKT(TEXT("ui.finance_apps.deposit_failed_refund")));S.FinNotice=Error;return;}
  S.FinNotice=OKMsg(TEXT("ui.finance_apps.deposit_fx_summary"), {{TEXT("twd_amount"), Twd}, {TEXT("converted_amount"), OKFinNum(R,TEXT("amount"))}, {TEXT("fx_rate"), OKFinNum(R,TEXT("fx"))}, {TEXT("fee_hint"), OKFinNum(R,TEXT("fee"))>0?*OKMsg(TEXT("ui.finance_apps.export_fee"), {{TEXT("fee"), OKFinNum(R,TEXT("fee"))}}):TEXT("")}});
 }else{
  A->SetNumberField(TEXT("amount"),FCString::Atod(*S.FinTransfer));const auto R=Fin(TEXT("withdraw"),A,Error);if(!R){S.FinNotice=Error;return;}
  const int64 Twd=int64(OKFinNum(R,TEXT("twd")));GainCash(Twd,OKFinNum(R,TEXT("fee"))>0?OKMsg(TEXT("ui.finance_apps.withdraw_fee_label"), {{TEXT("fee"), OKFinNum(R,TEXT("fee"))}}):FString(OKT(TEXT("ui.finance.withdraw_label"))));
 }
 RefreshAppData(true);
}

// ------------------------------------------------------------------------------------------------ refill the lists

void AOKStreetSocialScene::PopulateApp(){
 auto& S=*State;if(S.App.IsEmpty()||S.AppBoxes.Num()<6)return;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 for(auto& B:S.AppBoxes)if(B)B->ClearChildren();
 auto Add=[&](int32 Box,TSharedRef<SWidget> W,float Pad=1){if(S.AppBoxes[Box])S.AppBoxes[Box]->AddSlot().AutoHeight().Padding(Pad)[W];};
 auto Balls=[&](const TArray<TSharedPtr<FJsonValue>>& Numbers,int32 Special,const FLinearColor& Main,const FLinearColor& Extra,int32 Show=99){
  auto Row=SNew(SHorizontalBox);int32 K=0;
  for(const auto& V:Numbers){if(K++>=Show)break;Row->AddSlot().AutoWidth().Padding(2)[SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(Main).Padding(FMargin(8,4))[OKFinText(FString::Printf(TEXT("%02d"),int32(V->AsNumber())),FLinearColor::White,13,true)]];}
  if(Special>0&&K<Show+1&&Show>=6)Row->AddSlot().AutoWidth().Padding(2)[SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(Extra).Padding(FMargin(8,4))[OKFinText(FString::Printf(TEXT("%02d"),Special),FLinearColor::Black,13,true)]];
  return Row;};
 if(S.App==TEXT("lottery")){
  const auto Games=OKFinArr(S.FinLotto,TEXT("games"));
  if(S.LotTab==TEXT("star649")||S.LotTab==TEXT("twinstar")){
   const bool Twin=S.LotTab==TEXT("twinstar");TSharedPtr<FJsonObject> G;for(const auto& V:Games)if(OKFinStr(V->AsObject(),TEXT("game"))==S.LotTab)G=V->AsObject();
   Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.game_pool_header"), {{TEXT("game_name"), Twin?OKT(TEXT("ui.finance_apps.double_star_desc")):OKT(TEXT("ui.finance_apps.starlight_lotto_desc"))}, {TEXT("pool"), *OKFinMoney(OKFinNum(G,TEXT("pool")))}, {TEXT("next_date"), *OKFinStr(G,TEXT("next_date"))}, {TEXT("per_bet_price"), Twin?20:10}}),FLinearColor(1,.85f,.3f),13,true));
   auto Grid=SNew(SWrapBox).UseAllottedSize(true);
   for(int32 N=1;N<=(Twin?38:49);++N){const bool On=S.LotPicks.Contains(N);
    Grid->AddSlot().Padding(1)[SNew(SBox).WidthOverride(46)[OKFinButton(Weak,FString::Printf(TEXT("%02d"),N),[Weak,N](){auto& L=*Weak->State;if(L.LotPicks.Contains(N))L.LotPicks.Remove(N);else if(L.LotPicks.Num()<6)L.LotPicks.Add(N);Weak->PopulateApp();},true,On?FLinearColor(1,.3f,.3f):FLinearColor::White)]];}
   Add(0,Grid,4);
   if(Twin){auto Z=SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(OKT(TEXT("ui.finance_apps.zone_two_label")))];
    for(int32 N=1;N<=8;++N)Z->AddSlot().AutoWidth().Padding(1)[OKFinButton(Weak,FString::FromInt(N),[Weak,N](){Weak->State->LotZone2=N;Weak->PopulateApp();},true,S.LotZone2==N?FLinearColor(1,.3f,.3f):FLinearColor::White)];Add(0,Z);}
   auto Buy=SNew(SWrapBox).UseAllottedSize(true);
   Buy->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.finance_apps.buy_this_set"), {{TEXT("picked_count"), S.LotPicks.Num()}}),[Weak](){Weak->BuyLotto(0);},S.LotPicks.Num()==6,FLinearColor(1,.85f,.3f))];
   for(const int32 Q:{1,5,10})Buy->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.finance_apps.quick_pick_count"), {{TEXT("count"), Q}}),[Weak,Q](){Weak->BuyLotto(Q);})];
   Buy->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.clear_button")),[Weak](){Weak->State->LotPicks.Reset();Weak->PopulateApp();})];
   if(FreeTalkPartner()!=INDEX_NONE)Buy->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.let_her_pick")),[Weak](){Weak->PickHerNumbers();},true,FLinearColor(1,.6f,.8f))];
   Buy->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.finance_apps.consecutive_draws_toggle"), {{TEXT("draws"), S.LotDraws}}),[Weak](){auto& L=*Weak->State;L.LotDraws=L.LotDraws>=10?1:L.LotDraws+1;Weak->PopulateApp();})];
   Add(0,Buy,4);
   Add(0,OKFinText(OKT(TEXT("ui.finance_apps.draw_randomness_note")),FLinearColor(.8f,.8f,.8f),10));
   Add(1,OKFinText(OKT(TEXT("ui.finance_apps.recent_draws")),FLinearColor(1,.85f,.3f),13,true));
   for(const auto& V:OKFinArr(G,TEXT("recent"))){const auto D=V->AsObject();Add(1,SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[OKFinText(OKMsg(TEXT("ui.finance_apps.draw_issue_date"), {{TEXT("issue"), *OKFinStr(D,TEXT("issue"))}, {TEXT("date"), *OKFinStr(D,TEXT("date"))}}),FLinearColor(.85f,.85f,.85f),10)]+SVerticalBox::Slot().AutoHeight()[Balls(OKFinArr(D,TEXT("numbers")),int32(OKFinNum(D,TEXT("special"))),FLinearColor(.8f,.1f,.1f),FLinearColor(1,.8f,.2f))]);}
  }else if(S.LotTab==TEXT("scratch")){
   Add(0,OKFinText(OKT(TEXT("ui.finance_apps.lucky_scratch_rules_short")),FLinearColor(1,.85f,.3f),13,true));
   for(const auto& V:OKFinArr(S.FinLotto,TEXT("scratch"))){const auto T=V->AsObject();const FString Id=OKFinStr(T,TEXT("id"));
    Add(0,OKFinButton(Weak,OKMsg(TEXT("ui.finance_apps.scratch_buy_prompt"), {{TEXT("ticket_name"), *OKFinStr(T,TEXT("name"))}, {TEXT("price"), OKFinNum(T,TEXT("price"))}}),[Weak,Id](){Weak->State->ScratchTheme=Id;Weak->StartScratch(Id);}),3);}
  }else if(S.LotTab==TEXT("sports")){
   Add(0,OKFinText(OKT(TEXT("ui.finance_apps.sports_bet_rules_short")),FLinearColor(1,.85f,.3f),11));
   Add(0,SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(OKT(TEXT("ui.finance_apps.past_memory_notebook")),FLinearColor(.6f,.85f,1),11)]
    +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.flip_notebook")),[Weak](){Weak->OpenApp(TEXT("notebook"),true);})],2);
   FString LastDate;
   for(const auto& V:OKFinArr(S.FinBoard,TEXT("games"))){const auto G=V->AsObject();const FString Date=OKFinStr(G,TEXT("date"));
    if(Date!=LastDate){LastDate=Date;Add(0,OKFinText(Date,FLinearColor(.6f,.85f,1),12,true),4);}
    FString Head=OKMsg(TEXT("ui.finance_apps.match_line"), {{TEXT("comp_label"), *OKFinStr(G,TEXT("comp_label"))}, {TEXT("stage"), *OKFinStr(G,TEXT("stage"))}, {TEXT("away_name"), *OKFinStr(G,TEXT("away_name"))}, {TEXT("home_name"), *OKFinStr(G,TEXT("home_name"))}});
    if(G->HasField(TEXT("home_score")))Head+=OKMsg(TEXT("ui.finance_apps.final_score_line"), {{TEXT("away_score"), OKFinNum(G,TEXT("away_score"))}, {TEXT("home_score"), OKFinNum(G,TEXT("home_score"))}, {TEXT("extra_hint"), G->GetBoolField(TEXT("extra"))?OKT(TEXT("ui.finance_apps.extended_paren")):TEXT("")}});
    else if(!G->GetBoolField(TEXT("open")))Head+=OKT(TEXT("ui.finance_apps.closed_awaiting_result"));
    Add(0,OKFinText(Head));
    for(const auto& MV:OKFinArr(G,TEXT("markets"))){const auto M=MV->AsObject();auto Row=SNew(SWrapBox).UseAllottedSize(true);
     const FString Market=OKFinStr(M,TEXT("market"));const double Line=OKFinNum(M,TEXT("line"));
     Row->AddSlot().Padding(1)[OKFinText(OKFinStr(M,TEXT("label"))+(M->HasField(TEXT("line"))?OKMsg(TEXT("ui.finance_apps.change_pct_paren"), {{TEXT("change"), Line}}):FString()),FLinearColor(.8f,.8f,.8f),10)];
     for(const auto& PV:OKFinArr(M,TEXT("picks"))){const auto Pk=PV->AsObject();const FString Pick=OKFinStr(Pk,TEXT("pick"));const double Odds=OKFinNum(Pk,TEXT("odds"));const FString Target=OKFinStr(G,TEXT("id"));
      const FString Name=Pick==TEXT("home")?OKFinStr(G,TEXT("home_name")):Pick==TEXT("away")?OKFinStr(G,TEXT("away_name")):Pick==TEXT("draw")?OKT(TEXT("ui.finance_apps.and_label")):Pick==TEXT("over")?OKT(TEXT("ui.finance_apps.big_label")):Pick==TEXT("under")?OKT(TEXT("ui.finance_apps.small_label")):Pick;
      const FString Label=FString::Printf(TEXT("%s @ %s %s %s"),*OKFinStr(G,TEXT("away_name")),*OKFinStr(G,TEXT("home_name")),*OKFinStr(M,TEXT("label")),*Name);
      Row->AddSlot().Padding(1)[OKFinButton(Weak,FString::Printf(TEXT("%s %.2f"),*Name,Odds),[Weak,Target,Market,Pick,Odds,Label](){auto& L=*Weak->State;L.BetSlip.RemoveAll([&](const TSharedPtr<FJsonObject>& X){return OKFinStr(X,TEXT("target"))==Target;});
       if(L.BetSlip.Num()>=4){L.FinNotice=OKT(TEXT("ui.finance_apps.parlay_max_four"));return;}auto X=MakeShared<FJsonObject>();X->SetStringField(TEXT("target"),Target);X->SetStringField(TEXT("market"),Market);X->SetStringField(TEXT("pick"),Pick);X->SetNumberField(TEXT("odds"),Odds);X->SetStringField(TEXT("label"),Label);L.BetSlip.Add(X);Weak->PopulateApp();})];}
     Add(0,Row);}
   }
   for(const auto& V:OKFinArr(S.FinBoard,TEXT("series"))){const auto Sr=V->AsObject();const auto Names=OKFinArr(Sr,TEXT("names"));const auto Teams=OKFinArr(Sr,TEXT("teams"));const auto Odds=OKFinArr(Sr,TEXT("odds"));if(Names.Num()<2||Teams.Num()<2||Odds.Num()<2)continue;
    auto Row=SNew(SWrapBox).UseAllottedSize(true);Row->AddSlot()[OKFinText(OKMsg(TEXT("ui.finance_apps.series_winner_closes"), {{TEXT("closes"), *OKFinStr(Sr,TEXT("closes"))}}),FLinearColor(.8f,.8f,.8f),10)];
    for(int32 K=0;K<2;++K){const FString Target=OKFinStr(Sr,TEXT("id")),Pick=Teams[K]->AsString(),Name=Names[K]->AsString();const double O=Odds[K]->AsNumber();
     Row->AddSlot().Padding(1)[OKFinButton(Weak,FString::Printf(TEXT("%s %.2f"),*Name,O),[Weak,Target,Pick,Name,O](){auto& L=*Weak->State;L.BetSlip.RemoveAll([&](const TSharedPtr<FJsonObject>& X){return OKFinStr(X,TEXT("target"))==Target;});if(L.BetSlip.Num()>=4)return;auto X=MakeShared<FJsonObject>();X->SetStringField(TEXT("target"),Target);X->SetStringField(TEXT("market"),TEXT("series"));X->SetStringField(TEXT("pick"),Pick);X->SetNumberField(TEXT("odds"),O);X->SetStringField(TEXT("label"),OKT(TEXT("ui.finance_apps.series_prefix"))+Name);L.BetSlip.Add(X);Weak->PopulateApp();})];}
    Add(0,Row);}
   for(const auto& V:OKFinArr(S.FinBoard,TEXT("champions"))){const auto C=V->AsObject();auto Row=SNew(SWrapBox).UseAllottedSize(true);
    Row->AddSlot()[OKFinText(OKMsg(TEXT("ui.finance_apps.champion_bet_header"), {{TEXT("competition"), *OKFinStr(C,TEXT("comp")).ToUpper()}, {TEXT("season"), OKFinNum(C,TEXT("season"))}, {TEXT("closes_date"), *OKFinStr(C,TEXT("closes"))}}),FLinearColor(1,.85f,.3f),11)];
    for(const auto& PV:OKFinArr(C,TEXT("picks"))){const auto Pk=PV->AsObject();const FString Target=OKFinStr(C,TEXT("id")),Pick=OKFinStr(Pk,TEXT("pick")),Name=OKFinStr(Pk,TEXT("name"));const double O=OKFinNum(Pk,TEXT("odds"));
     Row->AddSlot().Padding(1)[OKFinButton(Weak,FString::Printf(TEXT("%s %.2f"),*Name,O),[Weak,Target,Pick,Name,O](){auto& L=*Weak->State;L.BetSlip.RemoveAll([&](const TSharedPtr<FJsonObject>& X){return OKFinStr(X,TEXT("target"))==Target;});if(L.BetSlip.Num()>=4)return;auto X=MakeShared<FJsonObject>();X->SetStringField(TEXT("target"),Target);X->SetStringField(TEXT("market"),TEXT("champion"));X->SetStringField(TEXT("pick"),Pick);X->SetNumberField(TEXT("odds"),O);X->SetStringField(TEXT("label"),OKT(TEXT("ui.finance_apps.champion_label"))+Name);L.BetSlip.Add(X);Weak->PopulateApp();})];}
    Add(0,Row);}
   Add(1,OKFinText(OKT(TEXT("ui.finance_apps.bet_slip_label")),FLinearColor(1,.85f,.3f),13,true));double Total=1;
   for(int32 K=0;K<S.BetSlip.Num();++K){const auto X=S.BetSlip[K];Total*=OKFinNum(X,TEXT("odds"),1);
    Add(1,SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(OKMsg(TEXT("ui.finance_apps.odds_option"), {{TEXT("label"), *OKFinStr(X,TEXT("label"))}, {TEXT("odds"), OKFinNum(X,TEXT("odds"))}})))]
     +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,TEXT("✕"),[Weak,K](){Weak->State->BetSlip.RemoveAt(K);Weak->PopulateApp();})]);}
   if(S.BetSlip.Num())Add(1,OKFinText(OKMsg(TEXT("ui.finance_apps.slip_total_odds"), {{TEXT("slip_label"), S.BetSlip.Num()>1?*OKMsg(TEXT("ui.finance_apps.parlay_legs_label"), {{TEXT("leg_count"), S.BetSlip.Num()}}):OKT(TEXT("ui.finance_apps.single_match_label"))}, {TEXT("total_odds"), Total}, {TEXT("stake"), *S.BetStake}, {TEXT("payout"), FCString::Atod(*S.BetStake)*Total}}),FLinearColor(1,.85f,.3f)));
  }else if(S.LotTab==TEXT("mine")){
   int64 Small=0,Big=0;for(const auto& V:OKFinArr(S.FinTickets,TEXT("claims"))){const auto C=V->AsObject();(C->GetBoolField(TEXT("big"))?Big:Small)+=int64(OKFinNum(C,TEXT("amount")));}
   Add(0,SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(OKMsg(TEXT("ui.finance_apps.claim_limits"), {{TEXT("small_amount"), Small}, {TEXT("big_amount"), Big}}),FLinearColor(1,.85f,.3f),12,true)]
    +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[S.AppReadOnly?OKFinText(OKT(TEXT("ui.finance_apps.small_prize_rule")),FLinearColor(.8f,.8f,.8f),10):OKFinButton(Weak,OKT(TEXT("ui.finance.claim_prize")),[Weak](){FString Error;auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("where"),TEXT("shop"));if(const auto R=Weak->Fin(TEXT("claim"),A,Error)){const int64 Paid=int64(OKFinNum(R,TEXT("paid")));if(Paid>0)Weak->GainCash(Paid,OKT(TEXT("ui.finance.claim_prize")));else Weak->State->FinNotice=OKT(TEXT("ui.finance_apps.no_in_store_prize"));}Weak->RefreshAppData(true);},Small>0)]);
   for(const auto& V:OKFinArr(S.FinTickets,TEXT("tickets"))){const auto T=V->AsObject();const FString Kind=OKFinStr(T,TEXT("kind")),Status=OKFinStr(T,TEXT("status"));
    const FString St=Status==TEXT("won")?OKMsg(TEXT("ui.finance_apps.prize_payout_line"), {{TEXT("payout"), OKFinNum(T,TEXT("payout"))}, {TEXT("prize"), *OKFinStr(T,TEXT("prize"))}}):Status==TEXT("lost")?OKT(TEXT("ui.finance_apps.not_won")):Status==TEXT("void")?OKT(TEXT("ui.finance_apps.refund_button")):Status==TEXT("unscratched")?OKT(TEXT("ui.finance_apps.not_scratched")):OKT(TEXT("ui.finance_apps.awaiting_draw"));
    FString What;
    if(Kind==TEXT("lotto")){TArray<FString> L;for(const auto& LV:OKFinArr(T,TEXT("lines"))){TArray<FString> N;for(const auto& X:OKFinArr(LV->AsObject(),TEXT("numbers")))N.Add(FString::Printf(TEXT("%02d"),int32(X->AsNumber())));L.Add(FString::Join(N,TEXT(" ")));}What=OKMsg(TEXT("ui.finance_apps.draw_result_header"), {{TEXT("game_label"), OKFinStr(T,TEXT("game"))==TEXT("twinstar")?OKT(TEXT("ui.finance_apps.double_star_name")):OKT(TEXT("ui.finance_apps.starlight_lotto_name"))}, {TEXT("draw_no"), OKFinNum(T,TEXT("draw_no"))+1}, {TEXT("draw_date"), *OKFinStr(T,TEXT("draw_date"))}, {TEXT("lines"), *FString::Join(L,OKT(TEXT("ui.finance_apps.separator_bar")))}});}
    else if(Kind==TEXT("sports")){TArray<FString> L;for(const auto& SV:OKFinArr(T,TEXT("selections")))L.Add(OKFinStr(SV->AsObject(),TEXT("label")));What=OKMsg(TEXT("ui.finance_apps.sports_bet_summary"), {{TEXT("game"), *OKFinStr(T,TEXT("game"))}, {TEXT("stake"), OKFinNum(T,TEXT("stake"))}, {TEXT("odds"), OKFinNum(T,TEXT("odds"))}, {TEXT("picks"), *FString::Join(L,OKT(TEXT("ui.finance_apps.plus_sign")))}});}
    else What=OKMsg(TEXT("ui.finance_apps.scratch_card_price"), {{TEXT("prize_name"), *OKFinStr(T,TEXT("prize"))}, {TEXT("stake"), OKFinNum(T,TEXT("stake"))}});
    Add(0,SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(Status==TEXT("won")?FLinearColor(1,.85f,.3f):FLinearColor(.85f,.85f,.85f)).Text(FText::FromString(OKMsg(TEXT("ui.finance_apps.item_to_status"), {{TEXT("ticket_no"), OKFinNum(T,TEXT("id"))}, {TEXT("item_name"), *What}, {TEXT("status_text"), *St}}))));}
  }else{ // board
   for(const auto& V:Games){const auto G=V->AsObject();const auto Recent=OKFinArr(G,TEXT("recent"));if(!Recent.Num())continue;const auto D=Recent[0]->AsObject();
    Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.issue_header"), {{TEXT("label"), *OKFinStr(G,TEXT("label"))}, {TEXT("issue"), *OKFinStr(D,TEXT("issue"))}, {TEXT("date"), *OKFinStr(D,TEXT("date"))}}),FLinearColor(1,.85f,.3f),13,true),4);
    // The draw animation: balls drop one by one in the first seconds after the 20:30 draw.
    const bool Animate=S.DrawShow&&OKFinStr(S.DrawShow,TEXT("game"))==OKFinStr(G,TEXT("game"))&&FPlatformTime::Seconds()-S.DrawShowAt<12;
    Add(0,Balls(OKFinArr(D,TEXT("numbers")),int32(OKFinNum(D,TEXT("special"))),FLinearColor(.8f,.1f,.1f),FLinearColor(1,.8f,.2f),Animate?int32((FPlatformTime::Seconds()-S.DrawShowAt)/1.5):99));
    Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.next_draw_pool"), {{TEXT("next_date"), *OKFinStr(G,TEXT("next_date"))}, {TEXT("pool"), *OKFinMoney(OKFinNum(G,TEXT("pool")))}})));}
   Add(1,OKFinText(OKT(TEXT("ui.finance_apps.red_board")),FLinearColor(1,.3f,.3f),14,true));
   for(const auto& V:OKFinArr(S.FinLotto,TEXT("board")))Add(1,OKFinText(OKMsg(TEXT("ui.bank.date_text_line"), {{TEXT("date"), *OKFinStr(V->AsObject(),TEXT("date"))}, {TEXT("text"), *OKFinStr(V->AsObject(),TEXT("text"))}}),FLinearColor(1,.8f,.2f)));
   Add(1,OKFinText(OKT(TEXT("ui.finance_apps.recent_results")),FLinearColor(.6f,.85f,1),13,true),4);
   for(const auto& V:OKFinArr(S.FinBoard,TEXT("games"))){const auto G=V->AsObject();if(!G->HasField(TEXT("home_score")))continue;
    Add(1,OKFinText(FString::Printf(TEXT("%s %s %s %.0f : %.0f %s"),*OKFinStr(G,TEXT("date")),*OKFinStr(G,TEXT("comp_label")),*OKFinStr(G,TEXT("away_name")),OKFinNum(G,TEXT("away_score")),OKFinNum(G,TEXT("home_score")),*OKFinStr(G,TEXT("home_name"))),FLinearColor(.85f,.85f,.85f),10));}
  }
  return;
 }
 if(S.App==TEXT("web")){
  const FLinearColor Ink(.1f,.1f,.15f);const auto Games=OKFinArr(S.FinLotto,TEXT("games"));
  if(S.WebTab==TEXT("home")){
   Add(0,SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(FLinearColor(1,.85f,.3f)).Padding(10)[OKFinText(OKT(TEXT("ui.finance_apps.mid_autumn_banner")),FLinearColor(.5f,.05f,.05f),13,true)],4);
   for(const auto& V:Games){const auto G=V->AsObject();const bool Twin=OKFinStr(G,TEXT("game"))==TEXT("twinstar");const auto Recent=OKFinArr(G,TEXT("recent"));
    auto Block=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[OKFinText(OKMsg(TEXT("ui.finance_apps.jackpot_pool_header"), {{TEXT("label"), *OKFinStr(G,TEXT("label"))}, {TEXT("pool"), *OKFinMoney(OKFinNum(G,TEXT("pool")))}, {TEXT("next_date"), *OKFinStr(G,TEXT("next_date"))}}),FLinearColor(.7f,.05f,.05f),15,true)];
    if(Recent.Num()){const auto D=Recent[0]->AsObject();const bool Animate=S.DrawShow&&OKFinStr(S.DrawShow,TEXT("game"))==OKFinStr(G,TEXT("game"))&&FPlatformTime::Seconds()-S.DrawShowAt<12;
     Block->AddSlot().AutoHeight()[OKFinText(OKMsg(TEXT("ui.finance_apps.draw_result_title"), {{TEXT("issue"), *OKFinStr(D,TEXT("issue"))}, {TEXT("date"), *OKFinStr(D,TEXT("date"))}}),Ink)];
     Block->AddSlot().AutoHeight()[Balls(OKFinArr(D,TEXT("numbers")),int32(OKFinNum(D,TEXT("special"))),Twin?FLinearColor(.1f,.3f,.85f):FLinearColor(.85f,.1f,.1f),Twin?FLinearColor(.85f,.1f,.1f):FLinearColor(1,.75f,.1f),Animate?int32((FPlatformTime::Seconds()-S.DrawShowAt)/1.5):99)];
     TArray<FString> Issues;for(int32 K=1;K<FMath::Min(11,Recent.Num());++K)Issues.Add(OKFinStr(Recent[K]->AsObject(),TEXT("issue")));Block->AddSlot().AutoHeight()[OKFinText(OKT(TEXT("ui.finance_apps.last_ten_draws_label"))+FString::Join(Issues,OKT(TEXT("ui.accounts_app.ideographic_space"))),FLinearColor(.3f,.3f,.5f),10)];}
    Add(0,SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(FLinearColor(1,1,1)).Padding(10)[Block],4);}
  }else if(S.WebTab==TEXT("draws")){
   for(const auto& V:Games){const auto G=V->AsObject();Add(0,OKFinText(OKFinStr(G,TEXT("label")),FLinearColor(.7f,.05f,.05f),14,true),4);int32 K=0;
    for(const auto& DV:OKFinArr(G,TEXT("recent"))){if(K++>=30)break;const auto D=DV->AsObject();TArray<FString> N;for(const auto& X:OKFinArr(D,TEXT("numbers")))N.Add(FString::Printf(TEXT("%02d"),int32(X->AsNumber())));
     Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.draw_result_line"), {{TEXT("issue"), *OKFinStr(D,TEXT("issue"))}, {TEXT("date"), *OKFinStr(D,TEXT("date"))}, {TEXT("numbers"), *FString::Join(N,TEXT(" "))}, {TEXT("special"), OKFinNum(D,TEXT("special"))}, {TEXT("jackpot_status"), D->GetBoolField(TEXT("jackpot_hit"))?OKT(TEXT("ui.finance_apps.drawn_label")):OKT(TEXT("ui.finance_apps.jackpot_rollover"))}}),Ink,11));}}
  }else if(S.WebTab==TEXT("rules")){
   for(const TCHAR* L:{OKT(TEXT("ui.finance_apps.starlight_lotto_rules")),
    OKT(TEXT("ui.finance_apps.double_star_rules")),
    OKT(TEXT("ui.finance_apps.lucky_scratch_rules_detail")),
    OKT(TEXT("ui.finance_apps.sports_bet_rules_full")),
    OKT(TEXT("ui.finance_apps.prize_claim_rule"))})Add(0,SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(Ink).Text(FText::FromString(L)),6);
  }else if(S.WebTab==TEXT("sports")){
   if(OKFinArr(S.FinBoard,TEXT("games")).IsEmpty()){const TSharedPtr<FJsonObject>* Next=nullptr;
    Add(0,OKFinText(S.FinBoard.IsValid()&&S.FinBoard->TryGetObjectField(TEXT("next"),Next)?OKMsg(TEXT("ui.finance_apps.no_games_next"), {{TEXT("date"), *OKFinStr(*Next,TEXT("date"))}, {TEXT("comp_label"), *OKFinStr(*Next,TEXT("comp_label"))}}):FString(OKT(TEXT("ui.finance_apps.no_games"))),Ink,13));}
   for(const auto& V:OKFinArr(S.FinBoard,TEXT("games"))){const auto G=V->AsObject();
    Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.match_schedule_row"), {{TEXT("date"), *OKFinStr(G,TEXT("date"))}, {TEXT("comp_label"), *OKFinStr(G,TEXT("comp_label"))}, {TEXT("away_name"), *OKFinStr(G,TEXT("away_name"))}, {TEXT("home_name"), *OKFinStr(G,TEXT("home_name"))}, {TEXT("score_or_status"), G->HasField(TEXT("home_score"))?*FString::Printf(TEXT("%.0f : %.0f"),OKFinNum(G,TEXT("away_score")),OKFinNum(G,TEXT("home_score"))):G->GetBoolField(TEXT("open"))?OKT(TEXT("ui.finance_apps.betting_open_in_store")):OKT(TEXT("ui.finance_apps.match_in_progress"))}}),Ink,11));}
  }else if(S.WebTab==TEXT("trend")){
   // Frequency and gap of each number over the last 100 draws: entertainment only (plan §15.7.2).
   for(const auto& V:Games){const auto G=V->AsObject();const bool Twin=OKFinStr(G,TEXT("game"))==TEXT("twinstar");const int32 Max=Twin?38:49;TArray<int32> Count,Gap;Count.Init(0,Max+1);Gap.Init(-1,Max+1);int32 K=0;
    for(const auto& DV:OKFinArr(G,TEXT("recent"))){for(const auto& X:OKFinArr(DV->AsObject(),TEXT("numbers"))){const int32 N=int32(X->AsNumber());if(N>=1&&N<=Max){++Count[N];if(Gap[N]<0)Gap[N]=K;}}++K;}
    Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.trend_header"), {{TEXT("label"), *OKFinStr(G,TEXT("label"))}, {TEXT("count"), K}}),FLinearColor(.7f,.05f,.05f),14,true),4);
    auto Grid=SNew(SWrapBox).UseAllottedSize(true);
    for(int32 N=1;N<=Max;++N)Grid->AddSlot().Padding(2)[SNew(SBox).WidthOverride(120)[OKFinText(OKMsg(TEXT("ui.finance_apps.number_frequency_gap"), {{TEXT("number"), N}, {TEXT("count"), Count[N]}, {TEXT("gap"), Gap[N]<0?K:Gap[N]}}),Ink,10)]];
    Add(0,Grid);}
   Add(0,OKFinText(OKT(TEXT("ui.finance_apps.draws_random_reminder")),FLinearColor(.5f,.1f,.1f),11));
  }else if(S.WebTab==TEXT("shop")){
   Add(0,OKFinText(OKT(TEXT("ui.finance_apps.store_description")),Ink,12));
   for(const auto& V:OKFinArr(S.FinLotto,TEXT("board")))Add(0,OKFinText(OKMsg(TEXT("ui.bank.date_text_line"), {{TEXT("date"), *OKFinStr(V->AsObject(),TEXT("date"))}, {TEXT("text"), *OKFinStr(V->AsObject(),TEXT("text"))}}),FLinearColor(.75f,.05f,.05f)));
  }else Add(0,SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(Ink).Text(FText::FromString(OKT(TEXT("ui.finance_apps.responsible_lottery_notice")))),8);
  return;
 }
 if(S.App==TEXT("notebook")){
  const FLinearColor Ink(.95f,.92f,.85f);
  Add(0,OKFinText(OKT(TEXT("ui.finance_apps.memory_lottery_note")),FLinearColor(1,.85f,.3f),13,true));
  Add(0,OKFinText(OKT(TEXT("ui.finance_apps.recent_match_results_note")),FLinearColor(.6f,.85f,1),13,true),6);
  for(const auto& V:OKFinArr(S.FinNotebook,TEXT("sports"))){const auto G=V->AsObject();
   Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.match_result_row"), {{TEXT("date"), *OKFinStr(G,TEXT("date"))}, {TEXT("comp_label"), *OKFinStr(G,TEXT("comp_label"))}, {TEXT("away_name"), *OKFinStr(G,TEXT("away_name"))}, {TEXT("away_score"), OKFinNum(G,TEXT("away_score"))}, {TEXT("home_score"), OKFinNum(G,TEXT("home_score"))}, {TEXT("home_name"), *OKFinStr(G,TEXT("home_name"))}, {TEXT("open_hint"), G->GetBoolField(TEXT("open"))?OKT(TEXT("ui.finance_apps.still_open_hint")):TEXT("")}}),Ink,11));}
  Add(0,OKFinText(OKT(TEXT("ui.finance_apps.index_big_direction")),FLinearColor(.6f,.85f,1),13,true),6);
  for(const auto& V:OKFinArr(S.FinNotebook,TEXT("index_years"))){const auto Y=V->AsObject();TArray<FString> Big;for(const auto& B:OKFinArr(Y,TEXT("big_days")))Big.Add(FString::Printf(TEXT("%s %+.1f%%"),*OKFinStr(B->AsObject(),TEXT("date")),OKFinNum(B->AsObject(),TEXT("pct"))));
   Add(0,SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(Ink).Text(FText::FromString(OKMsg(TEXT("ui.finance_apps.yearly_index_summary"), {{TEXT("year"), OKFinNum(Y,TEXT("year"))}, {TEXT("index_name"), *OKFinStr(Y,TEXT("index"))}, {TEXT("pct"), OKFinNum(Y,TEXT("pct"))}, {TEXT("big_moves"), Big.Num()?*(OKT(TEXT("ui.finance_apps.big_move_days_label"))+FString::Join(Big,OKT(TEXT("ui.bank.list_separator")))):TEXT("")}}))));}
  Add(0,OKFinText(OKT(TEXT("ui.finance_apps.famous_stock_event")),FLinearColor(.6f,.85f,1),13,true),6);
  for(const auto& V:OKFinArr(S.FinNotebook,TEXT("stocks"))){const auto X=V->AsObject();Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.yearly_stock_return"), {{TEXT("year"), OKFinNum(X,TEXT("year"))}, {TEXT("market"), OKFinStr(X,TEXT("market"))==TEXT("cn")?OKT(TEXT("ui.bank.a_shares_label")):OKT(TEXT("ui.bank.us_stocks_label"))}, {TEXT("name"), *OKFinStr(X,TEXT("name"))}, {TEXT("code"), *OKFinStr(X,TEXT("code"))}, {TEXT("pct"), OKFinNum(X,TEXT("pct"))}}),Ink,11));}
  return;
 }
 if(S.App==TEXT("atm")){
  Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.wallet_bank_balance"), {{TEXT("cash"), S.Store.PlayerCash}, {TEXT("bank"), S.Store.PlayerBank}}),FLinearColor(1,.85f,.3f),15,true));
  const auto Acc=OKFinObj(S.FinSummary,TEXT("accounts"));
  Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.account_value_cny"), {{TEXT("a_share_value"), OKFinNum(OKFinObj(Acc,TEXT("cn")),TEXT("twd"))}, {TEXT("us_value"), OKFinNum(OKFinObj(Acc,TEXT("us")),TEXT("twd"))}})));
  Add(0,OKFinText(OKMsg(TEXT("ui.finance_apps.summary_unclaimed_tickets"), {{TEXT("unclaimed"), OKFinNum(S.FinSummary,TEXT("unclaimed"))}, {TEXT("open_tickets"), OKFinNum(S.FinSummary,TEXT("open_tickets"))}})));
  return;
 }
 // Terminals.
 const bool Cn=S.App==TEXT("cn");const FLinearColor Ink=Cn?FLinearColor(.85f,.85f,.9f):FLinearColor(.1f,.1f,.15f);const FLinearColor Dim=Cn?FLinearColor(.55f,.55f,.6f):FLinearColor(.45f,.45f,.5f);
 const auto Q=S.FinQuote;const auto Session=OKFinObj(Q,TEXT("session"));
 {// ticker / market cards
  auto Row=SNew(SWrapBox).UseAllottedSize(true);
  Row->AddSlot().Padding(4,2)[OKFinText(Cn?OKMsg(TEXT("ui.bank.date_text_line"), {{TEXT("date"), *OKFinStr(Session,TEXT("text"))}, {TEXT("text"), *OKFinStr(Session,TEXT("data_through"))}}):OKMsg(TEXT("ui.company_app.ledger_entry_row"), {{TEXT("day_label"), (Session&&[&]{bool V=false;Session->TryGetBoolField(TEXT("dst"),V);return V;}())?OKT(TEXT("ui.finance_apps.summer_time_hours")):OKT(TEXT("ui.finance_apps.winter_time_hours"))}, {TEXT("note"), *OKFinStr(Session,TEXT("text"))}, {TEXT("amount"), Session&&Session->HasField(TEXT("et"))?*(OKT(TEXT("ui.finance_apps.us_east_prefix"))+OKFinStr(Session,TEXT("et"))):TEXT("")}}),Ink,11,true)];
  Row->AddSlot().Padding(4,2)[OKFinText(OKT(TEXT("ui.finance_apps.heat_label")),Dim)];
  int32 K=0;for(const auto& V:OKFinArr(S.FinMovers,TEXT("up"))){if(K++>=4)break;const auto X=V->AsObject();Row->AddSlot().Padding(4,2)[OKFinText(FString::Printf(TEXT("%s %+.2f%%"),*OKFinStr(X,TEXT("name")).Left(8),OKFinNum(X,TEXT("pct"))),OKFinUpDown(OKFinNum(X,TEXT("pct")),Cn),10)];}
  K=0;for(const auto& V:OKFinArr(S.FinMovers,TEXT("down"))){if(K++>=3)break;const auto X=V->AsObject();Row->AddSlot().Padding(4,2)[OKFinText(FString::Printf(TEXT("%s %+.2f%%"),*OKFinStr(X,TEXT("name")).Left(8),OKFinNum(X,TEXT("pct"))),OKFinUpDown(OKFinNum(X,TEXT("pct")),Cn),10)];}
  Add(5,Row);
 }
 // Left: search results, then the watchlist.
 for(const auto& V:OKFinArr(S.FinSearch,TEXT("results"))){const auto X=V->AsObject();const FString Code=OKFinStr(X,TEXT("code"));
  Add(0,SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[OKFinButton(Weak,FString::Printf(TEXT("%s %s"),*OKFinStr(X,TEXT("symbol")),*OKFinStr(X,TEXT("name")).Left(12)),[Weak,Code](){Weak->State->FinCode=Code;Weak->RefreshAppData(true);})]
   +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.plus_sign")),[Weak,Code](){auto& L=*Weak->State;auto& W=L.FinWatchCodes[L.App==TEXT("cn")?0:1];W.AddUnique(Code);Weak->RefreshAppData(true);})]);}
 Add(0,OKFinText(Cn?OKT(TEXT("ui.finance_apps.watchlist_title")):OKT(TEXT("ui.finance_apps.watchlist_tab")),FLinearColor(1,.8f,.2f),12,true),3);
 for(const auto& V:OKFinArr(S.FinWatch,TEXT("quotes"))){const auto X=V->AsObject();const FString Code=OKFinStr(X,TEXT("code"));const double Pct=OKFinNum(X,TEXT("pct"));
  auto Card=SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[OKFinText(FString::Printf(TEXT("%s %s"),*OKFinStr(X,TEXT("symbol")),*OKFinStr(X,TEXT("name")).Left(10)),Code==S.FinCode?FLinearColor(1,.85f,.3f):Ink,10)]
   +SVerticalBox::Slot().AutoHeight()[OKFinText(X->HasField(TEXT("price"))?OKMsg(TEXT("ui.finance_apps.price_and_pct"), {{TEXT("price"), OKFinNum(X,TEXT("price"))}, {TEXT("pct_change"), Pct}}):OKFinStr(X,TEXT("status")),OKFinUpDown(Pct,Cn),10)]]
   +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.watch_button")),[Weak,Code](){Weak->State->FinCode=Code;Weak->RefreshAppData(true);})]
   +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,TEXT("−"),[Weak,Code](){auto& L=*Weak->State;L.FinWatchCodes[L.App==TEXT("cn")?0:1].Remove(Code);Weak->RefreshAppData(true);})];
  Add(0,Card);}
 // Centre header.
 if(Q){const double Pct=OKFinNum(Q,TEXT("pct")),Chg=OKFinNum(Q,TEXT("change"));
  FString Limits;if(Cn&&OKFinNum(Q,TEXT("limit_up"))>0)Limits=OKMsg(TEXT("ui.finance_apps.price_limit_range"), {{TEXT("limit_up"), OKFinNum(Q,TEXT("limit_up"))}, {TEXT("limit_down"), OKFinNum(Q,TEXT("limit_down"))}});
  FString Blocked;if(Q->HasField(TEXT("at_limit_up"))&&Q->GetBoolField(TEXT("at_limit_up")))Blocked=OKT(TEXT("ui.finance_apps.limit_up_no_buy"));if(Q->HasField(TEXT("at_limit_down"))&&Q->GetBoolField(TEXT("at_limit_down")))Blocked=OKT(TEXT("ui.finance_apps.limit_down_no_sell"));
  Add(1,SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(FString::Printf(TEXT("%s %s"),*OKFinStr(Q,TEXT("symbol")),*OKFinStr(Q,TEXT("name"))),Ink,16,true)]
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12,0)[OKFinText(Q->HasField(TEXT("price"))?OKMsg(TEXT("ui.finance_apps.quote_price_change"), {{TEXT("currency_prefix"), Cn?TEXT(""):TEXT("$")}, {TEXT("price"), OKFinNum(Q,TEXT("price"))}, {TEXT("change"), Chg}, {TEXT("pct"), Pct}}):OKFinStr(Q,TEXT("status")),OKFinUpDown(Chg,Cn),18,true)]
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(OKMsg(TEXT("ui.finance_apps.status_date_flags"), {{TEXT("status"), *OKFinStr(Q,TEXT("status"))}, {TEXT("date"), *OKFinStr(Q,TEXT("date"))}, {TEXT("limits"), *Limits}, {TEXT("blocked"), *Blocked}}),Blocked.IsEmpty()?Dim:FLinearColor(1,.4f,.9f),11)]);}
 // Right: depth and stats.
 if(Q){
  Add(2,OKFinText(Cn?OKT(TEXT("ui.finance_apps.order_book_five_levels")):OKT(TEXT("ui.finance_apps.quote_simulated")),FLinearColor(1,.8f,.2f),12,true));
  const auto Depth=OKFinObj(Q,TEXT("depth"));const auto Asks=OKFinArr(Depth,TEXT("asks"));
  for(int32 K=Asks.Num()-1;K>=0;--K){const auto A=Asks[K]->AsObject();Add(2,OKFinText(OKMsg(TEXT("ui.finance_apps.sell_order_row"), {{TEXT("index"), K+1}, {TEXT("price"), OKFinNum(A,TEXT("price"))}, {TEXT("qty"), OKFinNum(A,TEXT("qty"))/(Cn?100:1)}}),Cn?FLinearColor(.3f,.9f,.4f):FLinearColor(.85f,.2f,.2f),10));}
  int32 K=0;for(const auto& V:OKFinArr(Depth,TEXT("bids"))){const auto B=V->AsObject();Add(2,OKFinText(OKMsg(TEXT("ui.finance_apps.buy_order_row"), {{TEXT("index"), ++K}, {TEXT("price"), OKFinNum(B,TEXT("price"))}, {TEXT("qty"), OKFinNum(B,TEXT("qty"))/(Cn?100:1)}}),Cn?FLinearColor(1,.3f,.3f):FLinearColor(.1f,.65f,.3f),10));}
  for(const auto& L:{TPair<const TCHAR*,const TCHAR*>(OKT(TEXT("ui.finance_apps.open_price_label")),TEXT("open")),TPair<const TCHAR*,const TCHAR*>(OKT(TEXT("ui.finance_apps.high_price")),TEXT("high")),TPair<const TCHAR*,const TCHAR*>(OKT(TEXT("ui.finance_apps.low_price")),TEXT("low")),TPair<const TCHAR*,const TCHAR*>(OKT(TEXT("ui.finance_apps.prev_close")),TEXT("prev_close")),TPair<const TCHAR*,const TCHAR*>(OKT(TEXT("ui.finance_apps.week52_high")),TEXT("high52")),TPair<const TCHAR*,const TCHAR*>(OKT(TEXT("ui.finance_apps.week52_low")),TEXT("low52"))})
   Add(2,OKFinText(OKMsg(TEXT("ui.finance_apps.key_value_pair"), {{TEXT("key"), L.Key}, {TEXT("value"), OKFinNum(Q,L.Value)}}),Ink,10));
  Add(2,OKFinText(OKMsg(TEXT("ui.finance_apps.volume_line"), {{TEXT("volume"), OKFinNum(Q,TEXT("volume"))/(Cn?100:1)}, {TEXT("unit"), Cn?OKT(TEXT("ui.finance_apps.hand_unit")):OKT(TEXT("ui.finance_apps.share_unit"))}}),Ink,10));
  // Fundamentals published by the game day (market.sqlite): valuation at the shown price and the latest report.
  if(const auto F=OKFinObj(Q,TEXT("fundamentals"))){
   const FString P=OKFinStr(F,TEXT("period"));const FString End=OKFinStr(F,TEXT("period_end"));
   const TCHAR* Label=P==TEXT("Q1")?OKT(TEXT("ui.finance_apps.quarter_one")):P==TEXT("H1")||P==TEXT("Q2")?OKT(TEXT("ui.finance_apps.first_half")):P==TEXT("Q3")?OKT(TEXT("ui.finance_apps.first_three_quarters")):OKT(TEXT("ui.finance_apps.annual_report"));
   Add(2,OKFinText(OKMsg(TEXT("ui.finance_apps.fundamentals_header"), {{TEXT("period_end_year"), *End.Left(4)}, {TEXT("label"), Label}}),FLinearColor(1,.8f,.2f),12,true));
   Add(2,OKFinText(OKMsg(TEXT("ui.finance_apps.announcement_title"), {{TEXT("available_label"), *OKFinStr(F,TEXT("available"))}}),FLinearColor(.7f,.7f,.7f),10));
   auto Big=[Cn](double V){const double A=FMath::Abs(V);return Cn?(A>=1e8?OKMsg(TEXT("ui.finance.amount_in_yi"), {{TEXT("amount_yi"), V/1e8}}):OKMsg(TEXT("ui.finance.amount_in_wan"), {{TEXT("amount_wan"), V/1e4}})):(A>=1e9?FString::Printf(TEXT("$%.2fB"),V/1e9):FString::Printf(TEXT("$%.1fM"),V/1e6));};
   auto Line=[&](const TCHAR* Name,const TCHAR* Key,int32 Kind){if(!F->HasTypedField<EJson::Number>(Key))return;const double V=OKFinNum(F,Key);
    Add(2,OKFinText(OKMsg(TEXT("ui.bank.date_text_line"), {{TEXT("date"), Name}, {TEXT("text"), *(Kind==0?FString::Printf(TEXT("%.2f"),V):Kind==1?Big(V):Kind==2?FString::Printf(TEXT("%.2f%%"),V):OKMsg(TEXT("ui.finance_apps.multiple_value"), {{TEXT("multiple"), V}}))}}),Ink,10));};
   Line(OKT(TEXT("ui.finance_apps.pe_ratio_label")),TEXT("pe_ttm"),3);Line(OKT(TEXT("ui.finance_apps.pb_ratio")),TEXT("pb"),3);Line(OKT(TEXT("ui.finance_apps.market_value_label")),TEXT("market_cap"),1);
   Line(OKT(TEXT("ui.finance_apps.revenue")),TEXT("revenue"),1);Line(OKT(TEXT("ui.finance_apps.net_profit")),TEXT("net_income"),1);Line(OKT(TEXT("ui.finance_apps.eps_label")),TEXT("eps"),0);Line(TEXT("ROE"),TEXT("roe"),2);Line(OKT(TEXT("ui.finance_apps.dividend_yield")),TEXT("dividend_yield"),2);
  }
  // Price alerts on this stock.
  Add(2,OKFinText(OKT(TEXT("ui.finance_apps.price_alert_title")),FLinearColor(1,.8f,.2f),12,true));
  Add(2,SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(Cn?OKT(TEXT("ui.finance_apps.price_or_percent_label")):TEXT("Price/%"),Ink,10)]
   +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SBox).WidthOverride(70)[SNew(SEditableTextBox).Text(FText::FromString(S.AlertValue)).OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->AlertValue=T.ToString();})]]);
  {auto Row=SNew(SWrapBox).UseAllottedSize(true);
   Row->AddSlot().Padding(1)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.rise_to")),[Weak](){Weak->AddAlert(TEXT("above"));})];
   Row->AddSlot().Padding(1)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.fall_to")),[Weak](){Weak->AddAlert(TEXT("below"));})];
   Row->AddSlot().Padding(1)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.move_exceeds_label")),[Weak](){Weak->AddAlert(TEXT("pct"));})];
   if(Cn){Row->AddSlot().Padding(1)[OKFinButton(Weak,OKT(TEXT("ui.bank.limit_up_label")),[Weak](){Weak->AddAlert(TEXT("limit_up"));})];Row->AddSlot().Padding(1)[OKFinButton(Weak,OKT(TEXT("ui.bank.limit_down_label")),[Weak](){Weak->AddAlert(TEXT("limit_down"));})];}
   Add(2,Row);}
  for(const auto& V:OKFinArr(S.FinAlerts,TEXT("alerts"))){const auto X=V->AsObject();if(OKFinStr(X,TEXT("code"))!=S.FinCode)continue;const int64 AId=int64(OKFinNum(X,TEXT("id")));const FString AK=OKFinStr(X,TEXT("kind"));
   Add(2,SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[OKFinText(AK==TEXT("above")?OKMsg(TEXT("ui.bank.rose_to_price"), {{TEXT("price"), OKFinNum(X,TEXT("value"))}}):AK==TEXT("below")?OKMsg(TEXT("ui.bank.fell_to_price"), {{TEXT("price"), OKFinNum(X,TEXT("value"))}}):AK==TEXT("pct")?OKMsg(TEXT("ui.finance_apps.move_exceeds_percent"), {{TEXT("pct"), OKFinNum(X,TEXT("value"))}}):AK==TEXT("limit_up")?FString(OKT(TEXT("ui.bank.limit_up_label"))):FString(OKT(TEXT("ui.bank.limit_down_label"))),Ink,10)]
    +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,TEXT("✕"),[Weak,AId](){FString E;auto A=MakeShared<FJsonObject>();A->SetNumberField(TEXT("id"),double(AId));Weak->Fin(TEXT("alert_del"),A,E);auto M=MakeShared<FJsonObject>();M->SetStringField(TEXT("market"),Weak->State->App);Weak->State->FinAlerts=Weak->Fin(TEXT("alerts"),M,E);Weak->PopulateApp();Weak->RefreshUI();})]);}
 }
 // Bottom: account, positions, orders.
 const auto A=S.FinAccount;
 Add(3,OKFinText(OKMsg(TEXT("ui.finance_apps.account_balance_summary"), {{TEXT("currency"), *OKFinStr(A,TEXT("currency"))}, {TEXT("cash"), OKFinNum(A,TEXT("cash"))}, {TEXT("frozen"), OKFinNum(A,TEXT("frozen"))}, {TEXT("holding_value"), OKFinNum(A,TEXT("value"))}, {TEXT("total_assets"), OKFinNum(A,TEXT("total"))}, {TEXT("total_twd"), OKFinNum(A,TEXT("total_twd"))}, {TEXT("fx_rate"), OKFinNum(A,TEXT("fx"))}, {TEXT("order_status"), *OKMsg(TEXT("ui.finance_apps.order_side_kind"), {{TEXT("side"), S.FinOrderSide==TEXT("buy")?OKT(TEXT("ui.finance_apps.buy_action")):OKT(TEXT("ui.finance.sell_label"))}, {TEXT("kind"), S.FinOrderKind==TEXT("market")?OKT(TEXT("ui.finance_apps.market_order_label")):OKT(TEXT("ui.finance_apps.limit_order"))}})}}),FLinearColor(1,.8f,.2f),11,true));
 for(const auto& V:OKFinArr(A,TEXT("positions"))){const auto P=V->AsObject();const FString Code=OKFinStr(P,TEXT("code"));
  Add(3,SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[OKFinText(OKMsg(TEXT("ui.finance_apps.holding_summary_line"), {{TEXT("symbol"), *(OKFinStr(P,TEXT("symbol")).IsEmpty()?Code:OKFinStr(P,TEXT("symbol")))}, {TEXT("name"), *OKFinStr(P,TEXT("name")).Left(10)}, {TEXT("qty"), OKFinNum(P,TEXT("qty"))}, {TEXT("sellable_note"), Cn?*OKMsg(TEXT("ui.finance_apps.sellable_bought_today"), {{TEXT("sellable"), OKFinNum(P,TEXT("sellable"))}, {TEXT("bought_today"), OKFinNum(P,TEXT("today"))}}):TEXT("")}, {TEXT("avg_cost"), OKFinNum(P,TEXT("avg"))}, {TEXT("price"), OKFinNum(P,TEXT("price"))}, {TEXT("value"), OKFinNum(P,TEXT("value"))}, {TEXT("pnl"), OKFinNum(P,TEXT("pnl"))}, {TEXT("pnl_pct"), OKFinNum(P,TEXT("pnl_pct"))}}),OKFinUpDown(OKFinNum(P,TEXT("pnl")),Cn),10)]
   +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.watch_button")),[Weak,Code](){Weak->State->FinCode=Code;Weak->RefreshAppData(true);})]);}
 int32 Shown=0;
 for(const auto& V:OKFinArr(A,TEXT("orders"))){if(Shown++>=12)break;const auto O=V->AsObject();const int64 Id=int64(OKFinNum(O,TEXT("id")));const bool Open=OKFinStr(O,TEXT("status"))==TEXT("open");
  const FString St=OKFinStr(O,TEXT("status")).Replace(TEXT("filled"),OKT(TEXT("ui.finance_apps.filled_short"))).Replace(TEXT("open"),OKT(TEXT("ui.finance_apps.reported_status"))).Replace(TEXT("cancelled"),OKT(TEXT("ui.finance_apps.cancelled_short"))).Replace(TEXT("rejected"),OKT(TEXT("ui.finance_apps.void_ticket")));
  auto Row=SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[OKFinText(OKMsg(TEXT("ui.finance_apps.order_detail_line"), {{TEXT("order_id"), Id}, {TEXT("date"), *OKFinStr(O,TEXT("date"))}, {TEXT("side"), OKFinStr(O,TEXT("side"))==TEXT("buy")?OKT(TEXT("ui.bank.buy_label")):OKT(TEXT("ui.finance_apps.sell_action"))}, {TEXT("symbol"), *(OKFinStr(O,TEXT("symbol")).IsEmpty()?OKFinStr(O,TEXT("code")):OKFinStr(O,TEXT("symbol")))}, {TEXT("order_kind"), OKFinStr(O,TEXT("kind"))==TEXT("market")?OKT(TEXT("ui.finance_apps.market_order_label")):OKT(TEXT("ui.finance_apps.limit_order"))}, {TEXT("qty"), OKFinNum(O,TEXT("qty"))}, {TEXT("price"), OKFinStr(O,TEXT("kind"))==TEXT("market")?TEXT("-"):*FString::Printf(TEXT("%.2f"),OKFinNum(O,TEXT("price")))}, {TEXT("filled_qty"), OKFinNum(O,TEXT("filled"))}, {TEXT("avg_price"), OKFinNum(O,TEXT("avg"))}, {TEXT("status"), *St}, {TEXT("note"), *OKFinStr(O,TEXT("note"))}}),Dim,10)];
  if(Open&&!S.AppReadOnly)Row->AddSlot().AutoWidth()[OKFinButton(Weak,Cn?OKT(TEXT("ui.finance_apps.cancel_f3_button")):OKT(TEXT("ui.finance_apps.cancel_order_label")),[Weak,Id](){FString Error;auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("market"),Weak->State->App);C->SetNumberField(TEXT("order"),double(Id));Weak->Fin(TEXT("cancel"),C,Error);Weak->State->FinNotice=Error.IsEmpty()?OKT(TEXT("ui.finance_apps.cancelled_status")):Error;Weak->RefreshAppData(true);})];
  Add(3,Row);}
}

// Plan §15.6 / §15.7: the app buttons in the right panel — the counter at the shop, the computer where there is one,
// the ATM at the convenience store, and the phone apps (official site, notebook, read-only market, 網銀) anywhere.
void AOKStreetSocialScene::BuildFinanceRows(const TSharedRef<SVerticalBox>& Panel){
 auto& S=*State;if(!S.Mind)return;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 // One button first ("理財", with any prize waiting); the money apps open under it.
 const FString Title=S.FinClaims>0?OKMsg(TEXT("ui.finance_apps.finance_with_pending"), {{TEXT("pending_amount"), S.FinClaims}}):FString(OKT(TEXT("ui.finance_apps.finance_tab")));
 if(!S.FinMenuOpen){Panel->AddSlot().AutoHeight().Padding(2)[SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,Title,[Weak](){Weak->State->FinMenuOpen=true;Weak->RefreshUI();},true,FLinearColor(1,.85f,.3f))]];return;}
 const FString Place=PlaceHere(S.Male);auto Row=SNew(SWrapBox).UseAllottedSize(true);
 if(Place==TEXT("lottery")){FString Why;const bool Closed=PlaceClosedNow(TEXT("lottery"),Why)||Blacklisted(TEXT("lottery"),nullptr);
  // Plan §15.7.1: buying happens at the counter; walk up to it for the menu.
  if(Closed)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.lottery_shop_closed")),[](){},false)];
  else if(AtLotteryCounter())Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.lottery_counter_name")),[Weak](){auto& L=*Weak->State;L.LotTab=TEXT("sports");L.LotPicks.Reset();Weak->OpenApp(TEXT("lottery"),false);},true,FLinearColor(1,.85f,.3f))];
  else Row->AddSlot().Padding(2)[OKFinText(OKT(TEXT("ui.finance_apps.counter_required_note")),FLinearColor(1,.85f,.3f),10)];}
 FString Why;const FString Pc=ComputerHere(&Why);
 if(!Pc.IsEmpty()){
  Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.computer_a_share_title")),[Weak](){Weak->OpenApp(TEXT("cn"),false);},true,FLinearColor(1,.4f,.3f))];
  Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.computer_us_buoy_title")),[Weak](){Weak->OpenApp(TEXT("us"),false);},true,FLinearColor(.3f,.7f,1))];
 }
 if(Place==TEXT("conveni"))Row->AddSlot().Padding(2)[OKFinButton(Weak,TEXT("ATM"),[Weak](){Weak->OpenApp(TEXT("atm"),false);})];
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.phone_my_lottery")),[Weak](){Weak->State->LotTab=TEXT("mine");Weak->OpenApp(TEXT("lottery"),true);})];
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.phone_official_site")),[Weak](){Weak->OpenApp(TEXT("web"),true);})];
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.phone_notebook")),[Weak](){Weak->OpenApp(TEXT("notebook"),true);})];
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.phone_market_watch")),[Weak](){Weak->OpenApp(Weak->State->FinMarket.IsEmpty()?TEXT("cn"):Weak->State->FinMarket,true);})];
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.phone_online_banking")),[Weak](){Weak->OpenApp(TEXT("atm"),true);})];
 // Money between him and a heroine is talked over with her (plan §15 托付/翻身/聘用): only while talking to her.
 const int32 Talking=FreeTalkPartner()!=INDEX_NONE?FreeTalkPartner():S.Target;
 if(S.Manifest.Characters.IsValidIndex(Talking)&&Talking>0)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.finance_apps.talk_money_work"), {{TEXT("heroine_name"), *S.Manifest.Characters[Talking].Name}}),[Weak,Talking](){Weak->State->VentureHeroine=Talking;Weak->OpenApp(TEXT("venture"),false);},true,FLinearColor(1,.6f,.8f))];
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.phone_my_company")),[Weak](){Weak->OpenApp(TEXT("company"),false);})];
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.phone_accounting_ledger")),[Weak](){Weak->State->AccBook=TEXT("private");Weak->OpenApp(TEXT("accounts"),false);})];
 Panel->AddSlot().AutoHeight().Padding(2)[SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(Title,FLinearColor(1,.85f,.3f),11,true)]
   +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[OKFinButton(Weak,OKT(TEXT("ui.finance_apps.collapse_button")),[Weak](){Weak->State->FinMenuOpen=false;Weak->RefreshUI();})]]
  +SVerticalBox::Slot().AutoHeight()[Row]];
}
