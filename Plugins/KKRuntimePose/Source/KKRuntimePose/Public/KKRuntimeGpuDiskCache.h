#pragma once
#include "KKRuntimePoseBankTypes.h"
struct FKKRuntimeGpuDiskCacheStats {
 uint64 Hits=0,Misses=0,Invalid=0,Stored=0,Evicted=0,Failures=0,DiskBytes=0,ReadBytes=0,WrittenBytes=0,CapacityBytes=0;
 FString Directory;
 uint64 LockWaitUs=0,ReadUs=0,DecodeUs=0;
};
KKRUNTIMEPOSE_API FKKRuntimeGpuDiskCacheStats OKRuntimeGpuDiskCacheStats();
// Worker only. Integrity/recipe verification before admission; miss is nonfatal.
KKRUNTIMEPOSE_API bool OKLoadRuntimeGpuPoseCache(const FKKRuntimePoseRequest&,FKKRuntimePoseResult&);
// Exactly pack GPU channels and optionally persist. Cache IO failure is nonfatal;
// invalid GPU packing is a preparation failure. Default disk capacity is 4 GiB.
KKRUNTIMEPOSE_API bool OKPackAndSaveRuntimeGpuPoseCache(const FKKRuntimePoseRequest&,FKKRuntimePoseResult&,FString&);
