#pragma once

#include "CoreMinimal.h"

// StudioScene uses Config.EstData.BackColor on the colour-clearing main
// camera; a depth-only child camera's serialized colour is not a background.
FLinearColor OKStudioClearColorFromSettings(const FString& SystemXml);
FLinearColor OKStudioConfiguredClearColor();
// The clear colour a scene picture shows when its four corners are one plain colour (a scene with nothing behind).
bool OKStudioClearColorFromPicture(const FString& Png, FLinearColor& OutColor);
