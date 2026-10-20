#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// His company's property and people (Docs/STREET_COMPANY_PLAN_20261012.md): what can be rented or bought (units in
// existing buildings and vacant lots), what can be built on a lot, the roles one can hire for, and the economy's
// numbers — all from Content/IndependentResources/StreetSocialData/property.json, so a wiped Saved/ loses nothing.
// The company's own holdings, staff, applicants, mortgages and ledger are saved with the store (FOKStreetEstate).

struct FOKStreetUnitDef {
 FString Id, Name, Place, Kind; // Place: the building it is in ("" for a lot); Kind: office | shop | cafe | land
 int32 Floor = 1, Capacity = 4;
 float Area = 40.f, Traffic = 0.5f;
 int64 Rent = 0, Price = 0;      // a month's rent; the base price to buy (0: not for sale)
 bool bLot = false;
 FVector2D Center = FVector2D::ZeroVector, Size = FVector2D(1200, 1000); // lots: world centre and size (cm)
 float Yaw = 0.f;                // lots: the street side the building faces
};
struct FOKStreetBuildingDef { FString Id, Name, Kind; int64 Cost = 0; int32 Days = 10, Capacity = 6, Floors = 1; };
struct FOKStreetRoleDef { FString Id, Name; float Weight = 1.f; int64 BasePay = 2400; };

struct FOKStreetPropertyData {
 TArray<FOKStreetUnitDef> Units;
 TArray<FOKStreetBuildingDef> Buildings;
 TArray<FOKStreetRoleDef> Roles;
 TArray<FString> Family, Given;
 float DepositMonths = 2.f, LandGrowth = 0.004f, PropertyTax = 0.012f, Resale = 0.92f, Closing = 0.03f, SeveranceMonths = 1.f;
 float MortgageDown = 0.3f, MortgageRate = 0.05f, ForeclosureRatio = 0.85f, BuildingResale = 0.7f;
 int32 MortgageMonths = 240;
 // Mainland China payroll and business taxes ("china" in property.json).
 TMap<FString, float> EmployeeRates, EmployerRates;
 float BaseMin = 1500.f, BaseMax = 30000.f, IitThreshold = 5000.f, VatSmall = 0.01f, CitSmall = 0.05f, CitRate = 0.25f, CitSmallLimit = 3000000.f;
 TArray<FVector> IitBrackets; // X: upper bound of yearly taxable income, Y: rate, Z: quick deduction
 FString Error;
 const FOKStreetUnitDef* Unit(const FString& Id) const { return Units.FindByPredicate([&](const FOKStreetUnitDef& U) { return U.Id == Id; }); }
 const FOKStreetBuildingDef* Building(const FString& Id) const { return Buildings.FindByPredicate([&](const FOKStreetBuildingDef& B) { return B.Id == Id; }); }
 const FOKStreetRoleDef* Role(const FString& Id) const { return Roles.FindByPredicate([&](const FOKStreetRoleDef& R) { return R.Id == Id; }); }
};

inline FString OKStreetPropertyPath() {
 const FString Content = FPaths::ProjectContentDir() / TEXT("IndependentResources/StreetSocialData/property.json");
 if (FPaths::FileExists(Content)) return Content;
 const FString Builtin = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BUILTIN_RESOURCES"));
 if (!Builtin.IsEmpty() && FPaths::FileExists(FPaths::GetPath(Builtin) / TEXT("StreetSocialData/property.json"))) return FPaths::GetPath(Builtin) / TEXT("StreetSocialData/property.json");
 return FPaths::ProjectDir() / TEXT("Content/IndependentResources/StreetSocialData/property.json");
}

