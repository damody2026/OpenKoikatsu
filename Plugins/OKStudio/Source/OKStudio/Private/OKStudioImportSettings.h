#pragma once

#include "CoreMinimal.h"

// StudioScene uses Config.EstData.BackColor on the colour-clearing main
// camera; a depth-only child camera's serialized colour is not a background.
FLinearColor OKStudioClearColorFromSettings(const FString& SystemXml);
FLinearColor OKStudioConfiguredClearColor();
