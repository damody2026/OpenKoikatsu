// His bank's record in the save. Rates, scholarship bands and the father's gift are game rules (Tools/ok-rules bank.rs,
// cargo test spec_rates_scholarship_and_father).
#include "../OKStreetSocialBank.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetBankSaveTest, "OpenKoikatsu.StreetSocial.Bank.Save", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetBankSaveTest::RunTest(const FString&)
{
 FOKStreetBanking B;
 B.Deposits.Add({3, 5000, 12, 40, 2.5f}); B.NextDeposit = 4; B.DemandInterest = 0.37;
 B.Card.Issued = true; B.Card.Balance = 1200; B.Card.Statement = 800; B.Card.MinDue = 80; B.Card.DueDay = 55; B.Card.Late = 1;
 B.Loan.Owed = 3100; B.Loan.Year = TEXT("2010"); B.Loan.TakenThisYear = 3100; B.PhoneArrears = 58;
 B.Notes.Add(TEXT("note")); B.NetWorth.Add(TPair<int32, int64>(12, 2674));
 B.FatherTexts.Add(TPair<int64, FString>(20000, TEXT("我是你爸。"))); B.FatherSeen = 1; B.FatherGifts = 1; B.FatherDay = 13; B.FatherTotal = 100000;
 FOKStreetBanking C; OKStreetReadBanking(OKStreetBankingJson(B), C);
 TestEqual(TEXT("deposit"), C.Deposits.Num(), 1); TestEqual(TEXT("deposit amount"), C.Deposits[0].Amount, int64(5000)); TestEqual(TEXT("deposit rate"), C.Deposits[0].Rate, 2.5f);
 TestEqual(TEXT("next id"), C.NextDeposit, 4); TestTrue(TEXT("card"), C.Card.Issued && C.Card.Balance == 1200 && C.Card.Statement == 800 && C.Card.MinDue == 80 && C.Card.DueDay == 55 && C.Card.Late == 1);
 TestTrue(TEXT("loan"), C.Loan.Owed == 3100 && C.Loan.Year == TEXT("2010") && C.Loan.RepayFrom == -1);
 TestEqual(TEXT("phone"), C.PhoneArrears, int64(58)); TestEqual(TEXT("notes"), C.Notes.Num(), 1);
 TestTrue(TEXT("net worth"), C.NetWorth.Num() == 1 && C.NetWorth[0].Key == 12 && C.NetWorth[0].Value == 2674);
 TestTrue(TEXT("father"), C.FatherTexts.Num() == 1 && C.FatherTexts[0].Value == TEXT("我是你爸。") && C.FatherSeen == 1 && C.FatherGifts == 1 && C.FatherDay == 13 && C.FatherTotal == 100000);
 return true;
}
#endif