// property.json read into the scene's shape: the file as written (base money, Traditional Chinese names) when the rules
// are not up yet, else the rules' copy in the save's region and the player's language (ok-rules property_data).
inline FOKStreetPropertyData OKStreetParseProperty(const TSharedPtr<FJsonObject>& Root) {
  FOKStreetPropertyData D;
  if (!Root.IsValid()) { D.Error = TEXT("property data missing"); return D; }
  auto Num = [](const TSharedPtr<FJsonObject>& O, const TCHAR* F, double Def) { double V = Def; O->TryGetNumberField(F, V); return V; };
  auto Vec = [](const TSharedPtr<FJsonObject>& O, const TCHAR* F, FVector2D Def) { const TArray<TSharedPtr<FJsonValue>>* A = nullptr; if (O->TryGetArrayField(F, A) && A->Num() == 2) return FVector2D((*A)[0]->AsNumber(), (*A)[1]->AsNumber()); return Def; };
  auto ReadUnits = [&](const TCHAR* Field, bool bLot) {
   const TArray<TSharedPtr<FJsonValue>>* A = nullptr; if (!Root->TryGetArrayField(Field, A)) return;
   for (const auto& V : *A) { const auto O = V->AsObject(); if (!O) continue; FOKStreetUnitDef U; U.bLot = bLot;
    O->TryGetStringField(TEXT("id"), U.Id); O->TryGetStringField(TEXT("name"), U.Name); O->TryGetStringField(TEXT("place"), U.Place); O->TryGetStringField(TEXT("kind"), U.Kind);
    if (bLot && U.Kind.IsEmpty()) U.Kind = TEXT("land");
    U.Floor = int32(Num(O, TEXT("floor"), 1)); U.Capacity = int32(Num(O, TEXT("capacity"), bLot ? 0 : 4)); U.Area = float(Num(O, TEXT("area"), 40)); U.Traffic = float(Num(O, TEXT("traffic"), 0.5));
    U.Rent = int64(Num(O, TEXT("rent"), 0)); U.Price = int64(Num(O, TEXT("price"), 0)); U.Center = Vec(O, TEXT("center"), FVector2D::ZeroVector); U.Size = Vec(O, TEXT("size"), FVector2D(1200, 1000)); U.Yaw = float(Num(O, TEXT("yaw"), 0));
    if (!U.Id.IsEmpty()) D.Units.Add(U); }
  };
  ReadUnits(TEXT("units"), false); ReadUnits(TEXT("lots"), true);
  const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
  if (Root->TryGetArrayField(TEXT("buildings"), A)) for (const auto& V : *A) if (const auto O = V->AsObject()) { FOKStreetBuildingDef B; O->TryGetStringField(TEXT("id"), B.Id); O->TryGetStringField(TEXT("name"), B.Name); O->TryGetStringField(TEXT("kind"), B.Kind);
   B.Cost = int64(Num(O, TEXT("cost"), 100000)); B.Days = int32(Num(O, TEXT("days"), 10)); B.Capacity = int32(Num(O, TEXT("capacity"), 6)); B.Floors = int32(Num(O, TEXT("floors"), 1)); if (!B.Id.IsEmpty()) D.Buildings.Add(B); }
  if (Root->TryGetArrayField(TEXT("roles"), A)) for (const auto& V : *A) if (const auto O = V->AsObject()) { FOKStreetRoleDef R; O->TryGetStringField(TEXT("id"), R.Id); O->TryGetStringField(TEXT("name"), R.Name);
   R.Weight = float(Num(O, TEXT("weight"), 1)); R.BasePay = int64(Num(O, TEXT("base_pay"), 2400)); if (!R.Id.IsEmpty()) D.Roles.Add(R); }
  const TSharedPtr<FJsonObject>* Names = nullptr;
  if (Root->TryGetObjectField(TEXT("names"), Names)) { (*Names)->TryGetStringArrayField(TEXT("family"), D.Family); (*Names)->TryGetStringArrayField(TEXT("given"), D.Given); }
  const TSharedPtr<FJsonObject>* E = nullptr;
  if (Root->TryGetObjectField(TEXT("economy"), E)) {
   D.DepositMonths = float(Num(*E, TEXT("deposit_months"), 2)); D.LandGrowth = float(Num(*E, TEXT("land_growth_monthly"), 0.004)); D.PropertyTax = float(Num(*E, TEXT("property_tax_yearly"), 0.012));
   D.Resale = float(Num(*E, TEXT("resale_ratio"), 0.92)); D.Closing = float(Num(*E, TEXT("closing_cost"), 0.03)); D.SeveranceMonths = float(Num(*E, TEXT("severance_months"), 1));
   D.MortgageDown = float(Num(*E, TEXT("mortgage_down"), 0.3)); D.MortgageRate = float(Num(*E, TEXT("mortgage_rate"), 0.05)); D.MortgageMonths = int32(Num(*E, TEXT("mortgage_months"), 240));
   D.ForeclosureRatio = float(Num(*E, TEXT("foreclosure_ratio"), 0.85)); D.BuildingResale = float(Num(*E, TEXT("building_resale"), 0.7));
  }
  const TSharedPtr<FJsonObject>* Cn = nullptr;
  if (Root->TryGetObjectField(TEXT("china"), Cn)) {
   auto Rates = [&](const TCHAR* F, TMap<FString, float>& Out) { const TSharedPtr<FJsonObject>* R = nullptr; if ((*Cn)->TryGetObjectField(F, R)) for (const auto& V : (*R)->Values) Out.Add(FString(*V.Key), float(V.Value->AsNumber())); };
   Rates(TEXT("employee"), D.EmployeeRates); Rates(TEXT("employer"), D.EmployerRates);
   D.BaseMin = float(Num(*Cn, TEXT("base_min"), 1500)); D.BaseMax = float(Num(*Cn, TEXT("base_max"), 30000)); D.IitThreshold = float(Num(*Cn, TEXT("iit_threshold_monthly"), 5000));
   D.VatSmall = float(Num(*Cn, TEXT("vat_small"), 0.01)); D.CitSmall = float(Num(*Cn, TEXT("cit_small_rate"), 0.05)); D.CitRate = float(Num(*Cn, TEXT("cit_rate"), 0.25)); D.CitSmallLimit = float(Num(*Cn, TEXT("cit_small_limit_yearly"), 3000000));
   const TArray<TSharedPtr<FJsonValue>>* B = nullptr;
   if ((*Cn)->TryGetArrayField(TEXT("iit_brackets_yearly"), B)) for (const auto& V : *B) { const auto& Row = V->AsArray(); if (Row.Num() == 3) D.IitBrackets.Add(FVector(Row[0]->AsNumber(), Row[1]->AsNumber(), Row[2]->AsNumber())); }
  }
  return D;
}

