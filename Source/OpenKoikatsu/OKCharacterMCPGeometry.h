#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class AKKPreviewActor;
// Read current posed triangles without switching the renderer's skinning mode.
TSharedPtr<FJsonObject> OKCharacterMCPGeometry(AKKPreviewActor* Actor,FString& Error);
