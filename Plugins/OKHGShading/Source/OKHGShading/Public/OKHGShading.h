#pragma once
#include "CoreMinimal.h"
#include "KKRuntimeSharedMaterial.h"

// HG render style. The Koikatsu program of each runtime material family still
// computes the surface (textures, color masks, its own highlights) with its
// self-shadow removed; OKHGRelight.ush then lights that surface with a hard
// ramp shadow, rim and, for hair, an anisotropic band. Authored into separate
// cooked parents (Prepare -HGShading); the game picks a root by ok.RenderStyle.

// What the relight treats the family as. None keeps the Koikatsu result
// (eyes, brows, glasses, effect shaders).
enum class EOKHGSurface:uint8 {None=0,Surface=1,Skin=2,Hair=3};
OKHGSHADING_API EOKHGSurface OKHGSurfaceOf(EKKRuntimeMaterialRecipe Recipe);

// Material parameters the relight reads. Name is the parent parameter,
// Input the custom-node input the shader source uses.
struct FOKHGParameter {const TCHAR* Name;const TCHAR* Input;FLinearColor Default;bool Vector;};
OKHGSHADING_API TConstArrayView<FOKHGParameter> OKHGParameters();
// Per-part switch the game sets on head skin (face lighting).
inline const TCHAR* OKHGFaceParameter(){return TEXT("__OK_HG_Face");}

// The relight statements (Shaders/Private/OKHGRelight.ush), inlined into the
// parent's custom node at authoring time so cooking needs no shader mapping.
OKHGSHADING_API bool OKHGRelightSource(FString& Code,FString& Error);

// 0 = Koikatsu (default), 1 = HG. Console: ok.RenderStyle.
OKHGSHADING_API int32 OKRenderStyle();
OKHGSHADING_API void OKSetRenderStyle(int32 Style);

// The game's own render styles: the shadow ramps its graphics settings offer
// (13 vanilla + Sideloader mods such as Minimalist), exported by
// `Tools/ok-extract ok-ramps` into Content/KKPreview/Ramps (ramps.json). A ramp
// style replaces _RampG on every Koikatsu material and applies live.
struct FOKRenderRamp {FString Key,Name,File;};
// Where ramps.json is (a launch manifest's ramp_catalogue; packaged builds have
// no loose Content). Default: <ProjectContent>/KKPreview/Ramps/ramps.json.
OKHGSHADING_API void OKSetRenderRampCatalogue(const FString& Path);
OKHGSHADING_API const TArray<FOKRenderRamp>& OKRenderRamps();
// Rooted and cached per key; nullptr with Error for an unknown key or bad PNG.
OKHGSHADING_API class UTexture2D* OKLoadRenderRamp(const FString& Key,FString& Error);
// ok.RenderRamp: the ramp key in use, "" = each card's own ramp.
OKHGSHADING_API FString OKRenderRampKey();
OKHGSHADING_API void OKSetRenderRampKey(const FString& Key);
