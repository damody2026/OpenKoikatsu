// His company and the heroines' (Docs/STREET_COMPANY_PLAN_20261012.md, Docs/STREET_HEROINE_COMPANY_PLAN_20261012.md).
// The game rules run them (Tools/ok-rules estate.rs): property, people, the monthly books with Chinese taxes,
// bankruptcy, her company opening and closing. This side keeps the books (the wallet below journals every movement of a
// company's money), sends the companies to the rules with what only the scene knows, replays the wallet operations
// they answer with, and shows the result. Included by OKStreetSocialScene.cpp before the finance apps.

namespace {
const FString OKBorrowPrefix=TEXT("borrow:");
// A borrowed corner of a public place (the old "office at the café").
FOKStreetUnitDef OKBorrowedUnit(const FString& Unit){
 FOKStreetUnitDef U;U.Id=Unit;U.Place=Unit.Mid(OKBorrowPrefix.Len());U.Kind=TEXT("office");U.Capacity=3;U.Traffic=0.3f;
 const auto* P=OKStreetFindPlace(U.Place);U.Name=OKMsg(TEXT("ui.company.borrowed_corner_label"), {{TEXT("place_name"), P?P->Name:*U.Place}});return U;
}
}

// What a holding is (its unit from the property data, or a borrowed corner): names and places for the screens.
FOKStreetUnitDef AOKStreetSocialScene::UnitOf(const FString& Unit) const{
 if(Unit.StartsWith(OKBorrowPrefix))return OKBorrowedUnit(Unit);
 if(const auto* U=OKStreetProperty().Unit(Unit))return *U;
 FOKStreetUnitDef Missing;Missing.Id=Unit;Missing.Name=Unit;return Missing;
}
// Where people work for that holding: the building it is in, or the lot itself (a place too).
FString AOKStreetSocialScene::HoldingPlace(const FOKStreetHolding& H) const{
 const FOKStreetUnitDef U=UnitOf(H.Unit);return U.bLot?U.Id:U.Place;
}
// The company's headquarters place (where heroines who work for him go), for the day plans.
FString AOKStreetSocialScene::CompanyHqPlace() const{
 const auto& Co=State->Store.Company;
 if(const FOKStreetHolding* H=Co.Estate.Hq())return HoldingPlace(*H);
 return Co.Office.IsEmpty()?FString(TEXT("cafe")):Co.Office;
}

