#pragma once
#include "CoreMinimal.h"
#include "RHI.h"

// Unity pass numeric enums. Private depth/stencil targets are required: these
// masks must never overwrite Unreal's reserved scene-stencil bits.
struct FOKSourceRasterState {
 bool DepthWrite=false,Stencil=false;
 int32 DepthCompare=4,StencilCompare=8,StencilFail=0,DepthFail=0,StencilPass=0;
 int32 StencilReadMask=255,StencilWriteMask=255,StencilRef=0;
 int32 SrcBlend=5,DstBlend=10,SrcAlphaBlend=5,DstAlphaBlend=10;
};
struct FOKSourceRasterBinding {
 FDepthStencilStateInitializerRHI DepthStencil;
 FBlendStateInitializerRHI Blend;
 uint32 StencilRef=0;
};
// Reverses only depth comparisons for Unreal reversed-Z, never stencil.
KKCROWDGPUPACKING_API bool OKPrepareSourceRasterState(const FOKSourceRasterState&,FOKSourceRasterBinding&,FString& Error);
// Diagnostic only: actual raster draws and asynchronous GPU pixel readback.
KKCROWDGPUPACKING_API bool OKVerifySourceRasterGpu(TArray<FColor>& Pixels,FString& Error);
