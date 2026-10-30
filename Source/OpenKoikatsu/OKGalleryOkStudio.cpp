// The gallery's way into OK Studio (Plugins/OKStudio): the loaded card becomes the scene's first character; the gallery
// hides while the studio runs and comes back when it closes. The 動作時間軸 button opens the timeline window too.
#include "OKCharacterController.h"
#include "KKPreviewActor.h"
#include "OKSceneActor.h"
#include "OKStudioGame.h"
#include "OpenKoikatsuPlayerController.h"

void UOKCharacterController::OnEnterOkStudio()
{
	if (bOkStudioOpen || InteractionWidget || InteractionScene) return;
	if (!IsValid(PreviewActor) || !PreviewActor->Session.IsOpen() || bLoadInFlight)
	{
		ShowAnimationStatus(TEXT("請先載入角色，再進入工作室。"), true);
		return;
	}
	ApplyPendingEditorMods();
	const FString Card = !PreviewCardPath.IsEmpty() ? PreviewCardPath : (Entries.IsValidIndex(SelectedIndex) ? Entries[SelectedIndex].Path : FString());
	FString Error;
	UOKStudioGame* Game = Card.IsEmpty() ? nullptr : UOKStudioGame::Start(GetWorld(), FString(), Error);
	if (!Game) { ShowAnimationStatus(TEXT("無法開啟工作室：") + (Card.IsEmpty() ? FString(TEXT("沒有角色卡")) : Error), true); return; }
	bOkStudioOpen = true;
	OnStopPair();
	// The studio scene stands where the gallery character stood; the gallery hides until the studio closes.
	const FVector Origin = PreviewActor->GetActorLocation();
	Game->SceneActor->SetActorLocation(Origin);
	Game->Focus = Origin + FVector(0.f, 0.f, 90.f);
	Game->AddCharacter(Card, PreviewOutfitIndex != INDEX_NONE ? PreviewOutfitIndex : OutfitIndex);
	for (AKKPreviewActor* A : { PreviewActor.Get(), PartnerActor.Get() }) if (IsValid(A)) A->SetActorHiddenInGame(true);
	if (auto* PC = Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer())) { PC->SetPaintPreviewActive(false); PC->bExternalMouseOwner = false; }
	VisibilityBeforeOkStudio = GetVisibility();
	SetVisibility(ESlateVisibility::Collapsed);
	TWeakObjectPtr<UOKCharacterController> Self(this);
	Game->OnStopped.AddLambda([Self] { if (Self.IsValid()) Self->OnOkStudioStopped(); });
	UE_LOG(LogTemp, Display, TEXT("OK studio (scene tree) open card=%s"), *Card);
}

void UOKCharacterController::OnEnterOkStudioTimeline()
{
	OnEnterOkStudio();
	if (UOKStudioGame* Game = bOkStudioOpen ? UOKStudioGame::Get() : nullptr) Game->RunUiCommand(TEXT("window.open"), TEXT("timeline"));
}

void UOKCharacterController::OnOkStudioStopped()
{
	if (!bOkStudioOpen) return;
	bOkStudioOpen = false;
	for (AKKPreviewActor* A : { PreviewActor.Get(), PartnerActor.Get() }) if (IsValid(A)) A->SetActorHiddenInGame(false);
	SetVisibility(VisibilityBeforeOkStudio);
	SetModelLayout(bModelLayout);
	ApplyClickInputMode();
	UE_LOG(LogTemp, Display, TEXT("OK studio (scene tree) closed"));
}
