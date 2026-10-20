#include "OpenKoikatsuGameMode.h"
#include "OKHUD.h"
#include "OpenKoikatsuPlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AOpenKoikatsuGameMode::AOpenKoikatsuGameMode()
{
	HUDClass = AOKHUD::StaticClass();
	PlayerControllerClass = AOpenKoikatsuPlayerController::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
    FString Manifest;
    if(FParse::Value(FCommandLine::Get(),TEXT("StreetSocialManifest="),Manifest)||FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimeScene="),Manifest))DefaultPawnClass=nullptr;
}
