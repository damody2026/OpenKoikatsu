#pragma once
#include "KKRuntimeRenderMesh.h"
#include "KKRuntimeSharedMaterial.h"

// Shadow-only compaction. Display meshes are not rewritten.
// A section joins the solid proxy only when its shadow coverage is the whole
// skinned triangle (no shader discard, no position offset). Masked coverage,
// translucency that still casts, and displacement stay on the original material.
// Workers see facts and mesh arrays only. Material objects are read on the game thread.
inline constexpr uint32 OKRuntimeShadowSectionBoneLimit=256;
inline constexpr uint64 OKRuntimeShadowMaxBytes=256ull*1024*1024;

enum class EOKRuntimeShadowCoverage : uint8 {
 NotCaster=0, OriginalMaterial, SolidOneSided, SolidTwoSided
};
enum class EOKRuntimeShadowSectionKind : uint8 {
 OriginalMaterial=0, SolidOneSided, SolidTwoSided
};

struct FOKRuntimeShadowMaterialFacts {
 EKKRuntimeMaterialRecipe Recipe=EKKRuntimeMaterialRecipe::None;
 EBlendMode Blend=BLEND_Opaque;
 bool BlendObserved=false,TwoSided=false,TwoSidedObserved=false;
 bool WorldPositionOffset=false,PixelDepthOffset=false,Displacement=false;
 bool CastShadowAsMasked=false;
};
struct FOKRuntimeShadowCoverageResult {
 EOKRuntimeShadowCoverage Coverage=EOKRuntimeShadowCoverage::OriginalMaterial;
 const TCHAR* Reason=TEXT("");
};

// Reason is empty only for solid coverage. Every other result names why it
// cannot join the solid proxy.
KKRUNTIMEPOSE_API FOKRuntimeShadowCoverageResult OKRuntimeShadowCoverage(const FOKRuntimeShadowMaterialFacts&);
// Cooked parent for a solid group. Original and non-casters return None.
// One-sided uses M_IBLDepth. Two-sided uses M_IBLOpaque. M_ForgeOpaque is not
// a stand-in: its installed program discards on texture alpha.
KKRUNTIMEPOSE_API EKKRuntimeMaterialRecipe OKRuntimeShadowProxyRecipe(EOKRuntimeShadowCoverage);

struct FOKRuntimeShadowPrepared {
 FKKRuntimeRenderMeshData Mesh;
 TArray<EOKRuntimeShadowSectionKind> SectionKinds;
 TSharedPtr<const FKKRuntimeMouthStream,ESPMode::ThreadSafe> Face;
 int32 SourceCasterSections=0,ExcludedSections=0,SolidSourceSections=0,OriginalSourceSections=0;
 TArray<FString> Reasons;
 FString Error;
};

// Copies caster triangles. Does not weld, drop, or clamp a partial result:
// a budget, palette, or weight failure returns an empty mesh and Error.
KKRUNTIMEPOSE_API FOKRuntimeShadowPrepared OKBuildRuntimeShadowMesh(
 const FKKRuntimeRenderMeshData& Mesh,TConstArrayView<FOKRuntimeShadowMaterialFacts> Facts,
 uint32 SectionBoneLimit=OKRuntimeShadowSectionBoneLimit,uint64 MaxBytes=OKRuntimeShadowMaxBytes);
KKRUNTIMEPOSE_API TFuture<FOKRuntimeShadowPrepared> OKPrepareRuntimeShadowMeshAsync(
 FKKRuntimeRenderMeshData Mesh,TArray<FOKRuntimeShadowMaterialFacts> Facts,
 uint32 SectionBoneLimit=OKRuntimeShadowSectionBoneLimit,uint64 MaxBytes=OKRuntimeShadowMaxBytes);

// The shadow mesh may omit non-casting facial parts. Suppress only absent
// channels; never clamp a pattern on a channel that actually exists.
inline FKKRuntimeFaceState OKRuntimeShadowFaceChannels(const FKKRuntimeMouthStream& Stream,FKKRuntimeFaceState Face){
 if(Stream.PatternCount[0]==0){Face.MouthPattern=-1;Face.MouthOpening=-1;Face.MouthBlend=false;}
 if(Stream.PatternCount[1]==0){Face.EyePattern=-1;Face.EyeOpening=-1;Face.EyeBlend=false;}
 if(Stream.PatternCount[2]==0){Face.BrowPattern=-1;Face.BrowOpening=-1;Face.BrowBlend=false;}
 return Face;
}
