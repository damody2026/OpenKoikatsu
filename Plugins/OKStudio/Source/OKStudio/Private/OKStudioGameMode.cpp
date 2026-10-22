#include "OKStudioGameMode.h"
#include "OKStudioGame.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

AOKStudioGameMode::AOKStudioGameMode()
{
	HUDClass = AHUD::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void AOKStudioGameMode::StartPlay()
{
	Super::StartPlay();
	// Engine on-screen messages (shader compiling, renderer warnings such as competing directional lights) are not the
	// editor's UI: what DisableAllScreenMessages turns off.
	GAreScreenMessagesEnabled = false;
	if (GEngine) GEngine->bEnableOnScreenDebugMessages = false;
	// The next tick: the game viewport and the player controller are ready for the studio's panel and view target.
	GetWorldTimerManager().SetTimerForNextTick([this]
	{
		FString File;
		FParse::Value(FCommandLine::Get(), TEXT("OKScene="), File);
		if (!File.IsEmpty() && FPaths::IsRelative(File)) File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), File);
		FString Error;
		UOKStudioGame* Game = UOKStudioGame::Start(GetWorld(), File, Error);
		if (!Game) { UE_LOG(LogTemp, Error, TEXT("OK scene editor: %s"), *Error); FPlatformMisc::RequestExit(false); return; }
		if (!Error.IsEmpty()) UE_LOG(LogTemp, Warning, TEXT("OK scene editor: %s"), *Error);
		// Nothing to return to: closing the studio closes the editor.
		Game->OnStopped.AddLambda([] { FPlatformMisc::RequestExit(false); });
		UE_LOG(LogTemp, Display, TEXT("OK scene editor: started %s"), File.IsEmpty() ? TEXT("(new scene)") : *File);
	});
}
