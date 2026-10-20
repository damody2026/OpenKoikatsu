#pragma once
#include "OKRuntimeGpuDeform.h"

// Runtime mapping into an already deformed source position stream. No bone
// count, card identity, authored mesh or deformation backend is assumed.
struct FOKGpuCoatingBinding {
 FUintVector4 Vertices=FUintVector4(0,0,0,0); // vertex, face edge start/end, reserved
 FVector4f Offset=FVector4f(0,0,0,0),Normal=FVector4f(0,0,1,0);
};
KKCROWDGPUPACKING_API bool OKValidateGpuCoatingBindings(TConstArrayView<FOKGpuCoatingBinding>,uint32 SourceCount,const FMatrix44f& SourceToOverlay,FString& Error);
KKCROWDGPUPACKING_API bool OKGpuCoatingAvailable();
// Source is a float3 ByteAddressBuffer. Binding upload/ownership belongs to
// the caller; output retains the existing deformation/packing contract.
// Invalid requests add no pass. Production has no CPU readback.
KKCROWDGPUPACKING_API bool OKAddRuntimeGpuCoatingPass(FRDGBuilder&,FRDGBufferRef SourcePositions,FRDGBufferRef Bindings,FRDGBufferRef Output,
 uint32 SourceCount,uint32 Count,const FMatrix44f& SourceToOverlay);