inline FOKStreetPropertyData& OKStreetPropertyStore() {
 static FOKStreetPropertyData Data = [] {
  FString Text; TSharedPtr<FJsonObject> Root;
  if (!FFileHelper::LoadFileToString(Text, *OKStreetPropertyPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) {
   FOKStreetPropertyData D; D.Error = TEXT("property.json missing or invalid: ") + OKStreetPropertyPath(); return D; }
  return OKStreetParseProperty(Root);
 }();
 return Data;
}

inline const FOKStreetPropertyData& OKStreetProperty() { return OKStreetPropertyStore(); }

// The rules' copy for the save's region and the player's language (amounts and names converted there).
inline void OKStreetSetProperty(const TSharedPtr<FJsonObject>& Root) {
 FOKStreetPropertyData D = OKStreetParseProperty(Root);
 if (D.Error.IsEmpty() && D.Units.Num()) OKStreetPropertyStore() = MoveTemp(D);
}

// What the company holds (a unit or a lot), one employee (no heroines: they stay FOKStreetEmployee), one applicant,
// one ledger row.
struct FOKStreetHolding {
 FString Unit, Mode = TEXT("rent"), Building, Use; // Mode: rent | own | borrow; Use: office | shop | cafe
 FString Landlord, Tenant; // Landlord: who it is rented from ("" the market, "player", a heroine id); Tenant: who rents his place
 int64 LeaseRent = 0;      // his own place offered to others at this rent (0: not offered)
 int32 Since = 0, BuildStart = -1, BuildDone = -1, MissedRent = 0, MissedLoan = 0, LeaveMonth = -1;
 int64 Rent = 0, Deposit = 0, Bought = 0, BuildPaid = 0, Loan = 0, LoanPayment = 0;
 bool bHq = false;
};
struct FOKStreetWorker {
 FString Id, Name, Role, Holding;
 int32 Skill = 50, Diligence = 50, Loyalty = 50, Since = 0, Missed = 0, Week = 0;
 float Morale = 70.f;
 int64 Pay = 0, Ask = 0;
};
struct FOKStreetLedgerRow { int32 Day = 0; FString Kind, Note; int64 Amount = 0; };
struct FOKStreetEstate {
 TArray<FOKStreetHolding> Holdings;
 TArray<FOKStreetWorker> Workers, Applicants;
 TArray<FOKStreetLedgerRow> Ledger;
 TArray<FString> Refused; // applicant ids who said no this week
 int32 ApplicantWeek = -1, NextWorker = 1, Debt = 0;
 int32 TaxYear = 0; int64 LossCarry = 0, YtdProfit = 0; // the company's tax year: losses carried forward, taxable profit so far
 // The company's own money (Docs/STREET_ACCOUNTING_PLAN_20261012.md): its bank account, what it owes its owner, the
 // paid-in capital, profit kept after tax (what dividends may come from), the owner's monthly pay, and whether a
 // shortfall is lent by the owner on its own.
 int64 Cash = 0, OwnerLoan = 0, Capital = 0, Retained = 0, OwnerPay = 0;
 bool bAutoFund = true, bHadCash = false; // bHadCash: read from a save that knew the company account
 float Reputation = 50.f;
 const FOKStreetHolding* Hq() const { return Holdings.FindByPredicate([](const FOKStreetHolding& H) { return H.bHq; }); }
};

inline TSharedRef<FJsonObject> OKStreetWriteEstate(const FOKStreetEstate& E) {
 auto Root = MakeShared<FJsonObject>(); auto I64 = [](int64 V) { return FString::Printf(TEXT("%lld"), V); };
 TArray<TSharedPtr<FJsonValue>> Holdings, Workers, Applicants, Ledger, Refused;
 for (const auto& H : E.Holdings) { auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("unit"), H.Unit); O->SetStringField(TEXT("mode"), H.Mode); O->SetStringField(TEXT("building"), H.Building); O->SetStringField(TEXT("use"), H.Use);
  O->SetNumberField(TEXT("since"), H.Since); O->SetNumberField(TEXT("build_start"), H.BuildStart); O->SetNumberField(TEXT("build_done"), H.BuildDone); O->SetNumberField(TEXT("missed_rent"), H.MissedRent); O->SetNumberField(TEXT("missed_loan"), H.MissedLoan); O->SetNumberField(TEXT("leave_month"), H.LeaveMonth);
  O->SetStringField(TEXT("rent"), I64(H.Rent)); O->SetStringField(TEXT("deposit"), I64(H.Deposit)); O->SetStringField(TEXT("bought"), I64(H.Bought)); O->SetStringField(TEXT("build_paid"), I64(H.BuildPaid)); O->SetStringField(TEXT("loan"), I64(H.Loan)); O->SetStringField(TEXT("loan_payment"), I64(H.LoanPayment));
  O->SetBoolField(TEXT("hq"), H.bHq); O->SetStringField(TEXT("landlord"), H.Landlord); O->SetStringField(TEXT("tenant"), H.Tenant); O->SetStringField(TEXT("lease_rent"), I64(H.LeaseRent)); Holdings.Add(MakeShared<FJsonValueObject>(O)); }
 auto Worker = [&](const FOKStreetWorker& W) { auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), W.Id); O->SetStringField(TEXT("name"), W.Name); O->SetStringField(TEXT("role"), W.Role); O->SetStringField(TEXT("holding"), W.Holding);
  O->SetNumberField(TEXT("skill"), W.Skill); O->SetNumberField(TEXT("diligence"), W.Diligence); O->SetNumberField(TEXT("loyalty"), W.Loyalty); O->SetNumberField(TEXT("since"), W.Since); O->SetNumberField(TEXT("missed"), W.Missed); O->SetNumberField(TEXT("week"), W.Week);
  O->SetNumberField(TEXT("morale"), W.Morale); O->SetStringField(TEXT("pay"), I64(W.Pay)); O->SetStringField(TEXT("ask"), I64(W.Ask)); return MakeShared<FJsonValueObject>(O); };
 for (const auto& W : E.Workers) Workers.Add(Worker(W));
 for (const auto& W : E.Applicants) Applicants.Add(Worker(W));
 for (const auto& L : E.Ledger) { auto O = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("day"), L.Day); O->SetStringField(TEXT("kind"), L.Kind); O->SetStringField(TEXT("note"), L.Note); O->SetStringField(TEXT("amount"), I64(L.Amount)); Ledger.Add(MakeShared<FJsonValueObject>(O)); }
 for (const FString& R : E.Refused) Refused.Add(MakeShared<FJsonValueString>(R));
 Root->SetArrayField(TEXT("holdings"), Holdings); Root->SetArrayField(TEXT("workers"), Workers); Root->SetArrayField(TEXT("applicants"), Applicants); Root->SetArrayField(TEXT("ledger"), Ledger); Root->SetArrayField(TEXT("refused"), Refused);
 Root->SetNumberField(TEXT("tax_year"), E.TaxYear); Root->SetStringField(TEXT("loss_carry"), I64(E.LossCarry)); Root->SetStringField(TEXT("ytd_profit"), I64(E.YtdProfit));
 Root->SetStringField(TEXT("cash"), I64(E.Cash)); Root->SetStringField(TEXT("owner_loan"), I64(E.OwnerLoan)); Root->SetStringField(TEXT("capital"), I64(E.Capital)); Root->SetStringField(TEXT("retained"), I64(E.Retained));
 Root->SetStringField(TEXT("owner_pay"), I64(E.OwnerPay)); Root->SetBoolField(TEXT("auto_fund"), E.bAutoFund);
 Root->SetNumberField(TEXT("applicant_week"), E.ApplicantWeek); Root->SetNumberField(TEXT("next_worker"), E.NextWorker); Root->SetNumberField(TEXT("reputation"), E.Reputation); Root->SetNumberField(TEXT("debt"), E.Debt);
 return Root;
}

