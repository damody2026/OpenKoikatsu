#pragma once
#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
KKCROWDGPUPACKING_API bool OKGpuBonePackingAvailable();
KKCROWDGPUPACKING_API void OKAddGpuBonePackingPass(FRDGBuilder& Graph,FRDGBufferRef Raw,FRDGBufferRef Work,FRDGBufferSRVRef Maps,FRDGBufferRef Output,uint32 WorkCount);
