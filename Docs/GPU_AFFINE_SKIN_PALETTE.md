# GPU skin affine palette parity

2026-10-05. Scope: native APNG reader/runtime skeletal skin display, including face morphs. This is a correctness adapter for the existing GPU vertex skinning path; it does not move animation evaluation to GPU or complete the independent crowd provider.

## Root cause and implementation

The source multiplies full matrices along the bone hierarchy. Unreal's component-space `FTransform` composition cannot represent shear produced by non-uniform scale combined with rotation. Matching bone names, weights, morphs and eye UVs therefore did not ensure matching geometry. Augusta's iris differed by about .171/.142 cm even before adding morphs; source reference-vertex skinning matched its baseline to about .000031 cm.

The two owned runtime skin components now publish per-component affine palette snapshots through the existing virtual `GetRefPoseOverride` renderer interface. For row-vector matrices:

```
effective inverse = source reference inverse * exact current world * engine TRS world inverse
engine GPU palette = effective inverse * engine TRS world
                   = source reference inverse * exact current world
```

- Reference geometry, source local animation poses, mesh weights, materials and morph deltas are retained. Morph deltas still precede skinning.
- Each newly submitted snapshot is immutable; a previous snapshot may remain referenced by renderer dynamic data. Engine source `FSkinnedMeshSceneProxyDynamicData` captures this override when dynamic data is constructed.
- Poses which have not changed reuse their snapshot. The final implementation accumulates only used bones and their ancestors, and inverts only used bone carriers. All LOD BoneMaps contribute to the used set.
- Legal zero scale makes the engine carrier singular. Only used render bones receive a unit-scale carrier in this case; the effective inverse supplies the original singular source matrix and preserves collapsed geometry. Unused bones, including the dummy pose leader's unused face bones, are not adjusted.
- CPU socket/debug transforms remain engine TRS representations. The carrier scale on a collapsed used render bone is not the source scale; exact geometry comes from the palette. This does not claim general physics/socket affine parity.
- Explicit external reference-pose overrides and leader-pose components retain their existing behavior; this adapter does not replace an explicitly supplied override.
- Bounds use the same effective palette and morph envelopes as rendering.
- No card names/IDs, common animation assumptions, shared-pose assumptions or hardware branches are used. The failed origin-only experiment and its environment switches were removed; the correction runs by default on these runtime components.

## Functional evidence

- `GpuAffinePaletteAcceptanceR3`: March 7th, Alice and Augusta, outfit 0, valid=1/exit=0. Source-palette reference-vertex gate .003 cm, original eye UV, outline and renderer bounds gates pass.
- With the same `CpuAffineGeometryReferenceR1` images, eye ROI MAE (/255) improves: March 7th 1.987 → .283; Alice 3.174 → .399; Augusta 9.903 → .268. Actual Augusta CPU/GPU portraits were viewed; eyes and face contours closely align. These figures describe image differences, not pixel identity.
- `GpuAffinePaletteDefaultOutfitsR1` and `CpuAffinePaletteDefaultOutfitsR1`: 21 outfits, including mouth .65, eye .35, brow .6 and combined gaze. Both valid=1/exit=0, 21 matching images. 294 reference-vertex skinning checks, maximum error .000250651 cm; 42 eye UV and 21 face-outline checks pass.
- `GpuAffinePaletteOutfitsComparisonR1`: eye ROI MAE March .384–.389, Alice .398–.475, Augusta .357–.461 /255.
- `GpuAffinePaletteWalkR2`: three characters, 12 successive source walk pose updates on each loaded actor to .37 seconds; valid=1/exit=0. This checks changing local poses and cached palette invalidation, not all clips or loop seams. `CpuAffinePaletteWalkR1` is excluded as a visual reference: its animation clock did not advance despite the audit returning valid=1.
- `GpuAffinePaletteDefaultBuildR2.log`: ordinary Development Editor build succeeded for the correctness adapter. Final used-bone refinement follows this evidence and requires its own isolated build/test evidence below.

The reader audit's `OK_READER_REQUIRE_SKIN_PALETTE=1` requires reference-vertex GPU skinning to match `OKDeformFromRef` within .003 cm. It keeps morph differences separate and compares only used bones in matrix statistics. `OK_READER_WALK_PROBE_TIME=.37` exercises repeated source walk pose changes before capturing.