inline void OKStreetReadEstate(const TSharedPtr<FJsonObject>& Root, FOKStreetEstate& E) {
 if (!Root.IsValid()) return;
 auto I64 = [](const TSharedPtr<FJsonObject>& O, const TCHAR* F) { FString S; double V = 0; if (O->TryGetStringField(F, S)) return FCString::Atoi64(*S); if (O->TryGetNumberField(F, V)) return int64(V); return int64(0); };
 auto I32 = [](const TSharedPtr<FJsonObject>& O, const TCHAR* F, int32 Def) { double V = Def; O->TryGetNumberField(F, V); return int32(V); };
 const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
 if (Root->TryGetArrayField(TEXT("holdings"), A)) for (const auto& V : *A) if (const auto O = V->AsObject()) { FOKStreetHolding H;
  O->TryGetStringField(TEXT("unit"), H.Unit); O->TryGetStringField(TEXT("mode"), H.Mode); O->TryGetStringField(TEXT("building"), H.Building); O->TryGetStringField(TEXT("use"), H.Use);
  H.Since = I32(O, TEXT("since"), 0); H.BuildStart = I32(O, TEXT("build_start"), -1); H.BuildDone = I32(O, TEXT("build_done"), -1); H.MissedRent = I32(O, TEXT("missed_rent"), 0); H.MissedLoan = I32(O, TEXT("missed_loan"), 0); H.LeaveMonth = I32(O, TEXT("leave_month"), -1);
  H.Rent = I64(O, TEXT("rent")); H.Deposit = I64(O, TEXT("deposit")); H.Bought = I64(O, TEXT("bought")); H.BuildPaid = I64(O, TEXT("build_paid")); H.Loan = I64(O, TEXT("loan")); H.LoanPayment = I64(O, TEXT("loan_payment"));
  O->TryGetBoolField(TEXT("hq"), H.bHq); O->TryGetStringField(TEXT("landlord"), H.Landlord); O->TryGetStringField(TEXT("tenant"), H.Tenant); H.LeaseRent = I64(O, TEXT("lease_rent")); if (!H.Unit.IsEmpty()) E.Holdings.Add(H); }
 auto Worker = [&](const TSharedPtr<FJsonObject>& O) { FOKStreetWorker W; O->TryGetStringField(TEXT("id"), W.Id); O->TryGetStringField(TEXT("name"), W.Name); O->TryGetStringField(TEXT("role"), W.Role); O->TryGetStringField(TEXT("holding"), W.Holding);
  W.Skill = I32(O, TEXT("skill"), 50); W.Diligence = I32(O, TEXT("diligence"), 50); W.Loyalty = I32(O, TEXT("loyalty"), 50); W.Since = I32(O, TEXT("since"), 0); W.Missed = I32(O, TEXT("missed"), 0); W.Week = I32(O, TEXT("week"), 0);
  double M = 70; O->TryGetNumberField(TEXT("morale"), M); W.Morale = float(M); W.Pay = I64(O, TEXT("pay")); W.Ask = I64(O, TEXT("ask")); return W; };
 if (Root->TryGetArrayField(TEXT("workers"), A)) for (const auto& V : *A) if (const auto O = V->AsObject()) E.Workers.Add(Worker(O));
 if (Root->TryGetArrayField(TEXT("applicants"), A)) for (const auto& V : *A) if (const auto O = V->AsObject()) E.Applicants.Add(Worker(O));
 if (Root->TryGetArrayField(TEXT("ledger"), A)) for (const auto& V : *A) if (const auto O = V->AsObject()) { FOKStreetLedgerRow L; L.Day = I32(O, TEXT("day"), 0); O->TryGetStringField(TEXT("kind"), L.Kind); O->TryGetStringField(TEXT("note"), L.Note); L.Amount = I64(O, TEXT("amount")); E.Ledger.Add(L); }
 Root->TryGetStringArrayField(TEXT("refused"), E.Refused);
 E.TaxYear = I32(Root, TEXT("tax_year"), 0); E.LossCarry = I64(Root, TEXT("loss_carry")); E.YtdProfit = I64(Root, TEXT("ytd_profit"));
 E.bHadCash = Root->HasField(TEXT("cash")); E.Cash = I64(Root, TEXT("cash")); E.OwnerLoan = I64(Root, TEXT("owner_loan")); E.Capital = I64(Root, TEXT("capital")); E.Retained = I64(Root, TEXT("retained"));
 E.OwnerPay = I64(Root, TEXT("owner_pay")); Root->TryGetBoolField(TEXT("auto_fund"), E.bAutoFund);
 E.ApplicantWeek = I32(Root, TEXT("applicant_week"), -1); E.NextWorker = I32(Root, TEXT("next_worker"), 1); E.Debt = I32(Root, TEXT("debt"), 0);
 double R = 50; Root->TryGetNumberField(TEXT("reputation"), R); E.Reputation = float(R);
}

