#pragma once
#include "CoreMinimal.h"
// Shared preflight: never reuse disk assets or loaded, unsaved packages.
KKUNITYBUNDLE_API bool OKValidateImportDestination(const FString& Destination,FString& Error);
