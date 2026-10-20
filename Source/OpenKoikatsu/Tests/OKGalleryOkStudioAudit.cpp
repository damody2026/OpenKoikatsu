#include "../OKCharacterController.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "KKPreviewActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OKSceneActor.h"
#include "OKStudioGame.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"
#if WITH_DEV_AUTOMATION_TESTS
// ok.GalleryOkStudioAudit: the gallery's 進入工作室 opens OK Studio with the loaded card and the gallery returns when it closes.
// Results in Saved/OKStudio/gallery_audit.txt (OK_CARD_DIRECTORY picks the card list).
namespace OKGalleryOkStudioAudit
{
struct FRun : TSharedFromThis<FRun>
{
	TWeakObjectPtr<UWorld> World;
	FTimerHandle Timer;
	double Started = FPlatformTime::Seconds();
	int32 Stage = 0;
	int32 Character = -1;
	TArray<FString> Lines;
	int32 Failures = 0;

	void Check(bool bOk, const FString& What)
	{
		Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What));
		if (!bOk) ++Failures;
	}
	void Stop()
	{
		const auto Keep = AsShared();
		if (UWorld* W = World.Get()) W->GetTimerManager().ClearTimer(Timer);
		Lines.Add(Failures ? FString::Printf(TEXT("RESULT FAIL (%d)"), Failures) : FString(TEXT("RESULT PASS")));
		for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("ok.GalleryOkStudioAudit: %s"), *L);
		FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/gallery_audit.txt")));
		FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
	}
	void Tick()
	{
		const auto Keep = AsShared();
		if (FPlatformTime::Seconds() - Started > 240) { Check(false, FString::Printf(TEXT("timeout at stage %d"), Stage)); Stop(); return; }
		UOKCharacterController* G = nullptr;
		for (TObjectIterator<UOKCharacterController> It; It; ++It) if (IsValid(*It) && It->GetWorld() == World.Get()) { G = *It; break; }
		if (!G) return;
		if (Stage == 0)
		{
			if (G->Entries.IsEmpty()) return;
			G->SelectIndex(0);
			G->OnLoad3D();
			Stage = 1;
			return;
		}
		if (Stage == 1)
		{
			if (G->bLoadInFlight || !IsValid(G->PreviewActor) || !G->PreviewActor->Session.IsOpen()) return;
			const FString Card = !G->PreviewCardPath.IsEmpty() ? G->PreviewCardPath : G->Entries[G->SelectedIndex].Path;
			G->OnEnterOkStudio();
			UOKStudioGame* Game = UOKStudioGame::Get();
			Check(Game != nullptr && G->bOkStudioOpen, TEXT("進入工作室 opens OK Studio"));
			if (!Game) { Stop(); return; }
			for (const FOKSceneObject& O : Game->Document->Objects) if (O.Kind == EOKSceneObjectKind::Character) Character = O.Id;
			const FOKSceneObject* O = Game->Document->Find(Character);
			Check(O && O->Character.Card == Card, TEXT("the scene's character is the loaded card: ") + Card);
			Check(G->GetVisibility() == ESlateVisibility::Collapsed && G->PreviewActor->IsHidden(), TEXT("the gallery and its character hide"));
			Check(Game->SceneActor->GetActorLocation().Equals(G->PreviewActor->GetActorLocation(), 0.1f), TEXT("the scene stands where the gallery character stood"));
			Stage = 2;
			return;
		}
		if (Stage == 2)
		{
			UOKStudioGame* Game = UOKStudioGame::Get();
			const AKKPreviewActor* Shown = Game && Game->SceneActor ? Game->SceneActor->PreviewOf(Character) : nullptr;
			if (!Shown) return;
			Check(Shown->Session.PartCount() > 0, FString::Printf(TEXT("studio character built: %d parts"), Shown->Session.PartCount()));
			Game->RequestStop();
			Stage = 3;
			return;
		}
		if (Stage == 3)
		{
			if (UOKStudioGame::Get()) return;
			Check(!G->bOkStudioOpen && G->GetVisibility() != ESlateVisibility::Collapsed && IsValid(G->PreviewActor) && !G->PreviewActor->IsHidden(), TEXT("離開 returns to the gallery with its character"));
			Stop();
		}
	}
};
}

static FAutoConsoleCommandWithWorld GalleryOkStudioAudit(TEXT("ok.GalleryOkStudioAudit"), TEXT("Gallery 進入工作室 opens OK Studio with the loaded card; 離開 returns"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		auto R = MakeShared<OKGalleryOkStudioAudit::FRun>();
		R->World = W;
		if (!W) { R->Check(false, TEXT("no world")); R->Stop(); return; }
		W->GetTimerManager().SetTimer(R->Timer, FTimerDelegate::CreateLambda([R] { R->Tick(); }), .1f, true);
	}));
#endif
