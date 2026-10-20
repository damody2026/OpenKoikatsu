#pragma once
#include "KKRuntimePoseBankTypes.h"

// Keep every exact renderer palette column and the ancestor closure of any
// additional native bone needed for gameplay sockets. Native simulation still
// uses the full snapshot; this only reduces the sampled/uploaded render rig.
// Output arguments remain unchanged on rejection. OldToNew is INDEX_NONE for
// removed bones; no caller may retain the old numeric pose indices.
KKRUNTIMEPOSE_API bool OKCompactRuntimeRenderRig(
 TConstArrayView<FKKRuntimePoseBone> Source,
 TConstArrayView<FKKRuntimePalettePart> Parts,
 TConstArrayView<FName> AdditionalBones,
 TArray<FKKRuntimePoseBone>& Out,TArray<int32>& OldToNew,FString& Error);
