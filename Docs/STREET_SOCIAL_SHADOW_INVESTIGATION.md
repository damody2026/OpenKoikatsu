# StreetSocial VSM marking overflow

Iteration 1 is the original read of the BlendR2 warning. Iteration 2 records the measured stream extents and the shared skin-weighted envelope that now replaces the streamed per-bone sphere. No shadow resolution, page-pool, queue-capacity, or global shadow switch was changed.

## Iteration 1

2026-10-07. Read-only investigation of the Runtime-BlendR2 warning. No renderer CVars, shadow capacity, materials, cards, launchers, or face transitions were changed. No Editor session and no full build were run.

## Logged event

`Saved/StreetSocial/Runtime-BlendR2/game.log:873` (frame 393, 2026.10.07-09.12.36):

`LogRenderer: Warning: [VSM] Non-Nanite Marking Job Queue overflow. Performance may be affected. This occurs when many non-nanite meshes cover a large area of the shadow map.`

That string is emitted once per cache-manager lifetime. `LoggedOverflowFlags` suppresses repeats (`VirtualShadowMapCacheManager.cpp` around the `VSM_STATUS_MSG_OVERFLOW` handler, lines 751–770). One line does not mean one frame. The same run did not log `Non-Nanite VSM page overlap` because `r.Shadow.Virtual.NonNanite.NumPageAreaDiagSlots` defaults to 0 (`VirtualShadowMapArray.cpp:607–611`). The primitive that covered the pages was not named.

Command line was StreetSocial, D3D12, SM6, 1920×1080, `StreetSocialSelfTest`, `RenderOffscreen`, StreetCook05. Epic `ShadowQuality@3` set `r.Shadow.Virtual.ResolutionLodBiasDirectional` and `...Moving` to -1.5 (`game.log:219–238`). Those CVars were not overridden by this project. The renderer adds the camera LOD scale and then clamps a negative absolute bias to 0 (`VirtualShadowMapClipmap.cpp:251–254`). The bias that was actually used was not printed.

All six characters were already GPU-visible by 09.12.21 (`game.log` private-GPU lines and `GPU bank composition`). The first streamed gestures were installed at 09.12.24. A new graphics PSO was recorded at 09.12.35, immediately before the warning. That sequence is consistent with the first non-Nanite shadow draws of the already-installed characters, not with a later one-frame glitch. It does not by itself identify which primitive overflowed.

## What the overflow is

`VirtualShadowMapBuildPerPageDrawCommands.usf`:

- `MAX_SINGLE_THREAD_MARKING_AREA` is 8 pages (line 89).
- `MARKING_JOB_QUEUE_SIZE` is `NUM_THREADS_PER_GROUP * 2` (line 90).
- The culling shader’s group size is 64 (`InstanceCullingLoadBalancer.h:22`, defined as `NUM_THREADS_PER_GROUP` from `InstanceCullingLoadBalancer.cpp:9`). The queue therefore holds 128 jobs.
- One thread handles one load-balancer item. Items share a group of 64. Each item may enqueue one job per clipmap view per mip (`usf` lines 292–344).
- A job is large only when the page rectangle, after clipping to that mip’s uncached pages, has area greater than 8 and `MarkPageDirtyFlags != 0` (lines 331–343).
- `InterlockedAdd` returns the index before the increment. Index 128 is the 129th large job and sets `bMarkingJobQueueOverflow`.
- On overflow the shader does not drop the draw. The overflowing item marks pages on the issuing thread (lines 346–360). The warning is a marking-performance failure, not a removed shadow. Raising the queue would be an engine shader/capacity change and was not done.

Page size is 128 texels. Level 0 is 128×128 pages, 16384 texels on a side (`VirtualShadowMapDefinitions.h:12–20`). Eight pages is about a 3×3 page footprint.

