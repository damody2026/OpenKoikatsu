#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// The books (Docs/STREET_ACCOUNTING_PLAN_20261012.md): every change of money is one entry in a journal that keeps the
// whole game. Books: "private" (his own money), "company" (his company), "hc_<heroine id>" (her company). Pure data
// here; OKStreetSocialBooks.inl posts, files and reports them.

// One change waiting to reach the journal (kept on the store, not saved): the money rules add it where they change
// an account; a store that never takes effect takes its entries with it.
struct FOKBookPost {
 int64 Serial = 0;
 FString Book, Account, Category, Note;
 int64 Amount = 0;
};

// One journal line: when, which account, how much, the account's balance after it, and why.
struct FOKBookEntry {
 int64 Serial = 0, Minute = 0, Amount = 0, Balance = 0;
 int32 Day = 0;
 FString Account, Category, Note;
};

// What a post's serial is: unique and rising for the session (wall clock ticks, never repeated).
inline int64 OKBookNextSerial() { static int64 Last = 0; Last = FMath::Max(Last + 1, FDateTime::UtcNow().GetTicks()); return Last; }

// His own payroll year (all his pay as an employee: part-time jobs, his company's pay and bonuses).
struct FOKStreetPersonal {
 int32 TaxYear = 0, BonusYear = 0; // BonusYear: the tax year whose one-time bonus was used
 int64 YtdIncome = 0, YtdSocial = 0, YtdTax = 0, HousingFund = 0, PensionPaid = 0;
 int64 YtdBonus = 0, YtdBonusTax = 0; // taxed on their own (the one-time bonus, dividends): not in the cumulative withholding
};
inline TSharedRef<FJsonObject> OKStreetWritePersonal(const FOKStreetPersonal& P) {
 auto O = MakeShared<FJsonObject>(); auto I64 = [](int64 V) { return FString::Printf(TEXT("%lld"), V); };
 O->SetNumberField(TEXT("tax_year"), P.TaxYear); O->SetNumberField(TEXT("bonus_year"), P.BonusYear);
 O->SetStringField(TEXT("ytd_income"), I64(P.YtdIncome)); O->SetStringField(TEXT("ytd_social"), I64(P.YtdSocial)); O->SetStringField(TEXT("ytd_tax"), I64(P.YtdTax));
 O->SetStringField(TEXT("housing_fund"), I64(P.HousingFund)); O->SetStringField(TEXT("pension_paid"), I64(P.PensionPaid)); O->SetStringField(TEXT("ytd_bonus"), I64(P.YtdBonus)); O->SetStringField(TEXT("ytd_bonus_tax"), I64(P.YtdBonusTax));
 return O;
}
// Also reads an old save's company estate, which kept these under the same names.
inline void OKStreetReadPersonal(const TSharedPtr<FJsonObject>& O, FOKStreetPersonal& P) {
 if (!O.IsValid()) return;
 auto I64 = [&](const TCHAR* F) { FString S; double V = 0; if (O->TryGetStringField(F, S)) return FCString::Atoi64(*S); if (O->TryGetNumberField(F, V)) return int64(V); return int64(0); };
 double V = 0; if (O->TryGetNumberField(TEXT("tax_year"), V)) P.TaxYear = int32(V); V = 0; if (O->TryGetNumberField(TEXT("bonus_year"), V)) P.BonusYear = int32(V);
 P.YtdIncome = I64(TEXT("ytd_income")); P.YtdSocial = I64(TEXT("ytd_social")); P.YtdTax = I64(TEXT("ytd_tax")); P.HousingFund = I64(TEXT("housing_fund")); P.PensionPaid = I64(TEXT("pension_paid")); P.YtdBonus = I64(TEXT("ytd_bonus")); P.YtdBonusTax = I64(TEXT("ytd_bonus_tax"));
}