// Who pays and gets paid for a company: its own account (Estate.Cash), never the owner's pocket. Short of money and
// with the owner lending on their own (bAutoFund), the rest is borrowed from the owner (him: savings, then wallet; a
// heroine: her bank, then her cash) as an owner's loan. Money goes back to the owner only under a name: the loan
// repaid, a dividend, what is left when it closes (OKStreetSocialBooks.inl journals both sides). The rules decide
// what is paid; this is the bookkeeping that carries it out.
struct FOKCompanyWallet{
 FOKStreetStore* Store=nullptr;int32 Rel=INDEX_NONE;FOKStreetEstate* Book=nullptr;FString BookId=TEXT("company");
 int64 OwnerFunds() const{if(Rel<0)return Store->PlayerBank+Store->PlayerCash;const auto& R=Store->Relations[Rel];return R.Bank+FMath::Max<int64>(0,R.Cash);}
 int64 Funds() const{return FMath::Max<int64>(0,Book->Cash)+(Book->bAutoFund?OwnerFunds():0);}
 void Note(int64 Amount,const FString& Kind,const FString& Text) const{
  auto& L=Book->Ledger;L.Add({Store->CalendarDay,Kind,Text,Amount});if(L.Num()>365)L.RemoveAt(0,L.Num()-365);OKBookPost(*Store,BookId,TEXT("cash"),Amount,Kind,Text);
  if(OKBookCategoryKind(Kind)==TEXT("pl"))Book->Retained+=Amount;} // every income and expense, whenever it happens, is the profit kept
 // The owner puts money in: Kind owner_loan (to be repaid) or capital.
 bool FromOwner(int64 Amount,const FString& Kind,const FString& Text) const{
  if(Amount<=0)return true;if(OwnerFunds()<Amount)return false;
  if(Rel<0)OKPrivateTake(*Store,Amount,Kind,Text);
  else{auto& R=Store->Relations[Rel];const int64 B=FMath::Min(R.Bank,Amount);R.Bank-=B;if(Amount>B)R.Cash=FMath::Max<int64>(0,R.Cash)-(Amount-B);}
  Book->Cash+=Amount;Note(Amount,Kind,Text);
  if(Kind==TEXT("owner_loan")){Book->OwnerLoan+=Amount;OKBookPost(*Store,BookId,TEXT("owner_loan"),-Amount,Kind,Text);if(Rel<0)OKBookPost(*Store,TEXT("private"),TEXT("owner_loan"),Amount,Kind,Text);}
  else Book->Capital+=Amount;
  return true;
 }
 // Money out to the owner (owner_repay, dividend, liquidation); any tax on it is the caller's.
 void ToOwner(int64 Amount,const FString& Kind,const FString& Text) const{
  if(Amount<=0)return;Book->Cash-=Amount;Note(-Amount,Kind,Text);
  if(Kind==TEXT("owner_repay")){Book->OwnerLoan-=Amount;OKBookPost(*Store,BookId,TEXT("owner_loan"),Amount,Kind,Text);if(Rel<0)OKBookPost(*Store,TEXT("private"),TEXT("owner_loan"),-Amount,Kind,Text);}
  if(Rel<0)OKBank(*Store,Amount,Kind==TEXT("dividend")?FString(TEXT("dividend_in")):Kind,Text);else Store->Relations[Rel].Bank+=Amount;
 }
 bool Pay(int64 Amount,const FString& Kind,const FString& Text) const{
  if(Amount<=0)return true;if(Funds()<Amount)return false;
  const int64 Has=FMath::Max<int64>(0,Book->Cash);if(Has<Amount)FromOwner(Amount-Has,TEXT("owner_loan"),OKT(TEXT("ui.company.company_borrow_from_owner")));
  Book->Cash-=Amount;Note(-Amount,Kind,Text);return true;
 }
 void Receive(int64 Amount,const FString& Kind,const FString& Text) const{if(Amount==0)return;Book->Cash+=Amount;Note(Amount,Kind,Text);}
 void RepayOwner(int64 Most) const{const int64 N=FMath::Min3(Most,Book->OwnerLoan,Book->Cash);if(N>0)ToOwner(N,TEXT("owner_repay"),OKT(TEXT("ui.company.repay_owner_loan")));}
};
FOKCompanyWallet OKPlayerWallet(FOKStreetStore& C){FOKCompanyWallet W;W.Store=&C;W.Book=&C.Company.Estate;return W;}

// His company pays or is paid (false: nothing paid, not enough even with what he would lend it).
bool OKStreetCompanyPay(FOKStreetStore& Store,int64 Amount,const FString& Kind,const FString& Note){return OKPlayerWallet(Store).Pay(Amount,Kind,Note);}
void OKStreetCompanyReceive(FOKStreetStore& Store,int64 Amount,const FString& Kind,const FString& Note){OKPlayerWallet(Store).Receive(Amount,Kind,Note);}

bool AOKStreetSocialScene::CompanyCommit(FOKStreetStore&& Candidate,const FString& Message){
 auto& S=*State;if(!CommitStore(MoveTemp(Candidate))){S.FinNotice=OKT(TEXT("ui.company.save_failed_cancel"));RefreshUI();return false;}
 S.FinNotice=Message;S.Message=Message;S.ScheduleTickAt=0;RefreshUI();UE_LOG(LogTemp,Display,TEXT("Street company: %s"),*Message);return true;
}

// ------------------------------------------------------------------------------------------------ to and from the rules

