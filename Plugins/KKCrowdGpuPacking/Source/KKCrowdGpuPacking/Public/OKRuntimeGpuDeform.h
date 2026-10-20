#pragma once
#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

// Owned immutable source, in component coordinates (or Unity meters when the
// explicit NativeUnitySpace mode is installed). Keep original float weights;
// signed influences must not be quantized/clamped to skeletal uint16 weights.
struct FOKGpuDeformSource {
 FVector4f Position=FVector4f(0,0,0,1), Normal=FVector4f(0,0,1,0), Tangent=FVector4f(1,0,0,1), Weights=FVector4f(0,0,0,0);
 FUintVector4 Bones=FUintVector4(0,0,0,0);
};
struct FOKGpuDeformDraw {
 FUintVector4 Binding=FUintVector4(0,0,0,0); // source index, section (zero = surface), reserved
 FVector4f Offset=FVector4f(0,0,0,0), SectionNormal=FVector4f(0,0,1,0), RestNormal=FVector4f(0,0,1,0);
};
struct FOKGpuDeformOutput {FVector4f Position, Normal, Tangent;};

// Validate once when installing a source revision; pose rows are validated on
// each publication. Source replacement must replace its validated buffers too.
KKCROWDGPUPACKING_API bool OKValidateGpuDeformSource(
 TConstArrayView<FOKGpuDeformSource> Source,TConstArrayView<FOKGpuDeformDraw> Draw,
 uint32 PaletteCount,uint32 FallbackSlot,FString& Error);
KKCROWDGPUPACKING_API bool OKGpuDeformAvailable();
KKCROWDGPUPACKING_API bool OKPrepareGpuDeformPalette(TConstArrayView<FMatrix44f> Matrices,TArray<FVector4f>& Rows,FString& Error);
// Palette is 3 float4 column-vector affine rows per matrix. Output is one
// FOKGpuDeformOutput per draw vertex. Production never reads it back to the CPU.
// For private native rigs, supply the pre-correction palette plus the separate
// placement correction. Folding them loses rot_normal's collapsed-vector
// fallback. In that mode the unweighted fallback palette slot is identity.
// NativeUnitySpace converts position/directions to UE (-X,Z,Y) only AFTER
// both native normalization stages. Draw offsets/normals remain UE coordinates.
// False adds no pass and leaves the previous output untouched.
KKCROWDGPUPACKING_API bool OKAddRuntimeGpuDeformPass(FRDGBuilder& Graph,
 FRDGBufferRef Source,FRDGBufferRef Draw,FRDGBufferRef Palette,FRDGBufferRef Output,
 uint32 DrawCount,uint32 PaletteCount,uint32 FallbackSlot,const FMatrix44f* PostSkinCorrection=nullptr,bool NativeUnitySpace=false);

// LocalVertexFactory streams: float3 position, FPackedNormal tangent X/Z.
// Byte-address outputs must also have VertexBuffer usage. No CPU readback.
KKCROWDGPUPACKING_API bool OKAddRuntimeGpuPackPass(FRDGBuilder& Graph,
 FRDGBufferRef Deformed,FRDGBufferRef Positions,FRDGBufferRef Tangents,uint32 DrawCount);
KKCROWDGPUPACKING_API bool OKGpuPackAvailable();
// Same deformation and packing contract in one dispatch, without the transient
// FOutput buffer. The two-pass API remains an independent GPU oracle.
KKCROWDGPUPACKING_API bool OKGpuDeformPackedAvailable();
KKCROWDGPUPACKING_API bool OKAddRuntimeGpuDeformPackedPass(FRDGBuilder& Graph,
 FRDGBufferRef Source,FRDGBufferRef Draw,FRDGBufferRef Palette,FRDGBufferRef Positions,FRDGBufferRef Tangents,
 uint32 DrawCount,uint32 PaletteCount,uint32 FallbackSlot,const FMatrix44f* PostSkinCorrection=nullptr,bool NativeUnitySpace=false);
