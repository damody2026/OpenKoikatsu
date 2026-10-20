#pragma once
#include "CoreMinimal.h"
#include "OKSourceRasterState.h"
class FRDGBuilder;class FRDGTexture;class FSceneView;class FMaterial;class FVertexFactoryType;
struct FMeshBatch;
class UMaterialInterface;
// Isolated one-shot actual material/VF draw and GPU readback, not a frame loop.
// IncludeRasterCases returns 4 rows: base, stencil accept/reject, depth-write
// occlusion, and no-depth-write control. Each row contains four texture samples.
KKCROWDGPUPACKING_API bool OKVerifySourceMaterialMeshPixels(UMaterialInterface*,TArray<FColor>&,FString&,bool IncludeRasterCases=false);
// Common shader permutation availability; never compile private card shaders.
KKCROWDGPUPACKING_API bool OKSourceMaterialMeshShadersAvailable(const FMaterial&,const FVertexFactoryType*);
// Diagnostic submission using existing vertex factories/material parameters.
// Caller retains the mesh resources through graph execution. Private targets
// only; this is not yet a cached production mesh pass or scene compositing.
// True means submitted, not that a rendered pixel comparison passed.
KKCROWDGPUPACKING_API bool OKAddSourceMaterialMeshDiagnosticPass(FRDGBuilder&,const FSceneView&,FRDGTexture* Color,FRDGTexture* PrivateDepthStencil,const FMeshBatch&,const FOKSourceRasterBinding&);

// Scene submission retains native vertex factory / primitive identity and view rect.
class FPrimitiveSceneProxy;
KKCROWDGPUPACKING_API bool OKAddSourceMaterialScenePass(FRDGBuilder&,const FSceneView&,FRDGTexture* Color,FRDGTexture* Depth,const FMeshBatch&,const FPrimitiveSceneProxy*,const FOKSourceRasterBinding&);

class FOKSourceMaterialCommandCache;
struct FOKSourceMaterialSceneDraw {const FMeshBatch* Mesh=nullptr;const FPrimitiveSceneProxy* Proxy=nullptr;};
// View-independent material/VF bindings are retained. GPU-scene instance IDs and
// view uniforms are resolved for the current frame; callers never retain proxies.
KKCROWDGPUPACKING_API TSharedRef<FOKSourceMaterialCommandCache,ESPMode::ThreadSafe> OKCreateSourceMaterialCommandCache();
KKCROWDGPUPACKING_API uint32 OKAddCachedSourceMaterialScenePass(FRDGBuilder&,const FSceneView&,FRDGTexture*,FRDGTexture*,TConstArrayView<FOKSourceMaterialSceneDraw>,const FOKSourceRasterBinding&,TSharedRef<FOKSourceMaterialCommandCache,ESPMode::ThreadSafe>);
KKCROWDGPUPACKING_API void OKSourceMaterialCommandCacheStats(const FOKSourceMaterialCommandCache&,uint64& Builds,uint64& Reuses);