namespace {
TSharedRef<FJsonObject> OKCompanyJson(const FOKStreetCompany& Co){
 auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("name"),Co.Name);C->SetStringField(TEXT("office"),Co.Office);C->SetNumberField(TEXT("founded"),Co.Founded);C->SetNumberField(TEXT("paid_month"),Co.PaidMonth);
 C->SetNumberField(TEXT("capital"),double(Co.Capital));C->SetNumberField(TEXT("last_profit"),double(Co.LastProfit));
 TArray<TSharedPtr<FJsonValue>> Staff;for(const auto& E:Co.Staff){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("heroine"),E.Heroine);O->SetStringField(TEXT("role"),E.Role);O->SetNumberField(TEXT("pay"),double(E.Pay));
  O->SetBoolField(TEXT("title_public"),E.TitlePublic);O->SetBoolField(TEXT("exclusive"),E.Exclusive);O->SetNumberField(TEXT("since"),E.Since);O->SetNumberField(TEXT("missed"),E.Missed);Staff.Add(MakeShared<FJsonValueObject>(O));}
 C->SetArrayField(TEXT("staff"),Staff);C->SetObjectField(TEXT("estate"),OKStreetWriteEstate(Co.Estate));
 return C;
}
// Everything of an estate the rules decide; its money columns and ledger stay as the wallet left them.
void OKEstateReadRules(FOKStreetEstate& E,const TSharedPtr<FJsonObject>& O){
 FOKStreetEstate N;OKStreetReadEstate(O,N);
 E.Holdings=MoveTemp(N.Holdings);E.Workers=MoveTemp(N.Workers);E.Applicants=MoveTemp(N.Applicants);E.Refused=MoveTemp(N.Refused);
 E.TaxYear=N.TaxYear;E.LossCarry=N.LossCarry;E.YtdProfit=N.YtdProfit;E.OwnerPay=N.OwnerPay;E.bAutoFund=N.bAutoFund;
 E.ApplicantWeek=N.ApplicantWeek;E.NextWorker=N.NextWorker;E.Reputation=N.Reputation;E.Debt=N.Debt;
}
void OKCompanyReadRules(FOKStreetCompany& Co,const TSharedPtr<FJsonObject>& O){
 if(!O.IsValid())return;double V=0;
 O->TryGetStringField(TEXT("name"),Co.Name);O->TryGetStringField(TEXT("office"),Co.Office);
 if(O->TryGetNumberField(TEXT("founded"),V))Co.Founded=int32(V);if(O->TryGetNumberField(TEXT("paid_month"),V))Co.PaidMonth=int32(V);
 if(O->TryGetNumberField(TEXT("capital"),V))Co.Capital=int64(V);if(O->TryGetNumberField(TEXT("last_profit"),V))Co.LastProfit=int64(V);
 Co.Staff.Reset();const TArray<TSharedPtr<FJsonValue>>* Staff=nullptr;
 if(O->TryGetArrayField(TEXT("staff"),Staff))for(const auto& Row:*Staff){const auto X=Row->AsObject();FOKStreetEmployee E;X->TryGetStringField(TEXT("heroine"),E.Heroine);X->TryGetStringField(TEXT("role"),E.Role);
  if(X->TryGetNumberField(TEXT("pay"),V))E.Pay=int64(V);X->TryGetBoolField(TEXT("title_public"),E.TitlePublic);X->TryGetBoolField(TEXT("exclusive"),E.Exclusive);
  if(X->TryGetNumberField(TEXT("since"),V))E.Since=int32(V);if(X->TryGetNumberField(TEXT("missed"),V))E.Missed=int32(V);if(!E.Heroine.IsEmpty())Co.Staff.Add(E);}
 const TSharedPtr<FJsonObject>* E=nullptr;if(O->TryGetObjectField(TEXT("estate"),E))OKEstateReadRules(Co.Estate,*E);
}
void OKHeroReadRules(FOKStreetHeroCompany& H,const TSharedPtr<FJsonObject>& O){
 FOKStreetHeroCompany N;OKStreetReadHeroCompany(O,N);
 H.Name=N.Name;H.Kind=N.Kind;H.Founded=N.Founded;H.PaidMonth=N.PaidMonth;H.ProfitStreak=N.ProfitStreak;H.LossStreak=N.LossStreak;H.OfferUntil=N.OfferUntil;H.OfferWeek=N.OfferWeek;
 H.Capital=N.Capital;H.LastProfit=N.LastProfit;H.OfferWage=N.OfferWage;H.PlayerMinutes=N.PlayerMinutes;
 const TSharedPtr<FJsonObject>* E=nullptr;if(O->TryGetObjectField(TEXT("estate"),E))OKEstateReadRules(H.Estate,*E);
}
}

