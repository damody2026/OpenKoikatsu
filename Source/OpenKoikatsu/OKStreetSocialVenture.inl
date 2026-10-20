// Included by OKStreetSocialScene.cpp: plan §15 幫她翻身・分錢四選・托付 (P10), 拜金轉真愛, the identity question, and the
// one company of the 創業線 (P11). The game rules decide (Tools/ok-rules venture.rs); this side keeps the records, adds
// the reminder and shows the result; the model only voices what happened through memories and 【剛才】.

// The four-way split of what he made for her (plan §15): who keeps what, and what it does to her.
struct FOKStreetSplit { const TCHAR* Id; const TCHAR* Label; };
static const FOKStreetSplit OKStreetSplits[]={{TEXT("all"),TEXT("全給她")},{TEXT("cut20"),TEXT("抽 20%")},{TEXT("half"),TEXT("五五分")},{TEXT("principal"),TEXT("只拿本金")}};

int32 AOKStreetSocialScene::StartEntrust(int32 Heroine,const FString& Kind,int64 Amount,int32 Days){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Heroine)||Amount<=0)return 0;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Heroine]);auto Candidate=S.Store;
 if(!Candidate.Relations.IsValidIndex(OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true)))return 0;
 auto Request=OKRulesHeroineRequest(S,TEXT("entrust_start"),Id);
 Request->SetStringField(TEXT("kind"),Kind);Request->SetNumberField(TEXT("amount"),double(Amount));Request->SetNumberField(TEXT("days"),Days);Request->SetNumberField(TEXT("day"),Candidate.CalendarDay);
 Request->SetNumberField(TEXT("next_id"),Candidate.NextEntrust);Request->SetNumberField(TEXT("player_cash"),double(Candidate.PlayerCash));Request->SetStringField(TEXT("name"),S.Manifest.Characters[Heroine].Name);
 Request->SetBoolField(TEXT("has_open"),Candidate.Entrusts.ContainsByPredicate([&](const FOKStreetEntrust& E){return E.Heroine==Id&&E.Status==TEXT("open");}));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return 0;
 FString Refused;if(Out->TryGetStringField(TEXT("refused"),Refused)){S.Message=Refused;return 0;}
 const TSharedPtr<FJsonObject>* Made=nullptr;if(!Out->TryGetObjectField(TEXT("entrust"),Made))return 0;
 const FOKStreetEntrust E=OKRulesReadEntrust(**Made);Candidate.Entrusts.Add(E);Candidate.NextEntrust=FMath::Max(Candidate.NextEntrust,E.Id+1);
 // The money moves and the relation changes now; her note comes after the save.
 ApplyRulesEffects(Candidate,*Out); // her note waits for the commit
 if(!CommitStore(MoveTemp(Candidate)))return 0;
 S.Message=Out->GetStringField(TEXT("message"));
 const TSharedPtr<FJsonObject> A=Out->GetObjectField(TEXT("appointment"));
 AddAppointment(TEXT("entrust"),Heroine,FString(),int64(A->GetNumberField(TEXT("start"))),int32(A->GetNumberField(TEXT("grace"))),A->GetStringField(TEXT("label")));
 return E.Id;
}

// Settle: `Total` is what the money is worth now (entered by him); the split decides her share of the profit.
void AOKStreetSocialScene::SettleEntrust(int32 EntrustId,int64 Total,const FString& Split){
 auto& S=*State;auto Candidate=S.Store;
 FOKStreetEntrust* E=Candidate.Entrusts.FindByPredicate([&](const FOKStreetEntrust& X){return X.Id==EntrustId&&X.Status==TEXT("open");});if(!E)return;
 const int32 Heroine=CharacterOf(E->Heroine);if(Heroine==INDEX_NONE)return;
 if(!Candidate.Relations.IsValidIndex(OKStreetRelationIndex(Candidate,E->Heroine,S.Manifest.TalkBudget,true)))return;
 auto Request=OKRulesHeroineRequest(S,TEXT("entrust_settle"),E->Heroine);Request->SetObjectField(TEXT("entrust"),OKRulesEntrustJson(*E));
 Request->SetNumberField(TEXT("total"),double(Total));Request->SetStringField(TEXT("split"),Split);Request->SetNumberField(TEXT("player_cash"),double(Candidate.PlayerCash));Request->SetStringField(TEXT("name"),S.Manifest.Characters[Heroine].Name);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 FString Refused;if(Out->TryGetStringField(TEXT("refused"),Refused)){S.Message=Refused;return;}
 *E=OKRulesReadEntrust(*Out->GetObjectField(TEXT("entrust")));
 ApplyRulesEffects(Candidate,*Out); // her note waits for the commit
 if(!CommitStore(MoveTemp(Candidate)))return;
 S.Message=Out->GetStringField(TEXT("message"));
 RefreshUI();
}

