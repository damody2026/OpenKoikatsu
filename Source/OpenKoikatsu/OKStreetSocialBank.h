#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// His bank (Docs/STREET_SOCIAL_FINANCE_IMPROVEMENT_PLAN.md stage 1): interest on savings, time deposits, a credit card,
// a student loan, fixed bills, and the daily net-worth history. The record; the rules (bank.rs) change it.

// Rates, fees, limits, the scholarship table and the father's gift are game rules (Tools/ok-rules bank.rs); this header is
// the record and its save codec.

// ---------------------------------------------------------------------------------------------------- state

struct FOKStreetDeposit {
 int32 Id = 0;
 int64 Amount = 0;
 int32 Months = 12, StartDay = 0; // game day
 float Rate = 0.f;                // % a year, fixed at the start
};

struct FOKStreetCard {
 bool Issued = false, Stopped = false;
 int64 Limit = 0;     // set by the rules when the card is issued
 int64 Balance = 0;   // everything owed on the card
 int64 Statement = 0; // what the last statement asked for and is still unpaid
 int64 MinDue = 0;
 int32 DueDay = -1;   // game day of the payment deadline (-1: nothing due)
 int32 Late = 0;
 double Interest = 0; // accrued, below one yuan
};

struct FOKStreetLoan {
 int64 Owed = 0;
 FString Year;        // academic year of the last disbursement ("2010")
 int64 TakenThisYear = 0;
 int32 RepayFrom = -1; // game day repayments start (-1: still studying)
 int64 Monthly = 0;
 double Interest = 0;
};

struct FOKStreetBanking {
 TArray<FOKStreetDeposit> Deposits;
 int32 NextDeposit = 1;
 double DemandInterest = 0; // accrued since the last quarterly payment
 FOKStreetCard Card;
 FOKStreetLoan Loan;
 int64 PhoneArrears = 0;
 TArray<FString> Notes;              // recent bank messages (newest last)
 TArray<TPair<int32, int64>> NetWorth; // (game day, total) at each day's end
 // The father he never met: when he goes broke, a text from an unknown number and a transfer (OKStreetSocialBank.inl).
 TArray<TPair<int64, FString>> FatherTexts; // (game minute, text)
 int32 FatherSeen = 0, FatherGifts = 0, FatherDay = -1;
 int64 FatherTotal = 0;
 // Bankrupt: liquidated and waiting for the morning; what was still owed; game minute the father acts.
 bool Bankrupt = false;
 int32 BankruptCount = 0;
 int64 BankruptShortfall = 0, RescueAt = -1;
};

// Phone thread id of the unknown number (heroines are 1..N).
constexpr int32 OKStreetFatherThread = -2;

inline int64 OKStreetDepositsTotal(const FOKStreetBanking& B) { int64 T = 0; for (const auto& D : B.Deposits) T += D.Amount; return T; }

// ---------------------------------------------------------------------------------------------------- JSON

