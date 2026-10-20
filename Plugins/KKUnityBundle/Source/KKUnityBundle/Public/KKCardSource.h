#pragma once
#include "CoreMinimal.h"
// Resolves original cards through application-owned resources and adjacent zipmods only.
KKUNITYBUNDLE_API bool OKResolveCardSource(const FString& Card,FString& SourceRoot,FString& Error);
#if WITH_EDITOR
// Mount the character/animation base library, excluding original game and Studio maps.
// Excluded entries remain valid catalog metadata but their payloads need not be installed.
#endif