// Payroll and business taxes, land prices and mortgage payments are game rules (Tools/ok-rules property.rs, estate.rs);
// this header reads the market for the screens and keeps the records' save codec.

// A heroine's own company (Docs/STREET_HEROINE_COMPANY_PLAN_20261012.md): the same estate as his, run by her rules.
struct FOKStreetHeroCompany {
 FString Owner, Name, Kind;            // Owner: her stable id; Kind: office | shop | cafe
 int32 Founded = 0, PaidMonth = -1, ProfitStreak = 0, LossStreak = 0, OfferUntil = -1, OfferWeek = -1;
 int64 Capital = 0, LastProfit = 0, OfferWage = 0, PlayerMinutes = 0;
 FOKStreetEstate Estate;
};

inline TSharedRef<FJsonObject> OKStreetWriteHeroCompany(const FOKStreetHeroCompany& H) {
 auto O = MakeShared<FJsonObject>(); auto I64 = [](int64 V) { return FString::Printf(TEXT("%lld"), V); };
 O->SetStringField(TEXT("owner"), H.Owner); O->SetStringField(TEXT("name"), H.Name); O->SetStringField(TEXT("kind"), H.Kind);
 O->SetNumberField(TEXT("founded"), H.Founded); O->SetNumberField(TEXT("paid_month"), H.PaidMonth); O->SetNumberField(TEXT("profit_streak"), H.ProfitStreak); O->SetNumberField(TEXT("loss_streak"), H.LossStreak);
 O->SetNumberField(TEXT("offer_until"), H.OfferUntil); O->SetNumberField(TEXT("offer_week"), H.OfferWeek);
 O->SetStringField(TEXT("capital"), I64(H.Capital)); O->SetStringField(TEXT("last_profit"), I64(H.LastProfit)); O->SetStringField(TEXT("offer_wage"), I64(H.OfferWage)); O->SetStringField(TEXT("player_minutes"), I64(H.PlayerMinutes));
 O->SetObjectField(TEXT("estate"), OKStreetWriteEstate(H.Estate));
 return O;
}
inline bool OKStreetReadHeroCompany(const TSharedPtr<FJsonObject>& O, FOKStreetHeroCompany& H) {
 if (!O.IsValid()) return false;
 auto I64 = [&](const TCHAR* F) { FString S; double V = 0; if (O->TryGetStringField(F, S)) return FCString::Atoi64(*S); if (O->TryGetNumberField(F, V)) return int64(V); return int64(0); };
 auto I32 = [&](const TCHAR* F, int32 Def) { double V = Def; O->TryGetNumberField(F, V); return int32(V); };
 O->TryGetStringField(TEXT("owner"), H.Owner); O->TryGetStringField(TEXT("name"), H.Name); O->TryGetStringField(TEXT("kind"), H.Kind);
 H.Founded = I32(TEXT("founded"), 0); H.PaidMonth = I32(TEXT("paid_month"), -1); H.ProfitStreak = I32(TEXT("profit_streak"), 0); H.LossStreak = I32(TEXT("loss_streak"), 0);
 H.OfferUntil = I32(TEXT("offer_until"), -1); H.OfferWeek = I32(TEXT("offer_week"), -1);
 H.Capital = I64(TEXT("capital")); H.LastProfit = I64(TEXT("last_profit")); H.OfferWage = I64(TEXT("offer_wage")); H.PlayerMinutes = I64(TEXT("player_minutes"));
 const TSharedPtr<FJsonObject>* E = nullptr; if (O->TryGetObjectField(TEXT("estate"), E)) OKStreetReadEstate(*E, H.Estate);
 return !H.Owner.IsEmpty();
}