inline TSharedRef<FJsonObject> OKStreetBankingJson(const FOKStreetBanking& B) {
 auto O = MakeShared<FJsonObject>();
 TArray<TSharedPtr<FJsonValue>> Deps;
 for (const auto& D : B.Deposits) {
  auto X = MakeShared<FJsonObject>();
  X->SetNumberField(TEXT("id"), D.Id); X->SetNumberField(TEXT("amount"), double(D.Amount)); X->SetNumberField(TEXT("months"), D.Months);
  X->SetNumberField(TEXT("start"), D.StartDay); X->SetNumberField(TEXT("rate"), D.Rate);
  Deps.Add(MakeShared<FJsonValueObject>(X));
 }
 O->SetArrayField(TEXT("deposits"), Deps);
 O->SetNumberField(TEXT("next_deposit"), B.NextDeposit); O->SetNumberField(TEXT("demand_interest"), B.DemandInterest);
 auto C = MakeShared<FJsonObject>();
 C->SetBoolField(TEXT("issued"), B.Card.Issued); C->SetBoolField(TEXT("stopped"), B.Card.Stopped); C->SetNumberField(TEXT("limit"), double(B.Card.Limit));
 C->SetNumberField(TEXT("balance"), double(B.Card.Balance)); C->SetNumberField(TEXT("statement"), double(B.Card.Statement)); C->SetNumberField(TEXT("min_due"), double(B.Card.MinDue));
 C->SetNumberField(TEXT("due_day"), B.Card.DueDay); C->SetNumberField(TEXT("late"), B.Card.Late); C->SetNumberField(TEXT("interest"), B.Card.Interest);
 O->SetObjectField(TEXT("card"), C);
 auto L = MakeShared<FJsonObject>();
 L->SetNumberField(TEXT("owed"), double(B.Loan.Owed)); L->SetStringField(TEXT("year"), B.Loan.Year); L->SetNumberField(TEXT("taken_this_year"), double(B.Loan.TakenThisYear));
 L->SetNumberField(TEXT("repay_from"), B.Loan.RepayFrom); L->SetNumberField(TEXT("monthly"), double(B.Loan.Monthly)); L->SetNumberField(TEXT("interest"), B.Loan.Interest);
 O->SetObjectField(TEXT("loan"), L);
 O->SetNumberField(TEXT("phone_arrears"), double(B.PhoneArrears));
 TArray<TSharedPtr<FJsonValue>> Notes; for (const auto& N : B.Notes) Notes.Add(MakeShared<FJsonValueString>(N));
 O->SetArrayField(TEXT("notes"), Notes);
 TArray<TSharedPtr<FJsonValue>> Worth;
 for (const auto& W : B.NetWorth) { TArray<TSharedPtr<FJsonValue>> P = {MakeShared<FJsonValueNumber>(W.Key), MakeShared<FJsonValueNumber>(double(W.Value))}; Worth.Add(MakeShared<FJsonValueArray>(P)); }
 O->SetArrayField(TEXT("net_worth"), Worth);
 TArray<TSharedPtr<FJsonValue>> Texts;
 for (const auto& T : B.FatherTexts) { auto X = MakeShared<FJsonObject>(); X->SetNumberField(TEXT("minute"), double(T.Key)); X->SetStringField(TEXT("text"), T.Value); Texts.Add(MakeShared<FJsonValueObject>(X)); }
 auto F = MakeShared<FJsonObject>(); F->SetArrayField(TEXT("texts"), Texts); F->SetNumberField(TEXT("seen"), B.FatherSeen); F->SetNumberField(TEXT("gifts"), B.FatherGifts);
 F->SetNumberField(TEXT("day"), B.FatherDay); F->SetNumberField(TEXT("total"), double(B.FatherTotal));
 F->SetBoolField(TEXT("bankrupt"), B.Bankrupt); F->SetNumberField(TEXT("bankrupt_count"), B.BankruptCount); F->SetNumberField(TEXT("shortfall"), double(B.BankruptShortfall)); F->SetNumberField(TEXT("rescue_at"), double(B.RescueAt));
 O->SetObjectField(TEXT("father"), F);
 return O;
}