// The companies as the rules see them, with the money of whoever owns them and what only the scene knows (names,
// places, the heroines' profiles).
TSharedRef<FJsonObject> AOKStreetSocialScene::CompanyWorld(const FOKStreetStore& C,const TCHAR* Kind) const{
 const auto& S=*State;auto R=OKRulesRequest(Kind);
 R->SetNumberField(TEXT("day"),C.CalendarDay);R->SetNumberField(TEXT("player_cash"),double(C.PlayerCash));R->SetNumberField(TEXT("player_bank"),double(C.PlayerBank));R->SetNumberField(TEXT("seed"),FMath::Rand());
 R->SetObjectField(TEXT("company"),OKCompanyJson(C.Company));
 TArray<TSharedPtr<FJsonValue>> Heroes;for(const auto& H:C.HeroCompanies)Heroes.Add(MakeShared<FJsonValueObject>(OKStreetWriteHeroCompany(H)));R->SetArrayField(TEXT("hero_companies"),Heroes);
 TArray<TSharedPtr<FJsonValue>> Rels;for(const auto& X:C.Relations)Rels.Add(MakeShared<FJsonValueObject>(OKRulesRelationJson(X)));R->SetArrayField(TEXT("relations"),Rels);
 auto Profiles=MakeShared<FJsonObject>(),Names=MakeShared<FJsonObject>(),Places=MakeShared<FJsonObject>();
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){const FString Id=OKStreetStableId(S.Manifest.Characters[I]);Names->SetStringField(Id,S.Manifest.Characters[I].Name);if(const TSharedPtr<FJsonObject> P=S.MindProfiles.FindRef(Id))Profiles->SetObjectField(Id,P.ToSharedRef());}
 for(const auto& P:OKStreetPlaces())Places->SetStringField(P.Id,P.Name);
 R->SetObjectField(TEXT("profiles"),Profiles);R->SetObjectField(TEXT("names"),Names);R->SetObjectField(TEXT("place_names"),Places);
 R->SetObjectField(TEXT("personal"),OKStreetWritePersonal(C.Personal));
 TArray<TSharedPtr<FJsonValue>> Jobs;for(const FString& J:C.PlayerJobs)Jobs.Add(MakeShared<FJsonValueString>(J));R->SetArrayField(TEXT("player_jobs"),Jobs);
 auto Rating=MakeShared<FJsonObject>();for(const auto& P:C.JobRating)Rating->SetNumberField(P.Key,P.Value);R->SetObjectField(TEXT("job_rating"),Rating);
 return R;
}

// The wallet operations and structural changes of a rules' answer, in order (ApplyRulesEffects forwards them here).
void AOKStreetSocialScene::ApplyCompanyEffect(FOKStreetStore& C,const FJsonObject& E){
 const FString Op=E.GetStringField(TEXT("op"));
 if(Op==TEXT("hero_open")){FOKStreetHeroCompany H;OKStreetReadHeroCompany(E.GetObjectField(TEXT("company")),H);C.HeroCompanies.Add(H);return;}
 if(Op==TEXT("hero_close")){const FString OwnerId=E.GetStringField(TEXT("owner"));C.HeroCompanies.RemoveAll([&](const FOKStreetHeroCompany& H){return H.Owner==OwnerId;});return;}
 const FString Book=E.GetStringField(TEXT("book")),Act=E.GetStringField(TEXT("act")),Kind=E.GetStringField(TEXT("kind")),Note=E.GetStringField(TEXT("note"));const int64 Amount=int64(E.GetNumberField(TEXT("amount")));
 FOKCompanyWallet W;
 if(Book.StartsWith(TEXT("hc_"))){FOKStreetHeroCompany* H=HeroCompanyOf(C,Book.Mid(3));if(!H){UE_LOG(LogTemp,Warning,TEXT("Street rules: wallet %s has no company"),*Book);return;}W=HeroWallet(C,*H);}
 else W=OKPlayerWallet(C);
 bool Ok=true;
 if(Act==TEXT("pay"))Ok=W.Pay(Amount,Kind,Note);
 else if(Act==TEXT("receive"))W.Receive(Amount,Kind,Note);
 else if(Act==TEXT("from_owner"))Ok=W.FromOwner(Amount,Kind,Note);
 else if(Act==TEXT("to_owner"))W.ToOwner(Amount,Kind,Note);
 else if(Act==TEXT("repay_owner"))W.RepayOwner(Amount);
 else if(Act==TEXT("reset")){W.Book->Cash=0;W.Book->Capital=0;W.Book->OwnerLoan=0;W.Book->Retained=0;}
 else if(Act==TEXT("close_books")){W.Book->Capital=0;W.Book->Retained=0;}
 else if(Act==TEXT("retained"))W.Book->Retained+=Amount;
 else if(Act==TEXT("write_off")){W.Book->OwnerLoan=0;OKBookPost(C,W.BookId,TEXT("owner_loan"),Amount,TEXT("liquidation"),Note);if(W.Rel<0)OKBookPost(C,TEXT("private"),TEXT("owner_loan"),-Amount,TEXT("bad_debt"),OKT(TEXT("ui.company.company_loan_unrecovered")));}
 if(!Ok)UE_LOG(LogTemp,Warning,TEXT("Street rules: wallet %s %s %lld failed (the rules and the books disagree)"),*Book,*Act,Amount);
}

