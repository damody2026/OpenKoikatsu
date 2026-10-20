#pragma once
#include "CoreMinimal.h"
class UWorld;
struct FOKCrowdBonePackingStats {bool Installed=false;bool Verified=false;uint64 Passes=0,Jobs=0,Bones=0,VerifiedJobs=0,Mismatches=0;int32 Mode=1;uint64 WorkItems=0,ReusedBones=0,RawSourceBytes=0,UniqueRawSourceBytes=0,WrittenBytes=0,AllocatedBytes=0,FootprintSamples=0,GpuPasses=0,GpuFallbacks=0,GpuVerifiedProxies=0;};
void OKInstallCrowdBonePacking(UWorld* World,bool Verify);
FOKCrowdBonePackingStats OKGetCrowdBonePackingStats();