inline void OKStreetReadBanking(const TSharedPtr<FJsonObject>& O, FOKStreetBanking& B) {
 if (!O) return;
 auto Num = [](const TSharedPtr<FJsonObject>& X, const TCHAR* K, double Def) { double V = Def; if (X) X->TryGetNumberField(K, V); return FMath::IsFinite(V) ? V : Def; };
 const TArray<TSharedPtr<FJsonValue>>* Deps = nullptr;
 if (O->TryGetArrayField(TEXT("deposits"), Deps)) for (const auto& V : *Deps) {
  const auto X = V.IsValid() ? V->AsObject() : nullptr; if (!X) continue;
  FOKStreetDeposit D; D.Id = int32(Num(X, TEXT("id"), 0)); D.Amount = FMath::Max<int64>(0, int64(Num(X, TEXT("amount"), 0)));
  D.Months = int32(Num(X, TEXT("months"), 12)); D.StartDay = int32(Num(X, TEXT("start"), 0)); D.Rate = float(Num(X, TEXT("rate"), 0));
  if (D.Amount > 0) B.Deposits.Add(D);
 }
 B.NextDeposit = int32(Num(O, TEXT("next_deposit"), 1)); B.DemandInterest = Num(O, TEXT("demand_interest"), 0);
 const TSharedPtr<FJsonObject>* C = nullptr;
 if (O->TryGetObjectField(TEXT("card"), C)) {
  (*C)->TryGetBoolField(TEXT("issued"), B.Card.Issued); (*C)->TryGetBoolField(TEXT("stopped"), B.Card.Stopped);
  B.Card.Limit = int64(Num(*C, TEXT("limit"), 0)); B.Card.Balance = int64(Num(*C, TEXT("balance"), 0)); B.Card.Statement = int64(Num(*C, TEXT("statement"), 0));
  B.Card.MinDue = int64(Num(*C, TEXT("min_due"), 0)); B.Card.DueDay = int32(Num(*C, TEXT("due_day"), -1)); B.Card.Late = int32(Num(*C, TEXT("late"), 0)); B.Card.Interest = Num(*C, TEXT("interest"), 0);
 }
 const TSharedPtr<FJsonObject>* L = nullptr;
 if (O->TryGetObjectField(TEXT("loan"), L)) {
  B.Loan.Owed = int64(Num(*L, TEXT("owed"), 0)); (*L)->TryGetStringField(TEXT("year"), B.Loan.Year); B.Loan.TakenThisYear = int64(Num(*L, TEXT("taken_this_year"), 0));
  B.Loan.RepayFrom = int32(Num(*L, TEXT("repay_from"), -1)); B.Loan.Monthly = int64(Num(*L, TEXT("monthly"), 0)); B.Loan.Interest = Num(*L, TEXT("interest"), 0);
 }
 B.PhoneArrears = FMath::Max<int64>(0, int64(Num(O, TEXT("phone_arrears"), 0)));
 const TArray<TSharedPtr<FJsonValue>>* Notes = nullptr;
 if (O->TryGetArrayField(TEXT("notes"), Notes)) for (const auto& V : *Notes) { FString S; if (V.IsValid() && V->TryGetString(S)) B.Notes.Add(S); }
 const TArray<TSharedPtr<FJsonValue>>* Worth = nullptr;
 if (O->TryGetArrayField(TEXT("net_worth"), Worth)) for (const auto& V : *Worth) {
  const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
  if (V.IsValid() && V->TryGetArray(P) && P->Num() == 2) B.NetWorth.Add(TPair<int32, int64>(int32((*P)[0]->AsNumber()), int64((*P)[1]->AsNumber())));
 }
 const TSharedPtr<FJsonObject>* F = nullptr;
 if (O->TryGetObjectField(TEXT("father"), F)) {
  const TArray<TSharedPtr<FJsonValue>>* Texts = nullptr;
  if ((*F)->TryGetArrayField(TEXT("texts"), Texts)) for (const auto& V : *Texts) {
   const auto X = V.IsValid() ? V->AsObject() : nullptr; FString T; if (X && X->TryGetStringField(TEXT("text"), T)) B.FatherTexts.Add(TPair<int64, FString>(int64(Num(X, TEXT("minute"), 0)), T));
  }
  B.FatherSeen = int32(Num(*F, TEXT("seen"), 0)); B.FatherGifts = int32(Num(*F, TEXT("gifts"), 0)); B.FatherDay = int32(Num(*F, TEXT("day"), -1)); B.FatherTotal = int64(Num(*F, TEXT("total"), 0));
  (*F)->TryGetBoolField(TEXT("bankrupt"), B.Bankrupt); B.BankruptCount = int32(Num(*F, TEXT("bankrupt_count"), 0)); B.BankruptShortfall = int64(Num(*F, TEXT("shortfall"), 0)); B.RescueAt = int64(Num(*F, TEXT("rescue_at"), -1));
 }
}
