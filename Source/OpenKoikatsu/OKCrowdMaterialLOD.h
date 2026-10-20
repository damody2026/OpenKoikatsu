#pragma once
#include "CoreMinimal.h"
class USkeletalMesh;
#if WITH_EDITOR
bool OKBuildCrowdMaterialLOD(USkeletalMesh* Mesh, const FString& Root, TArray<TObjectPtr<UObject>>& Assets);
#endif