// A company answer written into C: the wallet replay and the memories (effects), then what the rules decided about the
// companies, his tax records and jobs. Logs when the rules' money and the books' replay differ.
void AOKStreetSocialScene::CompanyApply(FOKStreetStore& C,const FJsonObject& A){
 auto& S=*State;ApplyRulesEffects(C,A);
 OKCompanyReadRules(C.Company,A.GetObjectField(TEXT("company")));
 const TArray<TSharedPtr<FJsonValue>>* Heroes=nullptr;
 if(A.TryGetArrayField(TEXT("hero_companies"),Heroes))for(const auto& V:*Heroes){const auto O=V->AsObject();if(FOKStreetHeroCompany* H=HeroCompanyOf(C,O->GetStringField(TEXT("owner"))))OKHeroReadRules(*H,O);}
 {FOKStreetPersonal P;OKStreetReadPersonal(A.GetObjectField(TEXT("personal")),P);C.Personal=P;}
 C.PlayerJobs.Reset();for(const auto& V:A.GetArrayField(TEXT("player_jobs")))C.PlayerJobs.Add(V->AsString());
 for(const auto& P:A.GetObjectField(TEXT("job_rating"))->Values)C.JobRating.Add(FString(*P.Key),int32(P.Value->AsNumber()));
 const FString Payslip=A.GetStringField(TEXT("payslip"));if(!Payslip.IsEmpty())S.PayslipNote=Payslip;
 const TSharedPtr<FJsonObject> Check=A.GetObjectField(TEXT("check"));
 const int64 RCash=int64(Check->GetNumberField(TEXT("player_cash"))),RBank=int64(Check->GetNumberField(TEXT("player_bank")));
 const int64 RCo=int64(A.GetObjectField(TEXT("company"))->GetObjectField(TEXT("estate"))->GetNumberField(TEXT("cash")));
 if(RCash!=C.PlayerCash||RBank!=C.PlayerBank||(C.Company.Founded>=0&&RCo!=C.Company.Estate.Cash))
  UE_LOG(LogTemp,Warning,TEXT("Street rules: company money differs: rules cash %lld bank %lld company %lld, books cash %lld bank %lld company %lld"),RCash,RBank,RCo,C.PlayerCash,C.PlayerBank,C.Company.Estate.Cash);
}

// One of his company actions (estate.rs company_action): the rules check it and answer; a refusal is shown.
bool AOKStreetSocialScene::CompanyAct(const FString& Action,TFunctionRef<void(FJsonObject&)> Args){
 auto& S=*State;auto C=S.Store;const TSharedRef<FJsonObject> Request=CompanyWorld(C,TEXT("company_action"));Request->SetStringField(TEXT("action"),Action);Args(*Request);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return false;
 FString Notice;if(Out->TryGetStringField(TEXT("notice"),Notice)){S.FinNotice=Notice;RefreshUI();return false;}
 if(!Out->HasField(TEXT("company")))return false; // nothing to do
 CompanyApply(C,*Out);
 return CompanyCommit(MoveTemp(C),Out->GetStringField(TEXT("message")));
}

