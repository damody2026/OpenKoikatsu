// The heroines' own companies (Docs/STREET_HEROINE_COMPANY_PLAN_20261012.md): a rich, able heroine opens a café, a
// shop or an office from the same market as his, hires, settles the same month, may offer him a job (it becomes one
// of his part-time jobs), may rent his place, and closes when it keeps losing. All of that is the rules' (estate.rs,
// from CompanyDaily); this file keeps who uses what, her wallet for the books, and his job at her company.
// Included after OKStreetSocialCompany.inl (the wallet).

namespace {
const FString OKHeroJobPrefix=TEXT("hc_");
}

FOKStreetHeroCompany* AOKStreetSocialScene::HeroCompanyOf(FOKStreetStore& C,const FString& OwnerId){return C.HeroCompanies.FindByPredicate([&](const FOKStreetHeroCompany& H){return H.Owner==OwnerId;});}

// Who uses a unit now: "player", a heroine's id, or "" (on the market). His place rented to her is hers to use.
FString AOKStreetSocialScene::UnitUser(const FOKStreetStore& C,const FString& UnitId) const{
 for(const auto& H:C.HeroCompanies)if(H.Estate.Holdings.ContainsByPredicate([&](const FOKStreetHolding& X){return X.Unit==UnitId;}))return H.Owner;
 if(C.Company.Estate.Holdings.ContainsByPredicate([&](const FOKStreetHolding& X){return X.Unit==UnitId;}))return TEXT("player");
 return FString();
}

// The holding that decides what stands on a lot: the owner's (with the building), whoever uses it.
const FOKStreetHolding* AOKStreetSocialScene::LotHolding(const FString& UnitId,FString* User) const{
 const auto& C=State->Store;const FOKStreetHolding* Best=nullptr;
 auto Consider=[&](const FOKStreetEstate& E,const FString& Who){if(const FOKStreetHolding* H=E.Holdings.FindByPredicate([&](const FOKStreetHolding& X){return X.Unit==UnitId;})){if(!Best||(H->BuildStart>=0&&Best->BuildStart<0)){Best=H;if(User)*User=Who;}}};
 Consider(C.Company.Estate,TEXT("player"));for(const auto& H:C.HeroCompanies)Consider(H.Estate,H.Owner);
 return Best;
}

FString AOKStreetSocialScene::HeroCompanyPlace(int32 Girl) const{
 if(!State->Manifest.Characters.IsValidIndex(Girl))return FString();const FString Id=OKStreetStableId(State->Manifest.Characters[Girl]);
 for(const auto& H:State->Store.HeroCompanies)if(H.Owner==Id)if(const FOKStreetHolding* Hq=H.Estate.Hq())return HoldingPlace(*Hq);
 return FString();
}
// A heroine who owns the business at this place (she interviews him; ManagerAt).
int32 AOKStreetSocialScene::HeroOwnerAt(const FString& Place) const{
 for(const auto& H:State->Store.HeroCompanies)if(const FOKStreetHolding* Hq=H.Estate.Hq())if(HoldingPlace(*Hq)==Place)return CharacterOf(H.Owner);
 return INDEX_NONE;
}

// Her company's job for him, in the part-time job list (so shifts, pay, rating and being let go work as for any
// job): hourly pay from the offer, weekday shifts at her headquarters.
void AOKStreetSocialScene::SyncHeroJobs(){
 auto& S=*State;S.Catalog.Jobs.RemoveAll([](const FOKStreetCatalogJob& J){return J.Id.StartsWith(OKHeroJobPrefix);});
 for(const auto& H:S.Store.HeroCompanies){const FOKStreetHolding* Hq=H.Estate.Hq();if(!Hq)continue;
  FOKStreetCatalogJob J;J.Id=OKHeroJobPrefix+H.Owner;J.Place=HoldingPlace(*Hq);J.Title=OKMsg(TEXT("ui.hero_company.employee_of"), {{TEXT("hero_name"), *H.Name}});
  J.Wage=int32(FMath::Max<int64>(H.OfferWage,Local(TEXT("salary"),2400)/176));J.Shifts={FVector2D(10,14),FVector2D(14,18)};S.Catalog.Jobs.Add(J);}
 // A job at a company that is gone is gone too.
 auto Keep=[&](const FString& Id){return !Id.StartsWith(OKHeroJobPrefix)||S.Store.HeroCompanies.ContainsByPredicate([&](const FOKStreetHeroCompany& H){return OKHeroJobPrefix+H.Owner==Id;});};
 if(S.Store.PlayerJobs.ContainsByPredicate([&](const FString& Id){return !Keep(Id);})){auto C=S.Store;C.PlayerJobs.RemoveAll([&](const FString& Id){return !Keep(Id);});CommitStore(MoveTemp(C));}
}

FOKCompanyWallet AOKStreetSocialScene::HeroWallet(FOKStreetStore& C,FOKStreetHeroCompany& H){
 FOKCompanyWallet W;W.Store=&C;W.Rel=OKStreetRelationIndex(C,H.Owner,State->Manifest.TalkBudget,true);W.Book=&H.Estate;W.BookId=OKHeroJobPrefix+H.Owner;return W;
}

// ------------------------------------------------------------------------------------------------ him and her company

void AOKStreetSocialScene::HeroOfferAnswer(const FString& OwnerId,bool Accept){
 if(CompanyAct(TEXT("hero_offer_answer"),[&](FJsonObject& A){A.SetStringField(TEXT("owner"),OwnerId);A.SetBoolField(TEXT("accept"),Accept);}))SyncHeroJobs();
}

void AOKStreetSocialScene::HeroJobQuit(const FString& OwnerId){CompanyAct(TEXT("hero_job_quit"),[&](FJsonObject& A){A.SetStringField(TEXT("owner"),OwnerId);});}

// His shift at her company is paid from her company's money (EndShift); she remembers when she could not pay.
void AOKStreetSocialScene::HeroJobPaid(FOKStreetStore& C,const FString& JobId,int64 Pay,int64 Minutes){
 if(!JobId.StartsWith(OKHeroJobPrefix)||!HeroCompanyOf(C,JobId.Mid(OKHeroJobPrefix.Len())))return;
 CompanyActOn(C,TEXT("hero_job_paid"),[&](FJsonObject& A){A.SetStringField(TEXT("job"),JobId);A.SetNumberField(TEXT("pay"),double(Pay));A.SetNumberField(TEXT("minutes"),double(Minutes));});
}
