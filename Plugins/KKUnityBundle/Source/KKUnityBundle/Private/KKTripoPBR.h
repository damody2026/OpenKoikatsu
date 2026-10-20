#pragma once
#include "CoreMinimal.h"
class UMaterial;
class UTexture2D;
class AActor;
// Editor preview trial: native UE DefaultLit, not a toon/Phong approximation.
UMaterial* OKMakeTripoPBRMaterial(UTexture2D* Base,UTexture2D* Normal,UTexture2D* Roughness,UTexture2D* Metallic);
void OKEnsureTripoPBRPreviewLights(AActor* Owner);