// The same on a store being changed elsewhere (a shift's pay, the in-game test): applied to C, not committed.
bool AOKStreetSocialScene::CompanyActOn(FOKStreetStore& C,const FString& Action,TFunctionRef<void(FJsonObject&)> Args){
 const TSharedRef<FJsonObject> Request=CompanyWorld(C,TEXT("company_action"));Request->SetStringField(TEXT("action"),Action);Args(*Request);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(*State,Request);if(!Out.IsValid()||!Out->HasField(TEXT("company")))return false;
 CompanyApply(C,*Out);const FString Message=Out->GetStringField(TEXT("message"));if(!Message.IsEmpty())State->FreeTalkNotice=Message;return true;
}

// What the company screens show (estate.rs company_view): each holding's figures, his equity, the market today.
TSharedPtr<FJsonObject> AOKStreetSocialScene::CompanyView(const FOKStreetStore& C) const{
 return OKRulesEval(*State,CompanyWorld(C,TEXT("company_view")));
}
int64 AOKStreetSocialScene::CompanyEquity(const FOKStreetStore& C) const{
 const TSharedPtr<FJsonObject> V=CompanyView(C);return V.IsValid()?int64(V->GetNumberField(TEXT("equity"))):0;
}

// ------------------------------------------------------------------------------------------------ his actions

void AOKStreetSocialScene::CompanyRent(const FString& UnitId){CompanyAct(TEXT("rent"),[&](FJsonObject& A){A.SetStringField(TEXT("unit"),UnitId);});}
void AOKStreetSocialScene::CompanyBuy(const FString& UnitId,bool bLoan){CompanyAct(TEXT("buy"),[&](FJsonObject& A){A.SetStringField(TEXT("unit"),UnitId);A.SetBoolField(TEXT("loan"),bLoan);});}
void AOKStreetSocialScene::CompanyBuild(const FString& UnitId,const FString& BuildingId){CompanyAct(TEXT("build"),[&](FJsonObject& A){A.SetStringField(TEXT("unit"),UnitId);A.SetStringField(TEXT("building"),BuildingId);});}
void AOKStreetSocialScene::CompanySetHq(const FString& UnitId){CompanyAct(TEXT("set_hq"),[&](FJsonObject& A){A.SetStringField(TEXT("unit"),UnitId);});}
// A lease ends at the start of next month (notice), the deposit back less what is owed; a borrowed corner at once.
void AOKStreetSocialScene::CompanyLeave(const FString& UnitId){CompanyAct(TEXT("leave"),[&](FJsonObject& A){A.SetStringField(TEXT("unit"),UnitId);});}
void AOKStreetSocialScene::CompanySell(const FString& UnitId){CompanyAct(TEXT("sell"),[&](FJsonObject& A){A.SetStringField(TEXT("unit"),UnitId);});}
void AOKStreetSocialScene::CompanyPayOff(const FString& UnitId){CompanyAct(TEXT("pay_off"),[&](FJsonObject& A){A.SetStringField(TEXT("unit"),UnitId);});}
// His own place offered for rent to others (0: taken off the market); with a tenant, ending the lease gives notice.
void AOKStreetSocialScene::CompanyLeaseOut(const FString& UnitId,int64 Rent){CompanyAct(TEXT("lease_out"),[&](FJsonObject& A){A.SetStringField(TEXT("unit"),UnitId);A.SetNumberField(TEXT("rent"),double(Rent));});}
// The interview: at or above what they ask, a yes; below it, less likely the lower (and the worse his reputation).
void AOKStreetSocialScene::CompanyHire(const FString& ApplicantId,int64 Offer,const FString& UnitId){
 CompanyAct(TEXT("hire"),[&](FJsonObject& A){A.SetStringField(TEXT("applicant"),ApplicantId);A.SetNumberField(TEXT("offer"),double(Offer));A.SetStringField(TEXT("unit"),UnitId);A.SetNumberField(TEXT("roll"),FMath::FRand());});
}
void AOKStreetSocialScene::CompanyFire(const FString& WorkerId){CompanyAct(TEXT("fire"),[&](FJsonObject& A){A.SetStringField(TEXT("worker"),WorkerId);});}
void AOKStreetSocialScene::CompanyRaise(const FString& WorkerId,int64 Pay){CompanyAct(TEXT("raise"),[&](FJsonObject& A){A.SetStringField(TEXT("worker"),WorkerId);A.SetNumberField(TEXT("pay"),double(Pay));});}
void AOKStreetSocialScene::CompanyAssign(const FString& WorkerId,const FString& UnitId){CompanyAct(TEXT("assign"),[&](FJsonObject& A){A.SetStringField(TEXT("worker"),WorkerId);A.SetStringField(TEXT("unit"),UnitId);});}

