#include "../OKMotionComboBox.h"
#include "../OKMotionLabelZh.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/SListView.h"
#include "InputCoreTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKMotionSearchTest, "OpenKoikatsu.Motion.SearchFilter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKMotionSearchTest::RunTest(const FString&)
{
	auto* Combo = NewObject<UOKMotionComboBox>();
	const FString A = TEXT("Studio：分類 / 歩く / studio/anime/00.unity3d");
	const FString B = TEXT("Studio：分類 / 歩く / studio/anime/01.unity3d");
	Combo->AddOption(TEXT("選擇遊戲動作")); Combo->AddOption(A); Combo->AddOption(B);
	Combo->SetSelectedOption(B);
	Combo->TakeWidget();
	Combo->SetSearchText(FText::FromString(TEXT("STUDIO 00.unity3d")));
	TestEqual(TEXT("All tokens and case-insensitive source match"), Combo->GetFilteredOptions().Num(), 1);
	TestEqual(TEXT("Matching raw key preserved"), Combo->GetFilteredOptions()[0], A);
	TestEqual(TEXT("Filtering hidden selection preserves playback key"), Combo->GetSelectedOption(), B);
	TestEqual(TEXT("Full option count preserved"), Combo->GetOptionCount(), 3);
	TestEqual(TEXT("Full source index preserved"), Combo->FindOptionIndex(B), 2);
	Combo->CommitSearchSelection(Combo->FilteredOptions[0], ESelectInfo::OnNavigation);
	TestEqual(TEXT("Navigation does not select motion"), Combo->GetSelectedOption(), B);
	Combo->CommitSearchSelection(Combo->FilteredOptions[0], ESelectInfo::OnMouseClick);
	TestEqual(TEXT("Explicit filtered selection commits exact raw key"), Combo->GetSelectedOption(), A);
	Combo->SetSearchText(FText::FromString(TEXT("no-such-motion")));
	TestEqual(TEXT("No match is empty"), Combo->GetFilteredOptions().Num(), 0);
	TestEqual(TEXT("No match preserves selection"), Combo->GetSelectedOption(), A);
	Combo->SetSearchText(FText::FromString(TEXT(" \t ")));
	TestEqual(TEXT("Whitespace restores every option"), Combo->GetFilteredOptions().Num(), 3);
	const FString Display = OKMotionLabelDisplay(A);
	Combo->SetSearchText(FText::FromString(Display));
	TestEqual(TEXT("Translated full label searchable"), Combo->GetFilteredOptions().Num(), 1);
	Combo->SearchBox->SetText(FText::FromString(TEXT("01.unity3d")));
	TestEqual(TEXT("Real Slate text change filters immediately"), Combo->GetFilteredOptions()[0], B);
	Combo->HandleSearchCommitted(FText::FromString(TEXT("01.unity3d")), ETextCommit::OnEnter);
	TestEqual(TEXT("Search Enter commits exact match"), Combo->GetSelectedOption(), B);
	Combo->SetSearchText(FText::GetEmpty());
	const auto Key = [](FKey K) { return FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0); };
	const auto Geometry = FGeometry();
	TestTrue(TEXT("Search Down handled"), Combo->HandleSearchKey(Geometry, Key(EKeys::Down)).IsEventHandled());
	TestEqual(TEXT("Search Down does not commit"), Combo->GetSelectedOption(), B);
	Combo->SearchList->SetSelection(Combo->Options[1], ESelectInfo::OnNavigation);
	Combo->SearchList->OnKeyDown(Geometry, Key(EKeys::Home));
	TestEqual(TEXT("Real list Home navigation does not commit"), Combo->GetSelectedOption(), B);
	Combo->SearchList->SetSelection(Combo->Options[1], ESelectInfo::OnNavigation);
	Combo->SearchList->OnKeyDown(Geometry, Key(EKeys::Enter));
	TestEqual(TEXT("Real list Enter commits highlighted key"), Combo->GetSelectedOption(), A);
	Combo->SetSearchText(FText::FromString(TEXT("01.unity3d")));
	Combo->SearchList->SetSelection(Combo->Options[1], ESelectInfo::Direct);
	Combo->SearchList->OnKeyDown(Geometry, Key(EKeys::Enter));
	TestEqual(TEXT("Hidden stale highlight cannot commit"), Combo->GetSelectedOption(), A);
	Combo->SearchList->Private_OnItemClicked(Combo->Options[2]);
	TestEqual(TEXT("Real list mouse click commits exact key"), Combo->GetSelectedOption(), B);
	Combo->SetIsEnabled(false);
	Combo->HandleSearchCommitted(FText::FromString(TEXT("00.unity3d")), ETextCommit::OnEnter);
	TestEqual(TEXT("Disabled menu cannot commit"), Combo->GetSelectedOption(), B);
	Combo->SetIsEnabled(true);
	const int32 Cached = Combo->SearchLabels.Num(); Combo->HandleOpening();
	TestEqual(TEXT("Reopening reuses unchanged translations"), Combo->SearchLabels.Num(), Cached);
	TestTrue(TEXT("Escape closes without committing"), Combo->HandleSearchKey(Geometry, Key(EKeys::Escape)).IsEventHandled());
	Combo->ClearOptions(); Combo->AddOption(TEXT("replacement/source")); Combo->HandleOpening();
	TestEqual(TEXT("Removed source translation cache pruned"), Combo->SearchLabels.Num(), 0);
	TestEqual(TEXT("Source replacement opening rebuilds filtered options"), Combo->GetFilteredOptions().Num(), 1);
	TestEqual(TEXT("Old options removed"), Combo->GetFilteredOptions()[0], FString(TEXT("replacement/source")));
	// Exercise the catalog-sized cached path without parsing native controllers.
	Combo->ClearOptions();
	for (int32 I=0; I<8751; ++I) Combo->AddOption(FString::Printf(TEXT("Studio：分類 / 歩く %d / studio/anime/%d.unity3d"), I, I));
	const double ColdStart = FPlatformTime::Seconds();
	Combo->SetSearchText(FText::FromString(TEXT("步行")));
	const double ColdMilliseconds = (FPlatformTime::Seconds()-ColdStart)*1000.;
	TestTrue(TEXT("Cold translated search below 150 ms"), ColdMilliseconds<150.);
	const double Start = FPlatformTime::Seconds();
	Combo->SetSearchText(FText::FromString(TEXT("studio 8750.unity3d")));
	const double Milliseconds = (FPlatformTime::Seconds()-Start)*1000.;
	TestEqual(TEXT("8751-row cached filtering exact source"), Combo->GetFilteredOptions().Num(), 1);
	TestTrue(TEXT("Cached search below 150 ms"), Milliseconds<150.);
	AddInfo(FString::Printf(TEXT("Motion filter 8751 rows cold %.3f ms cached %.3f ms"), ColdMilliseconds, Milliseconds));
	Combo->ReleaseSlateResources(true);
	return true;
}
#endif
