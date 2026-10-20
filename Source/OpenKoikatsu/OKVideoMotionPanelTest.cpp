#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "OKVideoMotionPanel.h"

// The editor's video motion list reads pair manifests from Saved/VideoMotions.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKVideoMotionPairsTest, "OpenKoikatsu.VideoMotion.PairList", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKVideoMotionPairsTest::RunTest(const FString&)
{
	TArray<FString> Problems;
	const TArray<UOKVideoMotionPanel::FPair> Pairs = UOKVideoMotionPanel::ReadPairs(&Problems);
	for (const FString& Problem : Problems) AddError(Problem);
	for (const auto& Pair : Pairs) AddInfo(FString::Printf(TEXT("pair %s: %s + %s"), *Pair.Name, *Pair.Female, *Pair.Male));
	return Problems.IsEmpty();
}
#endif