// The owner's money: capital in, the loan repaid, a bonus, a dividend, his monthly pay, lending on its own.
void AOKStreetSocialScene::CompanyInject(int64 Amount){CompanyAct(TEXT("inject"),[&](FJsonObject& A){A.SetNumberField(TEXT("amount"),double(Amount));});}
void AOKStreetSocialScene::CompanyRepayOwner(int64 Amount){CompanyAct(TEXT("repay_owner"),[&](FJsonObject& A){A.SetNumberField(TEXT("amount"),double(Amount));});}
void AOKStreetSocialScene::CompanyBonus(int64 Amount){CompanyAct(TEXT("bonus"),[&](FJsonObject& A){A.SetNumberField(TEXT("amount"),double(Amount));});}
void AOKStreetSocialScene::CompanyDividend(int64 Amount){CompanyAct(TEXT("dividend"),[&](FJsonObject& A){A.SetNumberField(TEXT("amount"),double(Amount));});}
void AOKStreetSocialScene::CompanySetOwnerPay(int64 Pay){CompanyAct(TEXT("owner_pay"),[&](FJsonObject& A){A.SetNumberField(TEXT("pay"),double(Pay));});}
void AOKStreetSocialScene::CompanyAutoFund(bool bOn){CompanyAct(TEXT("auto_fund"),[&](FJsonObject& A){A.SetBoolField(TEXT("on"),bOn);});}

// Pay he earned as an employee already in his wallet: his 五險一金 share and the income tax withheld (the payslip).
FString AOKStreetSocialScene::WithholdPlayerWage(FOKStreetStore& C,int64 Gross,int64 Minutes){
 auto& S=*State;const FString Before=S.PayslipNote;S.PayslipNote.Reset();
 CompanyActOn(C,TEXT("withhold"),[&](FJsonObject& A){A.SetNumberField(TEXT("gross"),double(Gross));A.SetNumberField(TEXT("minutes"),double(Minutes));});
 const FString Line=S.PayslipNote;S.PayslipNote=Before;return Line;
}

// His company's month on C right now (the in-game accounting test).
bool AOKStreetSocialScene::CompanySettleMonth(FOKStreetStore& C){return CompanyActOn(C,TEXT("settle_month"),[](FJsonObject&){});}
// A borrowed corner when his company has no place (the in-game tests set up from there).
void AOKStreetSocialScene::CompanyEnsure(FOKStreetStore& C){CompanyActOn(C,TEXT("ensure"),[](FJsonObject&){});}

// ------------------------------------------------------------------------------------------------ the day

