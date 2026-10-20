#pragma once
#include "CoreMinimal.h"
#if WITH_EDITOR
class USkeletalMesh;
class UAnimSequence;
class UMaterialInterface;
// Asset-agnostic authoring rule; no card indices, names or per-card parameters.
USkeletalMesh* OKRepairCrowdHairOutline(USkeletalMesh* Source,UAnimSequence* Clip,const TSet<UMaterialInterface*>& HairMaterials,const FString& Destination,int32& Changed);
// Read-only cache validation. -1 means the source could not be evaluated.
int32 OKCountCrowdHairOutlineIssues(USkeletalMesh* Source,UAnimSequence* Clip,const TSet<UMaterialInterface*>& HairMaterials);
#endif
