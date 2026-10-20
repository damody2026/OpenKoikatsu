#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"

// Debug aid: OKPhysXSecondary.Shot <delaySeconds> <name> saves a UI-free viewport screenshot
// (Saved/Screenshots/Windows/<name>.png) so a run can be inspected without touching the window.
static FAutoConsoleCommand GShotCommand(
	TEXT("OKPhysXSecondary.Shot"), TEXT("Usage: OKPhysXSecondary.Shot <delaySeconds> <name>"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const float Delay = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
		const FString Name = Args.Num() > 1 ? Args[1] : TEXT("OKPhysXShot");
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Name](float)
		{
			FScreenshotRequest::RequestScreenshot(Name, false, false);
			return false;
		}), Delay);
	}));