// Every new day (from NextDay): his month on the 1st, his company's day (buildings, morale, applicants), and the heroines'
// companies (opening, running, offering him a job, growing, closing).
void AOKStreetSocialScene::CompanyDaily(FOKStreetStore& C){
 auto& S=*State;const TSharedRef<FJsonObject> Request=CompanyWorld(C,TEXT("company_daily"));
 TArray<TSharedPtr<FJsonValue>> Visited;for(const FString& U:S.CompanyVisited)Visited.Add(MakeShared<FJsonValueString>(U));Request->SetArrayField(TEXT("company_visited"),Visited);
 TArray<TSharedPtr<FJsonValue>> Heroines;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){const FString Id=OKStreetStableId(S.Manifest.Characters[I]);OKStreetRelationIndex(C,Id,S.Manifest.TalkBudget,true);
  auto H=MakeShared<FJsonObject>();H->SetStringField(TEXT("id"),Id);H->SetNumberField(TEXT("actor"),I);H->SetNumberField(TEXT("seed"),int32(GetTypeHash(Id)&0xffff));Heroines.Add(MakeShared<FJsonValueObject>(H));}
 Request->SetArrayField(TEXT("heroines"),Heroines);Request->SetBoolField(TEXT("self_test"),S.SelfTest);
 // The relations created above are part of the world the rules see.
 TArray<TSharedPtr<FJsonValue>> Rels;for(const auto& X:C.Relations)Rels.Add(MakeShared<FJsonValueObject>(OKRulesRelationJson(X)));Request->SetArrayField(TEXT("relations"),Rels);
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request))CompanyApply(C,*Out);
 S.CompanyVisited.Reset();
}

// For the model: what she knows of his company (she works there, or has heard of it), and her own company.
FString AOKStreetSocialScene::CompanyPromptLine(int32 Girl) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return FString();
 const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);FString Out;const auto& Co=S.Store.Company;
 if(Co.Founded>=0){
  const bool Works=Co.Staff.ContainsByPredicate([&](const FOKStreetEmployee& E){return E.Heroine==Id;});
  const bool Heard=S.Store.Rumors.ContainsByPredicate([&](const FOKStreetRumor& R){return R.Kind==TEXT("wealth")&&R.KnownBy.Contains(Id);});
  if(Works||Heard){const FOKStreetHolding* Hq=Co.Estate.Hq();
   Out+=OKMsg(TEXT("ui.company.his_company_summary"), {{TEXT("company_name"), *Co.Name}, {TEXT("estate_count"), Co.Estate.Holdings.Num()}, {TEXT("staff_count"), Co.Estate.Workers.Num()+Co.Staff.Num()}, {TEXT("hq_part"), Hq?*OKMsg(TEXT("ui.company.hq_location_suffix"), {{TEXT("hq_unit_name"), *UnitOf(Hq->Unit).Name}}):TEXT("")}, {TEXT("works_part"), Works?*OKMsg(TEXT("ui.company.last_month_pnl_work_note"), {{TEXT("last_profit"), Co.LastProfit}}):TEXT("")}});}
 }
 for(const auto& H:S.Store.HeroCompanies)if(H.Owner==Id){const FOKStreetHolding* Hq=H.Estate.Hq();
  Out+=OKMsg(TEXT("ui.company.her_company_summary"), {{TEXT("company_name"), *H.Name}, {TEXT("location"), Hq?*UnitOf(Hq->Unit).Name:OKT(TEXT("ui.company.no_estate_label"))}, {TEXT("staff_count"), H.Estate.Workers.Num()}, {TEXT("last_profit"), H.LastProfit}, {TEXT("owner_work_part"), S.Store.PlayerJobs.Contains(TEXT("hc_")+Id)?OKT(TEXT("ui.company.works_at_your_company")):TEXT("")}, {TEXT("rented_part"), Hq&&Hq->Landlord==TEXT("player")?OKT(TEXT("ui.company.rented_from_him_note")):TEXT("")}});}
 return Out;
}

// Her companies change (opened, moved, closed, the offer's pay): his job list follows.
void AOKStreetSocialScene::TickCompany(){
 auto& S=*State;
 FString Sig;for(const auto& H:S.Store.HeroCompanies){const FOKStreetHolding* Hq=H.Estate.Hq();Sig+=H.Owner+(Hq?Hq->Unit:FString())+FString::Printf(TEXT("%lld|"),H.OfferWage);}
 if(Sig!=S.HeroJobSig){S.HeroJobSig=Sig;SyncHeroJobs();}
 if(S.Store.Company.Founded<0)return;const FString Here=PlaceHere(S.Male);if(Here.IsEmpty())return;
 for(const auto& H:S.Store.Company.Estate.Holdings)if(HoldingPlace(H)==Here)S.CompanyVisited.Add(H.Unit);
}