Directional clipmaps use absolute levels. Default first level is 6 and last level is 22 (`VirtualShadowMapClipmap.cpp:57–67`). `GetLevelRadius` is `2^(level+1)` cm (lines 175–179). The ortho width used for level 6 is `2 * radius` on each side of the center (`HalfLevelDim = 2 * RawLevelRadius`, passed to `FReversedZOrthoMatrix`), so that level’s virtual map spans 512 cm. A caster whose uncached footprint is only ~12 cm on that level already exceeds 8 pages on mip 0. Coarser levels and higher mips shrink the footprint. Deforming instances set invalidate/dirty flags (`usf` lines 273–274 and 330–336), so an animated caster rebuilds this footprint instead of staying cached.

`r.Shadow.Virtual.MaxPhysicalPages` 4096 in the same scalability block is the page pool. This warning is not the page-pool overflow string. The pool did not overflow in this log.

## Scene light

The authored map has one directional light and one sky light (`OKStreetSocialScene.cpp:419–420`). The sun is movable, intensity 2.5, rotation (-45, -30, 0), and is the atmosphere sun. Relight keeps that mobility (`OKStreetSocialScene.cpp:403–405`). A sky light does not build this directional clipmap. No local lights are spawned by the map builder.

Movable does not by itself select the non-Nanite queue. It does select the moving directional LOD bias until the light is considered inactive. The sun’s transform is not animated after spawn. Whether it had already settled to the non-moving bias at frame 393 was not logged.

Ground actors with `Z < 10` do not cast (`OKStreetSocialScene.cpp:403` and `:418`). The 49 basic-shape replacements are Nanite meshes (`OKStreetSocialScene.cpp:383–398`). Nanite casters do not use `CullPerPageDrawCommandsCs`. This log does not prove the cooked StreetCook05 map still had only those Nanite meshes at runtime. A leftover non-Nanite prop larger than a body would also be a candidate. The page-area diagnostic below is what separates that from the characters.

## Who casts in the street runtime

`OKCrowdRuntimeScene.cpp:643–648`:

- Shadow test distance is 1600 cm from the view. Street social is externally controlled, so the camera is the player camera, not the runtime demo camera.
- The hidden CPU poseable is forced not to cast.
- The visible GPU `UInstancedSkinnedMeshComponent` casts only inside that radius.
- Before the aggregate install, the loading poseable can cast inside the same radius. By frame 393 the aggregate bank had been installed and dialogue was playing, so the GPU components were the casters.

`Runtime-BlendR2/runtime.json` merged section counts for the six characters are 20, 22, 31, 43, 42, and 49. `OKCreateRuntimeRenderMesh` sets `bCastShadow = true` on every section (`KKRuntimeRenderMesh.cpp:245`). Each section is its own non-Nanite shadow draw of the same instance bounds. Depth-pass GPU components are created with cast shadow off (`OKCrowdRuntimeScene.cpp` depth-GPU setup). They are not a second shadow copy unless some later call enables them; the near-shadow update only enables `E.Gpu`.

Six characters inside 16 m therefore submit on the order of 200 non-Nanite shadow draws (sum of those section counts is 207), grouped up to 64 draws at a time. One character with 49 sections already fills most of a group.

## Bounds that those draws use

GPU instances do not use the skeletal asset bounds when the provider reports animation bounds. `UOKIndependentTransformProvider::GetAnimationBounds` returns the diagnostic box for animation index 0 only (`OKCrowdScene.h:36–38`). Instances are added at index 0. `FInstancedSkinnedMeshComponentHelper::GetComponentDesc` writes that box as per-instance local bounds (`InstancedSkinnedMeshComponentHelper.h:119–130`). The VSM shader starts from `InstanceData.LocalBoundsCenter/Extent` and may replace them from the dynamic-mesh-bounds buffer (`VirtualShadowMapBuildPerPageDrawCommands.usf:209–211`, `DynamicMeshBounds.ush:13–27`).

The box is a sphere about the component origin:

- Private bank, per vertex, triangle inequality over weighted bones (`OKCrowdRuntimeScene.cpp:458–468`). Radius is that maximum plus 1 cm. `FBox(-Extent, Extent)`.
- Streamed gesture, coarser per-bone bound: accumulated `|translation|` plus `scale * bind radius`, again plus 1 cm (`OKCrowdRuntimeStream.inl:110–123`).
- On install, `AnimationBounds += streamed box` and the provider is updated (`OKCrowdRuntimeStream.inl:83–86`). Restore of the base clip does not shrink this box. The first gesture install in the log is 09.12.24, before the warning.