// Daily (from NextDay): overdue money, money spent on someone else, 拜金轉真愛, the identity question, payroll.
void AOKStreetSocialScene::TickVentures(FOKStreetStore& Candidate){
 auto& S=*State;const int32 Day=Candidate.CalendarDay;
 // Overdue money, money spent on another girl, 拜金轉真愛, the identity question (venture.rs venture_daily).
 {auto Request=OKRulesRequest(TEXT("venture_daily"));Request->SetNumberField(TEXT("day"),Day);Request->SetNumberField(TEXT("player_cash"),double(Candidate.PlayerCash));
  TArray<TSharedPtr<FJsonValue>> Entrusts,Rumors,Heroines;
  for(const auto& E:Candidate.Entrusts)Entrusts.Add(MakeShared<FJsonValueObject>(OKRulesEntrustJson(E)));
  for(const auto& R:Candidate.Rumors){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("kind"),R.Kind);O->SetStringField(TEXT("about"),R.About);O->SetNumberField(TEXT("day"),R.Day);
   TArray<TSharedPtr<FJsonValue>> Known;for(const FString& K:R.KnownBy)Known.Add(MakeShared<FJsonValueString>(K));O->SetArrayField(TEXT("known_by"),Known);Rumors.Add(MakeShared<FJsonValueObject>(O));}
  TSet<FString> Ids;for(const auto& E:Candidate.Entrusts)if(E.Status==TEXT("open"))Ids.Add(E.Heroine);
  for(int32 I=1;I<S.Manifest.Characters.Num();++I)Ids.Add(OKStreetStableId(S.Manifest.Characters[I]));
  for(const FString& Id:Ids){const int32 X=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);if(!Candidate.Relations.IsValidIndex(X))continue;const int32 I=CharacterOf(Id);
   auto H=MakeShared<FJsonObject>();H->SetObjectField(TEXT("relation"),OKRulesRelationJson(Candidate.Relations[X]));H->SetBoolField(TEXT("present"),I!=INDEX_NONE);H->SetNumberField(TEXT("actor"),I);
   H->SetBoolField(TEXT("met_today"),I!=INDEX_NONE&&S.InteractedToday.Contains(I));if(const TSharedPtr<FJsonObject> P=S.MindProfiles.FindRef(Id))H->SetObjectField(TEXT("profile"),P.ToSharedRef());
   Heroines.Add(MakeShared<FJsonValueObject>(H));}
  Request->SetArrayField(TEXT("entrusts"),Entrusts);Request->SetArrayField(TEXT("rumors"),Rumors);Request->SetArrayField(TEXT("heroines"),Heroines);
  if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request)){
   Candidate.Entrusts.Reset();for(const auto& V:Out->GetArrayField(TEXT("entrusts")))Candidate.Entrusts.Add(OKRulesReadEntrust(*V->AsObject()));
   ApplyRulesEffects(Candidate,*Out);
  }}
 // His company's month (payroll, the books) is the rules' company day (CompanyDaily).
}

bool AOKStreetSocialScene::FoundCompany(const FString& Name,const FString& Office){
 // ¥50,000 of capital from his own money into the company's own account (estate.rs found).
 auto& S=*State;const bool Done=CompanyAct(TEXT("found"),[&](FJsonObject& A){A.SetStringField(TEXT("name"),Name);A.SetStringField(TEXT("office"),Office);});
 if(!Done)S.Message=S.FinNotice;
 return Done;
}

