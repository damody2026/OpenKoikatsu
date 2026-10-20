#pragma once
#include "CoreMinimal.h"
#if WITH_EDITOR
int32 OKOptimizeCrowdQuality(int32 Start, int32 End, const FString& Params);
int32 OKBuildCrowdBodyCoverage(int32 Start,int32 End,const FString& Params);
int32 OKRepairCrowdBindings(int32 Start,int32 End,const FString& Params);
class USkeletalMesh;
class UAnimSequence;
USkeletalMesh* OKBuildCrowdNearHair(USkeletalMesh* Source,UAnimSequence* Walk,const FString& Root,uint32 TriangleLimit);
struct FKKImportedPart;
bool OKCrowdIsHeadAccessory(const FKKImportedPart& Part,UAnimSequence* Walk);
#endif
