#pragma once
#include "KKPrivateGpuSource.h"

// Runtime-only intervals grouped by authored influence tuple. Signed weights
// are retained; no positive-weight convex hull assumption. Cost per update is
// proportional to groups, independent of the number of source/draw vertices.
struct FKKPrivateGpuBoundsGroup {
 FUintVector4 Bones;
 FVector4d Min[4],Max[4];
};
struct FKKPrivateGpuBounds {
 uint64 Revision=0;uint32 PaletteCount=0;
 double MaxOffset=0;
 TArray<FKKPrivateGpuBoundsGroup> Groups;
};
KKUNITYBUNDLE_API bool OKBuildPrivateGpuBounds(const FKKPrivateGpuSource&,TConstArrayView<FOKGpuDeformDraw>,FKKPrivateGpuBounds&,FString& Error);
KKUNITYBUNDLE_API bool OKEvaluatePrivateGpuBounds(const FKKPrivateGpuBounds&,const FKKPrivateGpuPose&,FBox&,FString& Error);