// Plan §15 Offer{role, pay, hours, title_public, exclusive}: her answer comes from the rules; when she is the one he
// is talking to, the model is told so she can voice it.
void AOKStreetSocialScene::OfferJob(int32 Heroine,const FString& JobRole,int64 Pay,bool TitlePublic,bool Exclusive){
 auto& S=*State;if(S.Store.Company.Founded<0||!S.Manifest.Characters.IsValidIndex(Heroine))return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Heroine]);
 if(S.Store.Company.Staff.ContainsByPredicate([&](const FOKStreetEmployee& E){return E.Heroine==Id;})){S.Message=OKT(TEXT("ui.venture.already_in_company"));return;}
 // Her answer is the rules' (venture.rs job_offer): her feelings, the pay against her income, the role, her pride.
 auto Request=OKRulesHeroineRequest(S,TEXT("job_offer"),Id);Request->SetStringField(TEXT("role"),JobRole);Request->SetNumberField(TEXT("pay"),double(Pay));
 Request->SetBoolField(TEXT("title_public"),TitlePublic);Request->SetBoolField(TEXT("exclusive"),Exclusive);Request->SetStringField(TEXT("company"),S.Store.Company.Name);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer.IsValid())return;
 const bool Accept=Answer->GetBoolField(TEXT("accept"));const FString Offer=Answer->GetStringField(TEXT("offer")),RoleName=Answer->GetStringField(TEXT("role_name"));
 if(Accept){
  auto Candidate=S.Store;FOKStreetEmployee E;E.Heroine=Id;E.Role=JobRole;E.Pay=Pay;E.TitlePublic=TitlePublic;E.Exclusive=Exclusive;E.Since=Candidate.CalendarDay;Candidate.Company.Staff.Add(E);
  if(!CommitStore(MoveTemp(Candidate)))return;
  HeroineNote(Heroine,TEXT("company_job"),Offer+OKT(TEXT("ui.venture.accepted_suffix")),PlaceHere(S.Male),0.85f);
  if(TitlePublic)MakeRumor(Heroine,TEXT("company"),OKMsg(TEXT("ui.venture.hired_as_role"), {{TEXT("heroine_name"), *S.Manifest.Characters[Heroine].Name}, {TEXT("role_name"), *RoleName}}),55.f,false);
  S.Message=OKMsg(TEXT("ui.venture.accepted_job_schedule"), {{TEXT("heroine_name"), *S.Manifest.Characters[Heroine].Name}});
 }else{HeroineNote(Heroine,TEXT("company_job"),Offer+OKT(TEXT("ui.venture.did_not_accept_suffix")),PlaceHere(S.Male),0.7f);S.Message=OKMsg(TEXT("ui.venture.declined_job_offer"), {{TEXT("heroine_name"), *S.Manifest.Characters[Heroine].Name}});}
 if(FreeTalkPartner()==Heroine)S.MindRecent.Add(Id,Offer+(Accept?OKT(TEXT("ui.venture.accept_dialogue_hint")):OKT(TEXT("ui.venture.decline_dialogue_hint"))));
 // Planning picks the office up at the next schedule pass.
 S.ScheduleTickAt=0;RefreshUI();
}

void AOKStreetSocialScene::FireStaff(const FString& Id){
 auto& S=*State;auto Candidate=S.Store;const int32 N=Candidate.Company.Staff.RemoveAll([&](const FOKStreetEmployee& E){return E.Heroine==Id;});if(!N)return;
 // Trust and resentment, and her note (venture.rs job_fire).
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesHeroineRequest(S,TEXT("job_fire"),Id));
 if(Out.IsValid())ApplyRulesEffects(Candidate,*Out); // her note waits for the commit
 if(!CommitStore(MoveTemp(Candidate)))return;
 RefreshUI();
}

