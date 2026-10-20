#include "../OKInteractionScene.h"
#include "../OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ComboBoxString.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKInteractionRestoreTest,"OpenKoikatsu.Interaction.RestoreControls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKInteractionRestoreTest::RunTest(const FString&) {
    auto* Widget=NewObject<UOKInteractionWidget>();auto* Original=NewObject<UVerticalBox>(Widget);auto* Dock=NewObject<UVerticalBox>(Widget);
    auto* A=NewObject<UTextBlock>(Widget);auto* B=NewObject<UTextBlock>(Widget);auto* C=NewObject<UTextBlock>(Widget);
    Original->AddChild(A);Original->AddChild(B);Original->AddChild(C);
    auto& Panel=Widget->Panels.AddDefaulted_GetRef();Panel.Parent=Original;Panel.Children={A,B,C};
    Cast<UVerticalBoxSlot>(B->Slot)->SetPadding(FMargin(7.f));
    for(auto* Child:{A,B,C})Panel.SlotTemplates.Add(DuplicateObject<UPanelSlot>(Child->Slot,Widget));
    B->RemoveFromParent();Dock->AddChild(B);A->RemoveFromParent();Dock->AddChild(A);
    Widget->MotionCancelled=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false);Widget->SpeechCancelled=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false);
    Widget->RestoreControls();TestTrue(TEXT("Return cancels motion and speech preparation"),*Widget->MotionCancelled&&*Widget->SpeechCancelled);TestEqual(TEXT("Original control count"),Original->GetChildrenCount(),3);
    TestTrue(TEXT("Complete original order restored"),Original->GetChildAt(0)==A&&Original->GetChildAt(1)==B&&Original->GetChildAt(2)==C);
    TestEqual(TEXT("Showcase has no borrowed controls"),Dock->GetChildrenCount(),0);
    TestEqual(TEXT("Original slot padding restored"),Cast<UVerticalBoxSlot>(B->Slot)->GetPadding(),FMargin(7.f));
    Widget->RestoreControls();TestEqual(TEXT("Return is idempotent"),Original->GetChildrenCount(),3);
    auto* Gallery=NewObject<UOKGalleryWidget>();Gallery->Selected.Sex=0;Gallery->OnEnterInteraction();TestNull(TEXT("Male card cannot enter heroine scene"),Gallery->InteractionWidget.Get());
    Gallery->bEnterInteractionAfterLoad=true;Gallery->OnExitInteraction();TestFalse(TEXT("Return cancels pending entry"),Gallery->bEnterInteractionAfterLoad);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Actor=World->SpawnActor<AKKPreviewActor>();Actor->MouthOpen01=.2f;Actor->bSpeechActive=true;Actor->SpeechOpen=.7f;
    Actor->bSpeechDrivesMouth=true;TestEqual(TEXT("Automatic mouth uses actual audio meter"),Actor->EffectiveFaceRate(0),.7f);
    Actor->bSpeechDrivesMouth=false;TestEqual(TEXT("Manual mouth wins without stopping audio"),Actor->EffectiveFaceRate(0),.2f);
    TestTrue(TEXT("Mode switch preserves audio state"),Actor->bSpeechActive);
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKInteractionSpeechCacheTest,"OpenKoikatsu.Interaction.SpeechCache",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKInteractionSpeechCacheTest::RunTest(const FString&) {
    auto* Widget=NewObject<UOKInteractionWidget>();
    // Three voice banks cannot pin old PCM beyond the scene's memory budget.
    TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm;
    {auto Owned=MakeShared<FKKSpeechPcm,ESPMode::ThreadSafe>();Owned->Samples.SetNumZeroed(24*1024*1024);Owned->Channels=1;Owned->SampleRate=48000;Pcm=Owned;}
    Widget->CacheSpeech(TEXT("voiceA"),Pcm);Widget->CacheSpeech(TEXT("voiceB"),Pcm);Widget->CacheSpeech(TEXT("voiceC"),Pcm);
    TestFalse(TEXT("Old voice evicted"),Widget->SpeechCache.Contains(TEXT("voiceA")));
    TestTrue(TEXT("Current voices remain available"),Widget->SpeechCache.Contains(TEXT("voiceB"))&&Widget->SpeechCache.Contains(TEXT("voiceC")));
    TestEqual(TEXT("Resident PCM accounting"),Widget->SpeechCacheBytes,int64(96*1024*1024));
    Widget->CacheSpeech(TEXT("voiceC"),Pcm);TestEqual(TEXT("Duplicate preparation does not double count"),Widget->SpeechCacheBytes,int64(96*1024*1024));
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Actor=World->SpawnActor<AKKPreviewActor>();
    Widget->Gallery=NewObject<UOKGalleryWidget>();Widget->Gallery->PreviewActor=Actor;Widget->Gallery->SpeechCombo=NewObject<UComboBoxString>(Widget->Gallery);
    FKKSpeechLine Selected;Selected.AudioPath=TEXT("voiceB");Actor->SpeechLines.Add(Selected);Widget->Gallery->SpeechCombo->AddOption(TEXT("selected"));Widget->Gallery->SpeechCombo->SetSelectedIndex(0);
    Widget->CacheSpeech(TEXT("voiceD"),Pcm);
    TestTrue(TEXT("Background preloading cannot evict the selected line"),Widget->SpeechCache.Contains(TEXT("voiceB")));
    TestFalse(TEXT("Unselected older line is evicted instead"),Widget->SpeechCache.Contains(TEXT("voiceC")));
    TestEqual(TEXT("Pinned selection remains within budget"),Widget->SpeechCacheBytes,int64(96*1024*1024));
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
#endif
