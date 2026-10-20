# StreetSocial VSM follow-up

2026-10-07. No runtime change. SubmitR4 already removed the compatible solid draws and the loose stream envelope. The marking warning is still the non-Nanite job queue, and neither a tighter conservative box nor the existing same-material pack can close it.

## What SubmitR4 measured

`Saved/StreetSocial/Runtime-SubmitR4/game.log:905` is the one engine warning. The same log has no `Non-Nanite VSM page overlap` lines, so page area was not re-sampled after the envelope fix. `runtime.json` is the caster census. Display casting is off only while a proxy is active, so the live count is 144, not the 154 source casters.

| Card | Live caster sections | Proxy | Half-extent cm | Imported sphere radius cm |
| --- | ---: | --- | ---: | ---: |
| male | 10 | no | 258.689 | 158.925 |
| C000 | 12 | no | 497.125 | 162.608 |
| C019 | 23 | no | 251.945 | 156.717 |
| C044 | 30 | yes | 460.808 | 157.016 |
| C022 | 34 | yes | 432.702 | 162.841 |
| C068 | 35 | yes | 286.413 | 156.439 |

10+12+23+30+34+35 = 144. The three proxies are the only reductions (33→30, 35→34, 41→35). Every published box is a cube: `OKRuntimeSkinEnvelopeBounds` is `±(radius+1)` on all three axes. `unique_material_bindings` on the heavy cards are 36, 36, and 40. `shadow_reasons` are only `shader coverage clip` and `translucent material does not cast a masked shadow`.

Frame times from the submission record, not re-measured here: mean 5.034 ms, P95 10.281 ms, six characters visible 8.464 s. That is not a controlled comparison.

## Where the queue fills

`VirtualShadowMapBuildPerPageDrawCommands.usf` still has the 8-page single-thread limit and a 128-job group queue (`NUM_THREADS_PER_GROUP` 64, queue `64*2`). One thread handles one load-balancer item. A directional clipmap is not a mip chain: `GetRenderViewCount` returns `{level count, 1}` (`VirtualShadowMapArray.cpp`). Defaults are levels 6 through 22, so one sun contributes 17 primary views and each view has one mip (`AddRenderViewsClipmap` sets `TargetMipCount = 1`). The street builder has one directional light and no local lights. `DefaultEngine.ini` does not override clipmap levels.

Each view is an ortho whose half-width is `HalfLevelDim = 2 * GetLevelRadius(level)` and `GetLevelRadius = 2^(level+1)`. Level 6 therefore spans 512 cm across the 16384-texel virtual map (`ViewRect` is the full virtual map). A page is 128 texels, 4 cm on level 6, and doubles each coarser level: page size = `2^(level-4)` cm. `ShadowQuality@3` asks for resolution bias -1.5, but `FVirtualShadowMapClipmap` clamps a negative absolute bias to 0, so that scalability value does not shrink the page.

A job is large when the uncached page rectangle has area greater than 8. For a square footprint of world size S that is about `S > 2.83 * 2^(level-4)`. Using S = the full cube edge (`2 * half-extent`), which understates a rotated cube:

| Card | Sections | Large levels at the published cube | Jobs if those draws share a group |
| --- | ---: | ---: | ---: |
| male | 10 | 6–11 (6) | 60 |
| C000 | 12 | 6–12 (7) | 84 |
| C019 | 23 | 6–11 (6) | 138 |
| C044 | 30 | 6–12 (7) | 210 |
| C022 | 34 | 6–12 (7) | 238 |
| C068 | 35 | 6–11 (6) | 210 |

C019, C044, C022, and C068 each exceed 128 jobs without any other caster in the group. A full group is 64 draws. The 144 live draws fill such groups. Overflow marks on the issuing thread; it does not drop the shadow. `r.SkinCache.CompileShaders` stays at the engine default 0 and this project does not set it, so `GetGPUSkinTechnique` stays inline. Dynamic mesh bounds are allocated only for the skin-cache technique, and the scene proxy wires `DynamicMeshBoundsIndex` only then. Marking therefore uses the provider cube, the same cube `runtime.json` prints, for every section of that component.

## Why a tighter box does not remove it

The scalar envelope is already the radius of an origin-centered ball. Its tightest axis-aligned box is the cube that is published. A smaller box would cut that ball. An axis-aligned box of the real motion would require rotation limits. The hierarchy crossfade case already shows that the union of endpoint poses is not a container; the current radius exists so `|lerp|` of keys and that crossfade stay inside. Replacing it with key samples, a radius clamp, or the imported ~157 cm sphere would be the rejected under-bound.

Even a box that only contains the imported mesh is not enough. Radius ≥ 156 cm forces an origin-centered cube of at least that half-extent, which is still large on levels 6–10 (five levels). Thirty sections times five levels is 150 jobs.

A non-cubic body box does not change the group result either. A 160×40 cm footprint is still large on levels 6, 7, and 8 (level 8 pages are 16 cm; 160/16 × 40/16 = 25 pages). Level 9 can fall to about 6 pages and become a small job. Three large levels times a full group of 64 is 192 jobs. Every section shares one instance bound (`GetAnimationBounds` is per animation index, and these instances use index 0), so a small accessory marks the whole character box too. Per-section bounds are not available on this inline path without a new component per section or an engine slot.

GPU skin-cache bounds, if that path were already the one drawing, are same-frame: the compute shader integer-bounds the skinned position in the same dispatch, the buffer is transitioned to a shader read before graphics, and instance culling waits on the async-compute skin dispatch. A slot that was not updated keeps `min > max` and the shader falls back to the instance box. That path is not active here. Turning it on would switch these components from inline bone fetch to the skin-cache vertex factory. That is a different deform path, not a bounds write, and it still would not get a 64-wide group under 128 while three clipmap levels stay large.

## Why another merge is not available

`OKBuildRuntimeShadowMesh` already packs same-material original sections and all solid sections, and it splits only on the existing 256-bone palette. The no-gain rule then keeps the displayed caster when the proxy is not strictly smaller. The leftover reasons are coverage clip, not an unpacked duplicate material. Merging those clipped sections into `M_IBLOpaque` / `M_IBLDepth` changes the silhouette and was the rejected compatibility experiment. Translucent sections are already omitted because those parents do not cast a masked shadow.

## Blocker

With one directional clipmap, inline skinning, and the published cubes, four of the six characters each produce more than 128 large marking jobs. With any box that still contains a body of the imported size, levels 6–8 stay large, and 64 such draws produce 192 jobs. The live set is 144 draws. No semantic-preserving bound, and no further same-material pack, gets a full culling group back to 128.

## Bounded next step

Do not repeat a radius clamp, a per-card section exception, a skin-cache switch, or a queue CVar. Do not split casters into more components unless a counted section census shows that enough of the 30–35 clipped sections are individually under about 12 cm on level 8 (the size that stops being a large job there). The shadow builder can report that census from data it already has: per original-material bucket, recipe id, section count, and the same envelope radius restricted to that bucket's vertices, without installing the result and without dropping triangles. If every clipped bucket is still large on levels 6–8, and the heavy cards still have about thirty of them, the census is the stopping proof. If some buckets are small and numerous, only then is a per-bucket shadow component with that bucket's own envelope a candidate, and it still has to keep the face stream, reveal, near-camera 1600 cm policy, and teardown on the new components.

Root's optional page-area log (`r.Shadow.Virtual.NonNanite.NumPageAreaDiagSlots` 4, test process only) would replace the geometric estimate with measured uncached page counts. It is not required to reject another bounds or compatibility edit.

No editor, cook, or automation was run for this follow-up. No source file other than this note was changed.
