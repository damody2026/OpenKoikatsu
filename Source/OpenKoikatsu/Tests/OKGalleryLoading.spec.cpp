#include "../OKGalleryWidget.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "../OKCardListView.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKGalleryLoadingTest,"OpenKoikatsu.Gallery.LoadSelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKGalleryLoadingTest::RunTest(const FString&)
{
 auto* Widget=NewObject<UOKGalleryWidget>();
 Widget->Entries.SetNum(2);Widget->Entries[0].Path=TEXT("original.png");Widget->Entries[1].Path=TEXT("other.png");
 Widget->SelectedIndex=0;Widget->Selected.Outfits.SetNum(3);Widget->OutfitIndex=1;
 Widget->Selected.Nickname=TEXT("Original selection");Widget->Selected.Body.ShapeValueBody={.5f};
 Widget->CardListView=NewObject<UOKCardListView>(Widget);Widget->OutfitNav=NewObject<UHorizontalBox>(Widget);Widget->OutfitCombo=NewObject<UComboBoxString>(Widget);
 Widget->RefreshOutfitList();Widget->SetLoadBusy(true);
 TestFalse(TEXT("Card list disabled while loading"),Widget->CardListView->GetIsEnabled());
 TestFalse(TEXT("Outfit navigation disabled while loading"),Widget->OutfitNav->GetIsEnabled());
 TestFalse(TEXT("Outfit combo disabled while loading"),Widget->OutfitCombo->GetIsEnabled());
 Widget->ScanAndShow();Widget->SelectIndex(1);Widget->OnOutfitNext();Widget->OnOutfitPrev();
 Widget->OutfitCombo->SetSelectedIndex(2);Widget->OnOutfitSelected(Widget->OutfitCombo->GetOptionAtIndex(2),ESelectInfo::OnMouseClick);
 TestEqual(TEXT("In-flight card identity preserved"),Widget->SelectedIndex,0);
 TestEqual(TEXT("In-flight card metadata preserved"),Widget->Selected.Nickname,FString(TEXT("Original selection")));
 TestEqual(TEXT("In-flight outfit identity preserved"),Widget->OutfitIndex,1);
 TestEqual(TEXT("Displayed outfit restored for blocked callback"),Widget->OutfitCombo->GetSelectedIndex(),1);
 // Same-card shape edits are intentional and are applied to the completed session.
 Widget->OnShapeChanged(true,0,.7f);TestEqual(TEXT("Shape edit remains available"),Widget->Selected.Body.ShapeValueBody[0],.7f);
 // A superseded worker must not publish its catalogue into the active load.
 Widget->LoadGeneration=9;Widget->PendingLoadPairGeneration=9;Widget->PendingLoadPairJson=TEXT("current");
 Widget->FinishLoad3D(8,FOkSession(),false,TEXT("cancelled"),FString(),FVector2D::ZeroVector,TEXT("stale"));
 TestEqual(TEXT("Stale catalogue generation ignored"),Widget->PendingLoadPairGeneration,9);
 TestEqual(TEXT("Stale catalogue payload ignored"),Widget->PendingLoadPairJson,FString(TEXT("current")));
 Widget->SetLoadBusy(false);Widget->OnOutfitNext();TestEqual(TEXT("Outfit navigation resumes"),Widget->OutfitIndex,2);
 Widget->OnOutfitPrev();TestEqual(TEXT("Reverse navigation resumes"),Widget->OutfitIndex,1);
 TestTrue(TEXT("Card selection restored"),Widget->CardListView->GetIsEnabled());
 TestTrue(TEXT("Outfit navigation restored"),Widget->OutfitNav->GetIsEnabled());
 TestTrue(TEXT("Outfit combo restored"),Widget->OutfitCombo->GetIsEnabled());
 UE_LOG(LogTemp,Display,TEXT("Gallery loading selection valid=%d blocked_card=1 blocked_outfit=1 resumed=1 shape_edit=1"),HasAnyErrors()?0:1);
 Widget->MarkAsGarbage();return !HasAnyErrors();
}
#endif