Palette columns are parented to bone 0 and stored as `OKSkinPaletteColumn` transforms: translation is the matrix origin, and one axis scale is `3 * columnLength` (`KKRuntimePoseMath.h:28–33`, `KKRuntimeRigMetadata.cpp:37`). Mesh weights are `W * 21845` on each of the three axes so the three weights sum to 65535 (`KKRuntimeMesh.cpp:102–130`). The per-vertex bank loop cancels that 3 with the one-third weight and is a conservative bound of `|origin| + sum |column| * |bind component|`. The stream loop does not apply the one-third weight, so it is looser. Neither number was logged. Imported mesh bounds additionally union bind-space `Position` with `InitialPosition` (`KKRuntimeRenderMesh.cpp:238–239`) and are not what the GPU shadow instance uses once the provider box is valid.

A body-sized footprint is already a large job on the finest clipmaps. A loose multi-meter sphere is a large job on more levels and more mips. Both, multiplied by 20–49 sections in one group of 64, can exceed 128 large jobs. Which of those two is what happened is not measured. Tightening the stream union would be a general bounds change, but it is not justified yet: it may still leave the finest clipmaps over 8 pages for every section, and an under-bound would clip shadows.

## What is not claimed

- No measured radius, page count, clipmap level, or primitive name.
- No claim that the Nanite street replacement failed, and no claim that it is the caster. The overflow path cannot see Nanite draws.
- No claim that the -1.5 scalability bias was the runtime bias. It is clamped with the camera scale.
- No claim that shadows were missing. The shader still marks overflowing jobs on the issuing thread.
- The hundred-person crowd warning in `CROWD_GPU_POSE_ERRORS.md` is a different scene. It was not used as evidence for this street frame.

## Measurement the next runtime should record

One street run, same executable and map, without changing resolution, page pool, or the near-shadow distance. Add only diagnostics:

1. Set `r.Shadow.Virtual.NonNanite.NumPageAreaDiagSlots` to 4 for that run. Record every `Non-Nanite VSM page overlap` line (primitive name and page count) from the same frame window as the marking warning.
2. Log, once per character at GPU registration and again on each stream bounds union: diagnostic box extent in cm, imported mesh sphere radius in cm, merged section count, and whether `CastShadow` is true. Do not print this every frame.
3. Log the directional light’s VSM `ResolutionLodBias` after `FVirtualShadowMapClipmap` finishes the first view that contains the characters, plus clipmap first/last level.

Interpretation: if the named primitives are the runtime GPU components and the page counts stay large while the diagnostic extent is only a body sphere, the overflow is section count times the finest clipmaps, and a safe fix has to reduce shadow draws per character without dropping casters. If the diagnostic extent is many times the imported radius, the stream union is the coverage bug and a conservative per-vertex bound can replace it. If the named primitive is a static street mesh, the Nanite cook did not remove that caster. Until one of those three is logged, do not change bounds, section shadow flags, or shadow CVars.

## Iteration 2 — measured stream envelope

`Saved/StreetSocial/Runtime-ShadowBoundsR1/game.log` names the casters. Page-overlap lines are `InstancedSkinnedMeshComponent` primitives on `OKStreetSocialScene_0` (first at line 843, 943 pages). The stream install log is origin-centered and only grows.

First install, then the later maximum union, half-extent in cm:

| Character | Sections | Bank before | First stream | Largest union |
| --- | ---: | ---: | ---: | ---: |
| selection-000 | 20 | 256.096 | 427.255 | 427.255 |
| selection-001 | 22 | 497.125 | 909.913 | 911.376 |
| selection-002 | 31 | 244.512 | 481.620 | 502.238 |
| selection-003 | 43 | 457.934 | 1422.968 | 1430.966 |
| selection-004 | 42 | 428.959 | 671.435 | 671.435 |
| selection-005 | 49 | 269.325 | 689.662 | 691.149 |

