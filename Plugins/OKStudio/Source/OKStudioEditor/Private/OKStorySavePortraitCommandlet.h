#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OKStorySavePortraitCommandlet.generated.h"

/**
 * Saves the story portrait's UI material (and the two render targets its samplers default to) under
 * /Game/OKStudio/Story, so cooked games (the street, a packaged OK Studio) have it:
 *   UnrealEditor-Cmd.exe OpenKoikatsu.uproject -run=OKStorySavePortrait -unattended -nullrhi
 */
UCLASS()
class UOKStorySavePortraitCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
