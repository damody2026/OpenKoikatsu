// What stays in C++ around the heroine module: rumour confidence in the save, and the favor caps and confession commit.
// The interaction table, gates, reactions, chastity, stranger cap and being alone with him are game rules now, tested in
// Tools/ok-rules (cargo test: heroine.rs spec_* tests carry the assertions that were here).
#include "../OKStreetHeroine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetHeroineRumorTest, "OpenKoikatsu.StreetSocial.Heroine.RumorConfidence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetHeroineRumorTest::RunTest(const FString&)
{
 // How sure each listener is stays with the rumour (P4) and survives the save; first-hand falls back to the rumour's.
 FOKStreetStore Store; FOKStreetRumor R; R.Id = 1; R.Text = TEXT("test"); R.Kind = TEXT("gift"); R.Confidence = 1.f; R.Day = 1;
 R.KnownBy = {TEXT("a"), TEXT("b")}; R.Heard.Add(TEXT("b"), 0.6f); Store.Rumors.Add(R);
 TestEqual(TEXT("first-hand"), Store.Rumors[0].SureOf(TEXT("a")), 1.f);
 TestEqual(TEXT("second-hand"), Store.Rumors[0].SureOf(TEXT("b")), 0.6f);
 FOKStreetStore Back; FString Error; OKStreetParseSave(OKStreetWriteSave(Store), Back, Error);
 if (TestEqual(TEXT("rumour kept"), Back.Rumors.Num(), 1)) TestTrue(TEXT("heard kept"), FMath::IsNearlyEqual(Back.Rumors[0].SureOf(TEXT("b")), 0.6f, 0.001f) && Back.Rumors[0].SureOf(TEXT("a")) == 1.f);
 return true;
}

// Favor caps and confessions are rules (Tools/ok-rules talk.rs, cargo test friends_and_confessions / shipped_lines_and_events).
#endif