// The 托付・翻身・公司 app.
TSharedRef<SWidget> AOKStreetSocialScene::BuildVentureApp(){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Box=SNew(SVerticalBox);
 auto Line=[&](TSharedRef<SWidget> W){Box->AddSlot().AutoHeight().Padding(2)[W];};
 Line(OKFinText(OKMsg(TEXT("ui.finance_apps.wallet_bank_balance"), {{TEXT("cash"), S.Store.PlayerCash}, {TEXT("bank"), S.Store.PlayerBank}}),FLinearColor(1,.85f,.3f),14,true));
 Line(SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKFinText(OKT(TEXT("ui.venture.amount_settlement_total_label")))]
  +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SBox).WidthOverride(120)[SNew(SEditableTextBox).Text(FText::FromString(S.VentureAmount)).OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->VentureAmount=T.ToString();})]]
  +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0,4,0)[OKFinText(OKT(TEXT("ui.venture.days_label")))]
  +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(60)[SNew(SEditableTextBox).Text(FText::FromString(S.VentureDays)).OnTextChanged_Lambda([Weak](const FText& T){if(Weak.IsValid())Weak->State->VentureDays=T.ToString();})]]);
 // Only the heroine he is talking with (her own money and her job are discussed face to face).
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  if(I!=S.VentureHeroine)continue;
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
  bool bSecret=false;const float Stress=MoneyStressOf(R,ProfileOf(I),bSecret);
  Line(OKFinText(OKMsg(TEXT("ui.venture.heroine_money_status"), {{TEXT("heroine_name"), *S.Manifest.Characters[I].Name}, {TEXT("cash"), FMath::Max<int64>(0,R.Cash)}, {TEXT("trust"), R.Trust}, {TEXT("tightness"), Stress>60?OKT(TEXT("ui.venture.tightness_tight")):Stress>30?OKT(TEXT("ui.mind.normal_label")):OKT(TEXT("ui.venture.tightness_comfortable"))}}),FLinearColor(.6f,.85f,1),12,true));
  const FOKStreetEntrust* Open=S.Store.Entrusts.FindByPredicate([&](const FOKStreetEntrust& E){return E.Heroine==Id&&E.Status==TEXT("open");});
  auto Row=SNew(SWrapBox).UseAllottedSize(true);
  if(Open){
   const int32 EId=Open->Id;Row->AddSlot().Padding(2)[OKFinText(OKMsg(TEXT("ui.venture.venture_loan_line"), {{TEXT("kind_label"), Open->Kind==TEXT("entrust")?OKT(TEXT("ui.venture.entrusted_kind")):OKT(TEXT("ui.venture.turnaround_kind"))}, {TEXT("principal"), Open->Principal}, {TEXT("due_date"), *OKStreetCalendarLabel(Open->Due,20.0)}, {TEXT("overdue_mark"), Open->Overdue?OKT(TEXT("ui.venture.overdue_mark")):TEXT("")}}))];
   for(const auto& Sp:OKStreetSplits){const FString Split=Sp.Id;Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.venture.settlement_label_line"), {{TEXT("settlement_label"), OKT(Sp.Label)}}),[Weak,EId,Split](){Weak->SettleEntrust(EId,Weak->MoneyIn(Weak->State->VentureAmount),Split);})];}
  }else{
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.venture.ask_entrust_me")),[Weak,I](){Weak->StartEntrust(I,TEXT("entrust"),Weak->MoneyIn(Weak->State->VentureAmount),FCString::Atoi(*Weak->State->VentureDays));})];
   Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.venture.use_my_money_turnaround")),[Weak,I](){Weak->StartEntrust(I,TEXT("help"),Weak->MoneyIn(Weak->State->VentureAmount),FCString::Atoi(*Weak->State->VentureDays));})];
  }
  if(S.Store.Company.Founded>=0){
   const FOKStreetEmployee* Emp=S.Store.Company.Staff.FindByPredicate([&](const FOKStreetEmployee& E){return E.Heroine==Id;});
   if(Emp)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.venture.dismiss_employee_button"), {{TEXT("role"), *Emp->Role}, {TEXT("pay"), Emp->Pay}}),[Weak,Id](){Weak->FireStaff(Id);})];
   else for(const auto& RoleOption:{TPair<const TCHAR*,const TCHAR*>(TEXT("ceo"),OKT(TEXT("ui.venture.hire_as_ceo"))),TPair<const TCHAR*,const TCHAR*>(TEXT("secretary"),OKT(TEXT("ui.venture.hire_as_secretary"))),TPair<const TCHAR*,const TCHAR*>(TEXT("manager"),OKT(TEXT("ui.venture.hire_as_manager")))}){
    const FString RoleId=RoleOption.Key;Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.venture.monthly_salary_note"), {{TEXT("salary"), RoleOption.Value}}),[Weak,I,RoleId](){Weak->OfferJob(I,RoleId,FMath::Max<int64>(Weak->Local(TEXT("salary"),2000),Weak->MoneyIn(Weak->State->VentureAmount)),true,false);})];}
  }
  Line(Row);
 }
 Line(OKFinText(OKT(TEXT("ui.venture.startup_line")),FLinearColor(1,.85f,.3f),14,true));
 const auto& Co=S.Store.Company;
 if(Co.Founded<0)Line(SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.venture.found_company_cafe")),[Weak](){Weak->FoundCompany(FString(),TEXT("cafe"));})]
  +SHorizontalBox::Slot().AutoWidth()[OKFinButton(Weak,OKT(TEXT("ui.venture.found_company_mall")),[Weak](){Weak->FoundCompany(FString(),TEXT("department"));})]);
 else Line(OKFinText(OKMsg(TEXT("ui.venture.company_summary_line"), {{TEXT("company_name"), *Co.Name}, {TEXT("founded_date"), *OKStreetCalendarLabel(Co.Founded,12.0)}, {TEXT("office_name"), OKStreetFindPlace(Co.Office)?OKStreetFindPlace(Co.Office)->Name:*Co.Office}, {TEXT("staff_count"), Co.Staff.Num()}, {TEXT("last_profit"), Co.LastProfit}})));
 Line(OKFinText(OKT(TEXT("ui.venture.settle_amount_explainer")),FLinearColor(.75f,.75f,.75f),10));
 BuildHerInvestRows(Box);
 return SNew(SScrollBox)+SScrollBox::Slot()[Box];
}
