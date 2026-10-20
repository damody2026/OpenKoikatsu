#pragma once
#include "CoreMinimal.h"
#include "KKRuntimeSharedMaterial.h"
class UTexture2D;
struct FPostProcessSettings;
// Scene preferences independent of character/APNG source data.
KKRUNTIMEPOSE_API bool OKCharacterDisplayShadowsEnabled();
// Character maker lighting from ChaShader / Manager.Character, not Studio.
// _ShadowDepth stays in the session's Studio encoding; the binder restores
// GraphicData.shadowDepth. Does not change EnableShadows.
KKRUNTIMEPOSE_API void OKApplyCharacterMakerLighting(FKKRuntimeMaterialScene& Scene);
KKRUNTIMEPOSE_API FString OKCharacterDisplayRampPath();
// The same authored display LUT as CardReader. No card-specific correction.
KKRUNTIMEPOSE_API UTexture2D* OKLoadRuntimeDisplayLUT(const FString& Path,FString& Error);
KKRUNTIMEPOSE_API void OKApplyRuntimeDisplayProfile(FPostProcessSettings& Settings,UTexture2D* LUT);