// The chart of categories. Kind: pl (income or expense, by sign), transfer (between his own accounts), financing
// (capital, owner loans, dividends, the mortgage's principal), investing (property bought, built, sold), opening, adjust.
struct FOKBookCategory { const TCHAR* Id; const TCHAR* Label; const TCHAR* Kind; };
inline const TArray<FOKBookCategory>& OKBookCategories() {
 static const TArray<FOKBookCategory> T = {
  {TEXT("wage"), TEXT("薪資"), TEXT("pl")}, {TEXT("owner_pay"), TEXT("老闆薪資"), TEXT("pl")}, {TEXT("bonus"), TEXT("獎金"), TEXT("pl")},
  {TEXT("social"), TEXT("五險一金"), TEXT("pl")}, {TEXT("iit"), TEXT("個人所得稅"), TEXT("pl")}, {TEXT("dividend_tax"), TEXT("股息紅利稅"), TEXT("pl")},
  {TEXT("dividend"), TEXT("分紅"), TEXT("financing")}, {TEXT("dividend_in"), TEXT("公司分紅收入"), TEXT("pl")}, {TEXT("scholarship"), TEXT("獎學金"), TEXT("pl")}, {TEXT("allowance"), TEXT("家裡生活費"), TEXT("pl")},
  {TEXT("father"), TEXT("境外匯款"), TEXT("pl")}, {TEXT("interest"), TEXT("利息收入"), TEXT("pl")}, {TEXT("prize"), TEXT("中獎"), TEXT("pl")},
  {TEXT("lottery"), TEXT("彩券"), TEXT("pl")}, {TEXT("food"), TEXT("餐飲"), TEXT("pl")}, {TEXT("treat"), TEXT("請客"), TEXT("pl")},
  {TEXT("shopping"), TEXT("購物"), TEXT("pl")}, {TEXT("tuition"), TEXT("學費宿舍費"), TEXT("pl")}, {TEXT("phone"), TEXT("手機費"), TEXT("pl")},
  {TEXT("card_fee"), TEXT("信用卡利息罰款"), TEXT("pl")}, {TEXT("loan_interest"), TEXT("貸款利息"), TEXT("pl")}, {TEXT("service"), TEXT("其他消費"), TEXT("pl")},
  {TEXT("entrust_share"), TEXT("托付・翻身分成"), TEXT("pl")}, {TEXT("bad_debt"), TEXT("呆帳"), TEXT("pl")}, {TEXT("other"), TEXT("其他"), TEXT("pl")},
  {TEXT("atm"), TEXT("存提款"), TEXT("transfer")}, {TEXT("deposit_term"), TEXT("定存"), TEXT("transfer")}, {TEXT("card_pay"), TEXT("繳信用卡"), TEXT("transfer")},
  {TEXT("loan_pay"), TEXT("還貸款本金"), TEXT("transfer")}, {TEXT("lend"), TEXT("借給女主・收回"), TEXT("transfer")}, {TEXT("entrust"), TEXT("托付・翻身本金"), TEXT("transfer")},
  {TEXT("broker"), TEXT("證券帳戶出入金"), TEXT("transfer")}, {TEXT("liquidated"), TEXT("強制清算"), TEXT("transfer")},
  {TEXT("revenue"), TEXT("營收"), TEXT("pl")}, {TEXT("rent_income"), TEXT("租金收入"), TEXT("pl")}, {TEXT("rent"), TEXT("房租"), TEXT("pl")},
  {TEXT("tax"), TEXT("房產持有稅"), TEXT("pl")}, {TEXT("vat"), TEXT("增值稅"), TEXT("pl")}, {TEXT("cit"), TEXT("企業所得稅"), TEXT("pl")},
  {TEXT("severance"), TEXT("資遣費"), TEXT("pl")}, {TEXT("overhead"), TEXT("雜支"), TEXT("pl")},
  {TEXT("deposit"), TEXT("押金"), TEXT("transfer")}, {TEXT("loan_principal"), TEXT("房貸本金"), TEXT("financing")},
  {TEXT("buy"), TEXT("購置不動產"), TEXT("investing")}, {TEXT("build"), TEXT("工程款"), TEXT("investing")}, {TEXT("sell"), TEXT("出售不動產"), TEXT("investing")},
  {TEXT("foreclosure"), TEXT("法拍"), TEXT("investing")}, {TEXT("auction"), TEXT("拍賣抵債"), TEXT("investing")},
  {TEXT("capital"), TEXT("股東注資"), TEXT("financing")}, {TEXT("owner_loan"), TEXT("股東借款"), TEXT("financing")}, {TEXT("owner_repay"), TEXT("還股東借款"), TEXT("financing")},
  {TEXT("liquidation"), TEXT("公司清算"), TEXT("financing")},
  {TEXT("opening"), TEXT("期初餘額"), TEXT("opening")}, {TEXT("adjust"), TEXT("對帳調整"), TEXT("adjust")}, {TEXT("unclassified"), TEXT("未分類"), TEXT("adjust")},
 };
 return T;
}
inline const FOKBookCategory* OKBookCategory(const FString& Id) { return OKBookCategories().FindByPredicate([&](const FOKBookCategory& C) { return Id == C.Id; }); }
inline FString OKBookCategoryLabel(const FString& Id) { const FOKBookCategory* C = OKBookCategory(Id); return C ? FString(OKT(C->Label)) : Id; }
inline FString OKBookCategoryKind(const FString& Id) { const FOKBookCategory* C = OKBookCategory(Id); return C ? FString(C->Kind) : FString(TEXT("pl")); }

