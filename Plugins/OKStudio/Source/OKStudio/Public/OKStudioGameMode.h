#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "OKStudioGameMode.generated.h"

/**
 * The OK Studio scene editor on its own (SceneEditor.cmd): no card reader, gallery or other HUD, only the studio's
 * panel and viewer. The scene opens at start (`-OKScene=<file>`: an .okscene, an OK scene card or a KStudio PNG;
 * none: a new scene) and closing the studio (離開) quits.
 */
UCLASS()
class OKSTUDIO_API AOKStudioGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOKStudioGameMode();
	virtual void StartPlay() override;
};