The bank extent is the per-vertex weighted radius. The stream extent was `max |translation| + max |scale| * full inverse-bind radius` for every bone, including a palette column whose other two scales are zero. That is the coverage inflation. It does not by itself prove the marking queue will go quiet after the sphere shrinks: section count times the finest clipmap can still exceed 8 pages.

`OKRuntimeSkinEnvelope.h` is the bank formula, used by both the initial private bank and each streamed action. The game thread copies parents, inverse binds, and compact position/weight vertices into a bounded plain snapshot (`OKRuntimeSkinEnvelopeMaxBones` 65536, `OKRuntimeSkinEnvelopeMaxVertices` 2000000, 256 MB). Workers do not touch the skeletal mesh. A cap or a non-topological parent fails the character or that action. There is no per-bone-sphere fallback, no per-frame skinning, and no radius clamp. The install line `APNG shadow stream bounds` and `AnimationBounds += streamed box` are unchanged, so a new action cannot shrink the bank sphere. Root review corrected the streamed input to compute joint per-channel maxima over the base/idle clips and incoming action. Local hierarchy crossfade can exceed the union of separately evaluated endpoint spheres; the added complementary parent-scale/child-translation regression proves this case. The published box is still that radius plus 1 cm.

`OpenKoikatsu.Runtime.SkinEnvelope` checks containment for partial weights, a zero scale axis, an unweighted translation, a weighted accessory, non-uniform parent scale, inverse bind, rotation, palette columns, key interpolation, and crossfade. The zero-scale, partial-weight, unweighted-bone, and column cases must also be strictly inside the old full-radius sphere. Worker did not execute this test. Root subsequently compiled Development StreetShadow03 and Editor-streetshadow22, then Automation-Shadow02 passed all five tests including SkinEnvelope and the added hierarchy crossfade case.


## Root measured correction
ShadowTightR1 passed the five-NPC dialogue/voice/private dual-action/face/relationship/cancellation/save assertions. Final halfextent before -> after, cm:

| Character | Previous | Corrected |
|---|---:|---:|
| male | 427.255 | 258.689 |
| C000 | 911.376 | 497.125 |
| C019 | 502.238 | 251.945 |
| C044 | 1430.966 | 460.808 |
| C022 | 671.435 | 432.702 |
| C068 | 691.149 | 286.413 |

No per-card special cases, geometry changes, shadow quality changes or engine queue resizing. The VSM marking overflow still exists, so this is a verified envelope improvement, not a claim of complete shadow acceptance. Next renderer work should target repeated section shadow draws without dropping masked silhouettes. The initial bank envelope remains conservative and larger than the actual mesh; no heuristic size clamp applied.

Bridge jobs: investigation run-muxwepws-0cd9ie (10m21s, 418601 input / 6216832 cached / 37502 output, reported USD2.8360284); follow-up run-muxwyp5z-hdd3id (13m13s, 226526 input / 6203136 cached / 76636 output, reported USD2.77000788), same thread71ad93d4-df5a-425a-93f9-578bbcf21b11, grok-4.7-build-fast. Root reviewed source, fixed crossfade safety and host integration, and independently built/tested. HEAD and branch unchanged; no commit/push.

## StreetShadow03 root acceptance
Development StreetShadow03、Editor-streetshadow22、Automation-Shadow02五項（含包含範圍與階層交叉淡化）、Python26、Runtime-ShadowTightR1/R2全通過。公開StreetSocial CMD receipt已原子更新，ValidateOnly成功。R2六人1080p離屏10000samples平均4.956ms／P958.304ms／全部顯示10.244s；GT2.134／RT4.934／RHI1.404／GPU1.904ms，階段不能相加。相較前次存在負載與測試波動，不宣稱受控FPS提升。串流bounds修正已實測有效；VSM overflow仍記錄一次（引擎只報一次不代表只發生一幀）。完整視窗／CPU-GPU外觀及陰影提交驗收仍未完成。原百人CMD未改。