## Limits and reproduction

These are functional image runs, some concurrent. They are not FPS profiles or Development 120 FPS/8.33 ms acceptance. Additional CPU matrix work exists when poses change; final crowd profiling must include it if this runtime component path is used. Cooked crowd native components and GPU-only providers are separate integrations.

`Tools/sync_gpu_skin_audit_host.ps1` refreshes only the owned isolated host's DLLs/manifests after a completed ordinary build. It rejects a running host, linked binary directories and incompatible engine/project BuildIds. Other editors and hosts are not modified.

`Tools/build_gpu_skin_audit_plugin.ps1` is an isolated compile/link fallback using the last successful UBT response files and generated code. It writes private objects, dependencies, diagnostics and DLLs under a fresh test tag, without bypassing Live Coding to overwrite loaded project binaries. It does not run UHT or replace an ordinary full build; reflected/API changes require normal UBT validation.

Errors and rejected approaches are preserved in `CROWD_GPU_POSE_ERRORS.md`.

## Moving reference follow-up

`GpuAffinePaletteFinalBuildR2.log` and `CpuAnimationReferenceBuildR1.log` confirm ordinary Development Editor compilation succeeded after the Live Coding mutex was absent. No visible Live Coding panel was observed or required.

`GpuWalkParityCorrectClockR1` and `CpuWalkParityCorrectClockR1` both actually advance to .37 seconds. Their initial portrait comparison is not accepted: full RGB MAE ranges from 26.83 to 43.46 /255. Inspection shows substantial hair/accessory placement differences. These runs establish clock advancement, not image parity.

The initial comparison camera still used a shared-skeleton deformation helper on CPU prefab-local vertices and bone indices. That is not a common coordinate-space oracle. Portrait framing now uses the native part skin palette and raw mesh vertices in both modes, includes signed influences, and logs the focus/distance/yaw. `PortraitNativeCameraBuildR1.log` confirms compilation. Fresh paired evidence is required before attributing the remaining differences to renderer geometry.

Subsequent changes restore CPU full-head kind-3 skinning and canonical raw morph deformation through the actual private part palette. These do not by themselves resolve the observed moving-image difference (`WalkNativeReferenceComparisonR2` remains rejected). Shared portable GPU playback also needs explicit current vertices for kind-4 procedural hair/accessories: RefreshSkinnedDisplay now invokes native Skin, whose OK_SKELMESH guard skips GPU body/head skinning. Final independent all-GPU crowd animation remains a separate path and this change is not a frame-time claim.

`GpuPrivateRenderRefreshBuildR1.log` failed linking because a root editor held the DLL; no editor was stopped. A private build succeeded, then a later ordinary retry `GpuPrivateRenderRefreshBuildR2.log` succeeded when the lock was absent. This distinguishes a DLL lock from the earlier UBT Live Coding guard and from a visible UI panel.

### Current moving image result

`GpuWalkPrivateRefreshR3` and `CpuWalkPrivateRefreshR3` use the final ordinary build, three cards/outfit 0, .37 seconds after 12 explicit ticks, mouth .65, eye .35, brow .6 and combined gaze. Both exit 0 / valid=1. The GPU matrix, UV, outline and bounds gates pass. The actual Alice and Augusta comparison montages were inspected: the large hair/accessory pose discrepancy is gone and the facial features align closely.

`WalkPrivateRefreshComparisonR3` RGB MAE /255:

| Card | Full image | Eye ROI |
| --- | ---: | ---: |
| March 7th C001 | .1147 | .1815 |
| Alice C002 | 1.0765 | .4787 |
| Augusta C050 | .2331 | .4747 |

Alice still has visible differences in the blue upper hair ornaments, explaining the larger full-image value; this is not pixel identity or full asset acceptance. The three current outfit-0 images are moving functional evidence; the older 21-outfit static results do not automatically validate every outfit with the latest changes. These runs are not profiles, do not provide average/P95 frame times, and do not establish Development 120 FPS or full independent crowd GPU animation parity.
