#include "../OKGalleryWidget.h"
#include "Components/ComboBoxString.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKAnimationLookupTest, "OpenKoikatsu.Motion.AnimationLookup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKAnimationLookupTest::RunTest(const FString&)
{
 auto* G = NewObject<UOKGalleryWidget>();
 G->GameClipCombo = NewObject<UComboBoxString>(G);
 G->Selected.Sex = 1;
 G->GameMotionOptions.Add(TEXT("Studio：步行"));
 G->GameMotionRows.Add(TEXT("Studio：步行"), FName(TEXT("WalkRow")));
 G->AddAnimationSourceClips(TEXT("studio/anime/00.unity3d"), {TEXT("walk"), TEXT("idle")});
 G->AddAnimationSourceClips(TEXT("studio/anime/01.unity3d"), {TEXT("walk")});
 G->AddAnimationSourceClips(TEXT("studio/anime/00.unity3d"), {TEXT("walk")});
 G->AddAnimationSourceClips(FString(), {TEXT("states:action/animator/00.unity3d|Base Layer.Walk")});
 G->AddAnimationSourceClips(TEXT("h/anim/male/00.unity3d"), {TEXT("wrong sex")});
 TestEqual(TEXT("All unique exact keys retained and wrong-sex source excluded"), G->GameRawClipKeys.Num(), 4);
 const FString FirstKey = TEXT("studio/anime/00.unity3d|walk");
 const FString SecondKey = TEXT("studio/anime/01.unity3d|walk");
 const FString First = G->GameClipLabels.FindRef(FirstKey);
 const FString Second = G->GameClipLabels.FindRef(SecondKey);
 TestTrue(TEXT("Duplicate animation names independently selectable"), !First.IsEmpty() && !Second.IsEmpty() && First != Second);
 TestFalse(TEXT("Display hides source filename"), First.Contains(TEXT("unity3d")));
 TestEqual(TEXT("First animation resolves exact file"), G->GameRawClipKeys.FindRef(First), FirstKey);
 TestEqual(TEXT("Second animation resolves exact file"), G->GameRawClipKeys.FindRef(Second), SecondKey);
 G->SelectedGameSource = TEXT("studio/anime/01.unity3d");
 G->SelectedGameClip = FirstKey;
 G->PopulateGameClips({});
 TestEqual(TEXT("Selection independent of previously selected file"), G->GameClipCombo->GetSelectedOption(), First);
 TestEqual(TEXT("Unified list contains all raw and catalog motions"), G->GameClipCombo->GetOptionCount(), 6);
 G->PopulateGameClips({TEXT("new motion")});
 TestTrue(TEXT("Adding another source preserves previous animation"), G->GameClipCombo->FindOptionIndex(First) > 0);
 G->SelectedStudioRow = FName(TEXT("WalkRow"));
 G->PopulateGameClips({});
 TestEqual(TEXT("Catalog selection restored by exact row"), G->GameClipCombo->GetSelectedOption(), FString(TEXT("Studio：步行")));
 G->SelectedStudioRow = NAME_None;
 G->SyncGameAnimationSelection(SecondKey);
 TestEqual(TEXT("External playback synchronization reuses exact lookup"), G->GameClipCombo->GetSelectedOption(), Second);
 TestEqual(TEXT("Synchronization preserves complete list"), G->GameClipCombo->GetOptionCount(), 7);
 G->MarkAsGarbage();
 auto* D=NewObject<UOKGalleryWidget>();
 D->GameClipCombo=NewObject<UComboBoxString>(D);
 D->Selected.Sex=1;
 D->AddDemoAnimations();
 TestTrue(TEXT("Original female demo catalog available"),D->DemoAnimationOptions.Num()>67);
 D->PopulateGameClips({});
 TestEqual(TEXT("Demo order follows original maker list"),D->GameClipCombo->GetOptionAtIndex(1),FString(TEXT("Demo：基本")));
 TestTrue(TEXT("Jump demo has exact controller identity"),D->GameRawClipKeys.FindRef(TEXT("Demo：蹦跳")).StartsWith(TEXT("states:controller@")));
 for(int32 I=1;I<=7;++I)TestTrue(TEXT("Standing demo exists"),D->GameRawClipKeys.Contains(FString::Printf(TEXT("Demo：站立 %02d"),I)));
 const FString Jump=D->GameRawClipKeys.FindRef(TEXT("Demo：蹦跳"));D->SelectedGameClip=Jump;D->PopulateGameClips({});
 TestEqual(TEXT("Demo selection survives repopulating"),D->GameClipCombo->GetSelectedOption(),FString(TEXT("Demo：蹦跳")));
 D->MarkAsGarbage();
 auto* C=NewObject<UOKGalleryWidget>();
 C->bIndependentCards=true;C->SelectedIndex=0;FKKCardListEntry ClothingCard;ClothingCard.Path=FPaths::ProjectContentDir()/TEXT("CharacterCards/CrowdAPNG/C000.png");C->Entries.Add(ClothingCard);
 C->OnClothesState(102);
 for(uint8 State:C->ClothesStates)TestEqual(TEXT("Full removal includes gloves stockings socks and shoes"),int32(State),3);
 C->OnClothesState(100);
 for(uint8 State:C->ClothesStates)TestEqual(TEXT("Wear all restores every clothing slot"),int32(State),0);
 C->OnClothesState(101);
 for(int32 I=0;I<9;++I)TestEqual(TEXT("Half removal uses authored partial states"),int32(C->ClothesStates[I]),I<4?1:0);
 C->MarkAsGarbage();
 return !HasAnyErrors();
}
#endif