// The accounts of each book. Liabilities are kept as negative balances.
inline FString OKBookAccountLabel(const FString& Book, const FString& Account) {
 static const TMap<FString, FString> Private = {{TEXT("cash"), TEXT("錢包")}, {TEXT("bank"), TEXT("活期存款")}, {TEXT("deposit"), TEXT("定期存款")}, {TEXT("card"), TEXT("信用卡")},
  {TEXT("loan"), TEXT("助學貸款")}, {TEXT("arrears"), TEXT("手機欠費")}, {TEXT("lent"), TEXT("借給女主")}, {TEXT("owner_loan"), TEXT("借給公司")}};
 static const TMap<FString, FString> Company = {{TEXT("cash"), TEXT("公司帳戶")}, {TEXT("owner_loan"), TEXT("股東借款")}};
 const FString* L = (Book == TEXT("private") ? Private : Company).Find(Account); return L ? FString(OKT(**L)) : Account;
}

inline FString OKBookEntryLine(const FOKBookEntry& E) {
 auto O = MakeShared<FJsonObject>(); auto I64 = [](int64 V) { return FString::Printf(TEXT("%lld"), V); };
 O->SetStringField(TEXT("s"), I64(E.Serial)); O->SetStringField(TEXT("m"), I64(E.Minute)); O->SetNumberField(TEXT("d"), E.Day);
 O->SetStringField(TEXT("a"), E.Account); O->SetStringField(TEXT("c"), E.Category); O->SetStringField(TEXT("n"), E.Note);
 O->SetStringField(TEXT("v"), I64(E.Amount)); O->SetStringField(TEXT("b"), I64(E.Balance));
 FString Out; FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out)); return Out;
}
inline bool OKBookEntryParse(const FString& Line, FOKBookEntry& E) {
 TSharedPtr<FJsonObject> O; if (Line.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), O) || !O) return false;
 auto I64 = [&](const TCHAR* F) { FString S; double V = 0; if (O->TryGetStringField(F, S)) return FCString::Atoi64(*S); if (O->TryGetNumberField(F, V)) return int64(V); return int64(0); };
 E.Serial = I64(TEXT("s")); E.Minute = I64(TEXT("m")); E.Day = int32(I64(TEXT("d"))); E.Amount = I64(TEXT("v")); E.Balance = I64(TEXT("b"));
 O->TryGetStringField(TEXT("a"), E.Account); O->TryGetStringField(TEXT("c"), E.Category); O->TryGetStringField(TEXT("n"), E.Note);
 return !E.Account.IsEmpty();
}
