# Independent GPU animation implementation and validation plan

Status: independent GPU compute foundation, native private-animation preparation and render-thread-owned persistent GPU resources/current-previous pose API are implemented. RealVerticesR1 validates ten real independent cards against native compressed poses/FTransform hierarchy and editable low skin vertices; synthetic matrix/native regression also passes. Cooked-game rendering integration, streaming and final quality/loading/FPS acceptance remain pending. No new game performance measurement is claimed.

## Contract

100 player-selected visually distinct characters. Assume no animation, skeleton, bind matrix, private rig or pose sharing between characters. Each character independently selects among up to 20 walk clips, phase, rate and transition state. Batching compute dispatches may process unrelated data; it must not deduplicate or substitute a common pose. Maintain current game visuals, 60 Hz animation, native resolution, current near-face/hair detail and nearby shadows. No hardware-model, affinity or priority tuning. Initial load contains low geometry/textures and active clips only; preserve all outfits in the source card. Target: cooked Development, initial scene ready under 60 seconds and stable frame mean/P95 <= 8.333 ms. Prepared warm load and first-time conversion are separate measurements.

## 1. Baseline and data audit

- Profile the adopted CPU path: exact constant-scale cache on, Development debug-pose copy off, CPU provider mode 0. Old full-copy traces are not the current baseline.
- Record CPU decode, hierarchy, private-palette/current-previous preparation, per-component update and Render Thread waits; record GPU/RHI and memory.
- Inventory cooked clip codecs, keys, bones/parents, section mappings, inverse bind transforms, root motion, curves/morph dependencies, current/previous poses, bounds and low/near asset relationships. Identify current affine/shear-preserving three-bone representation before choosing GPU interpolation.
- Inventory actual distinct walk clips. Do not claim a 20-style test if only one source walk is available. Structural tests with independent buffers/phase IDs are not motion-diversity validation.
- Calculate resident/upload bytes for active clips, transitions and a full 20-clip per-character library before implementing the streaming budget. Two active clips per person may dominate memory; do not assume it is cheap.

## 2. Independent GPU pose oracle (1 character, then 10)

- Introduce versioned immutable GPU animation data: independent owner/clip IDs, track offsets/key times/data, per-character hierarchy, bindings/private mappings and compatibility metadata.
- Start with supported cooked Bitwise clips or a compact import-time key representation. Audit all source format variants before implementing direct compressed decoding. Unsupported data keeps the existing CPU path with an explicit reason/counter.
- Compute local sampling, supported retarget/affine semantics, hierarchical transforms and skin matrices. Preserve the current representation; ordinary TRS reconstruction is not assumed equivalent to arbitrary affine matrices.
- Use RDG-managed buffers and explicit dependencies. Maintain current and previous GPU poses; no frame-time readback. Diagnostics may read back asynchronously in correctness runs only.
- Compare against the adopted CPU path at keyframes, between keys, reverse/rate changes, loop seam and clip transitions. Report finite checks, max/RMS matrix and skinned-vertex error, worst bone/part and source clip. Set explicit numerical tolerances after determining float-conversion baseline; never silently loosen them to pass.
- Test deterministic transitions and root-motion ownership. Apply gameplay/world movement once. Retain authoritative CPU gameplay/collision queries or hybrid bone queries where needed; their costs must be reported.

## 3. Integrate actual rendering (1, 10, 100)

- Feed GPU-produced per-character matrices into the existing compatible skinning/render path through an audited provider/vertex-factory integration. Existing InstancedSkinnedMesh prototypes are a reference, not proof of current low/near compatibility.
- Preserve materials, sections, outlines, alpha cutouts, private hair/clothes/accessories, current shadows, normals/tangents and motion vectors. Different meshes still have separate render submissions.
- Remove CPU pose/palette computation only for verified GPU-supported components. Account for followers and near parts; moving matrix packing alone is insufficient.
- Add explicit feature flag and counters: requested/executed GPU actors/components, CPU fallback reasons, bytes uploaded, resident animation bytes, transitions, GPU pass timings and remaining CPU pose evaluations. Game default remains CPU until gates pass.
- Bounds must conservatively cover clips/transitions without full pose readback. Validate camera proximity/near clipping and low-to-near-to-low changes.
- Numerical readbacks, traces and moving-camera functional diagnostics are excluded from FPS acceptance.

## 4. Independent multi-clip streaming and gameplay

- Per character: independent current/next animation IDs, times, rates, blend duration and GPU offsets. Request next clip asynchronously; transition only when resources are ready. Continue current motion while loading.
- Retain current/next/in-flight render resources until GPU completion; generation IDs prevent stale async results being assigned to a replaced card/clip. Enforce byte-based cache limits with active clips protected.
- Start with two real clips per character, then 20 real styles where assets exist. Randomize style, phase and rate with reproducible seeds. Test mixed lengths and selection changes.
- Motion-specific hair/clothing physics must come from validated per-character data or a compatible solver. Do not reuse one walk's baked private motion across unrelated styles without testing.
- Cold/warm load tests include process start, active animation resource readiness and first correctly animated frame. Runtime first conversion/import is reported independently. High assets remain on demand.

## 5. Quality and performance acceptance

- Visuals: near player and multiple other cards, side view with legs spread, hair edges, garment penetration, face/eyes/outlines, current shadows; test animation/LOD changes and loop/transition boundaries.
- Functional: WASD, idle/diagonal motion, camera movement, near round-trip, arbitrary card order, clip eviction/reload, actor removal, unsupported fallback and unavailable target clips.
- Benchmark cooked Development at the same third-person scene/camera/resolution/quality/60 Hz and matched nearby detail/shadow populations. Run CPU/GPU/CPU and reversed GPU/CPU/GPU, warm before bounded samples, record mean/P95 and per-thread/GPU timings plus memory/load. No builds/imports/audits overlap timing.
- Repeat with multiple reproducible 100-card selections and genuinely diverse styles. Record environment interference and reject drifting baselines; do not claim a universal gain from one selection.
- Adopt only if correctness/quality/loading pass and performance benefit exceeds observed baseline drift without harmful GPU/memory/stutter regressions. Target120 requires both mean and P95 <= 8.333 ms, not average FPS alone. If unmet, profile the integrated path before the next change.

## Deliverables

Versioned animation data/exporter, opt-in GPU runtime/shaders, correctness oracle, streaming/transition tests, benchmark wrappers/report fields, CPU fallback, reproducible build/stage and evidence. Append failures and corrective rules to Docs/CROWD_GPU_POSE_ERRORS.md. This plan does not claim implementation, 20 available styles or new FPS results.

## Residency milestone update

Completed persistent resource ownership and current/previous GPU pose references. ResidentFinalR1 covers 100 independent synthetic actors with 20 owned clips, independent A/B selection, repeated-pose reuse, explicit history reset, invalid inputs and release/reload. RealResidentR1 verifies ten actual private rigs and 1,372,332 editable low-vertex comparisons; one immutable upload, six parameter-only updates (960 bytes total), current/previous pose error <=0.000126362. No source assets or staged gameplay defaults were changed. Current stage functional verification passes.

Next actionable implementation: connect these verified GPU component matrices to the actual renderer's per-section inverse-bind/bone palettes and previous transforms, introduce an opt-in registry with lifetime/generation handling and conservative bounds, then remove CPU evaluation only for supported components. Validate one rendered low/near character before ten and one hundred. Cooked data preparation and active-clip streaming still need implementation. Final visuals/load/FPS acceptance is pending; no hardware tuning or new FPS claim.

## GPU palette milestone and required performance comparison

SkinPaletteFinalR1 completes persistent per-mesh mapping/bindings and GPU current/previous float32 renderer-format output. Ten actual independent rigs, 593 section BoneMaps, 45,834 palette entries and 6,600,096 matrix elements pass; 1,372,332 low-vertex comparisons also pass. Arbitrary item order/nonzero output offsets preserve guard regions; five malformed data and six runtime cases are rejected. Runtime gameplay provider/registry, CPU evaluation removal for supported components, bounds and near/LOD/shadow integration remain open.

The user's new instruction makes matched profiling/performance comparison a required deliverable after implementation, before adoption: cooked Development CPU/GPU/CPU plus reversed controls, same third-person scene/selections/quality, no audit readbacks/build/import overlap. Report per-thread and GPU/mean/P95, GPU-executed component counts and CPU fallbacks; retain failing/interfered evidence. No inference of scene FPS from this isolated correctness host.

## Shader-data cache milestone

AnimationCacheFinalR2 completes versioned bounded shader-data serialization, immutable file publication, bit-identical disk roundtrip, shared CPU ownership validation and asynchronous worker loading. Ten actual private rigs use reloaded data for the same six-phase GPU pose/palette/low-vertex oracle and pass. Eleven corrupt/stale/budget/lifecycle cases are rejected with existing output preserved. The 118,799,304-byte cache reads/checks on a worker in 481.9177 ms in this diagnostic warm-cache run; this is not cold/random-100 scene load or FPS evidence. CacheOwnerRegressionR1 confirms final-source 100x20 synthetic resident behavior.

Still required: production per-card source/mesh/clip manifest and cache discovery/cooked packaging; runtime selection/clip lifetime and bounds; actual provider/render integration with low/near/shadows; remove CPU pose work only for verified GPU components. Profile matched cooked CPU/GPU controls after integration and complete final visuals/loading/FPS acceptance. The shader-data format alone is not a complete standalone gameplay asset.

## Manifest and selection milestone

AnimationManifestFinalR1 completes versioned actor/clip/duration/soft-mesh/palette metadata roundtrip and validates arbitrary chunk/actor reordering for ten real private rigs against native CPU component poses and renderer-format palettes (1,695,456 elements, max 0.0000762939453). Sixteen malformed/semantic/selection/composition cases are rejected. ManifestSyntheticFinalR1 additionally validates 100 independent actors x20 synthetic clips with independent A/B/time/blend after permutation and reversed chunk composition, against an analytical CPU reference. No shared poses/private bindings are substituted.

Next actionable work is actual scene registration: production cache/manifest discovery and exporter/package support, per-component ownership/palette linkage, current/previous world transforms, conservative bounds and low/near/shadow/unsupported fallback. Remove CPU work only for verified rendered components. The user reiterated that profiling and CPU/GPU comparison MUST occur in the formal scene after implementation, at identical third-person camera/100-card selections/quality/60 Hz settings; these numerical tests are not substitutes. Final quality/loading/FPS acceptance remains pending.

## Formal scene request-control milestone

Added F8 request toggle, F9 truthful status, console/launch/Blueprint APIs and Boolean performance-control JSON input. FormalR3 exercises launch ON then runtime OFF/ON/OFF in the cooked Development 100-character third-person scene; all three windows retain 100 distinct animated cards and low-only startup. Requested GPU remains explicit CPU fallback with zero executed components because the renderer adapter is not installed. No legacy MatrixBank/matrix-gather substitution. ON requests and mid-window changes cannot qualify for FPS acceptance; new controlled windows reset the mixed-window marker. See CROWD_GPU_ANIMATION_SCENE_CONTROL.md and the retained receipt/logs.

Fixed standalone shader packaging prerequisites revealed by the scene test: the new registered shaders need matching Cook content; independent dynamic hierarchy traversal requires SM6, with HasShader rejection before allocation/dispatch on unsupported platforms. Final Development build and 100x20 numerical regression pass. Stable game and authoring DLLs/assets are untouched. This milestone completes request controls, not the actual GPU renderer adapter, CPU pose removal, quality acceptance or formal CPU/GPU profiling. Those remain required next work.
## 2026-10-05 renderer integration checkpoint

- Actual independent GPU pose and per-mesh palette writes now reach the formal scene renderer transform buffer via an opt-in, scene-owned provider adapter.
- Runtime OFF/ON/OFF succeeds with all 100 cards animated. A ten-card fixture yields seven visible GPU actors / fifteen components in the matched third-person window; unsupported and near parts stay CPU.
- Capture playback clock at pose finalization. Thirty actual rendered components / 25,872 matrices match native CPU reference with max scalar error 0.00006103515625 and zero mismatches.
- Native CPU pose remains enabled. This is an integration correctness milestone, not CPU offload completion or 120 FPS delivery.
- Next: source-freshness catalog and all-card caches, previous rendered transform validation, supported CPU-work removal with near/shadow/socket/bounds compatibility, then uncapped repeatable performance acceptance. Every performance report must include frame mean/P95 and Game/Render/RHI/GPU means against 8.333 ms.

### History/CPU dependency checkpoint

RenderHistoryR2 now passes actual current/previous renderer matrix checks: cumulative
179,439 comparisons, including 12,762 previous matrices, zero mismatches, max
0.000186920166015625. OFF/ON/OFF keeps 100 independent cards animated and ends
with zero GPU actors/components after OFF. Native CPU remains enabled.

Implemented per-actor held-pose history selection and a genuinely idle OFF bridge:
no per-frame all-character mesh scan, playback-array render command, or finalized
clock render command while disabled. These changes preserve current mesh/material
quality. Final visuals and 120 FPS remain open.

Installed engine research confirms ordinary FSkeletalMeshObjectGPUSkin still calls
UpdateRefToLocalMatrices in InitDynamicSkelMeshObjectDataGPUSkin even when our
renderer provider overwrites its palette. Merely skipping RefreshBoneTransforms
would freeze native near followers and retain stale bounds/data; do not use that
as a completed universal CPU-offload implementation.

The next supported integration should use a private UTransformProviderData with
UInstancedSkinnedMeshComponent (one independently owned instance per distinct mesh)
or an explicitly GPU-owned skeletal dynamic-data path. No cross-character sharing
is required. It must provide independent current/previous time, private inverse
bindings, conservative authored animation bounds and compatible low/near parts,
then keep native CPU fallback for sockets/physics/morph/unsupported interaction.
Per-card source catalog and near-part palette mapping remain prerequisites.

### GPU-only native component milestone

Implemented UOKIndependentTransformProvider with independently owned provider GUIDs
and native instanced-skinned render proxy registration. GpuOnlyProviderR7 now passes
actual GPU-only component current/history buffer validation in formal CrowdRoom:
32,718 matrices including 14,022 previous, zero mismatches, max 0.0000152587890625.
Runtime structural checks confirm no CPU skeletal pose evaluator and disabled
component tick. Original 100 characters stay animated during OFF/ON/OFF.

This diagnostic uses one mesh and neutral materials; it retains an original CPU
source for numerical comparison and does not replace production character display.
Next: original instanced material shaders/Cook, conservative authored clip bounds,
source-validated low/near palettes and component transition/fallback ownership,
then remove supported CPU consumers and profile without diagnostics.
See CROWD_GPU_ONLY_PROVIDER_PROBE.md for exact scope, errors and frame times.

## Original-material GPU-only probe checkpoint
Generic preparation and Cook are now implemented and verified for two real selected low-character libraries: 249 compiled material/parent copies, 157 source slots, no section merge or original asset writes. Formal Development GpuOnlyOriginalMaterialsR1 passes OFF/ON/OFF with 100 original characters retained, 59 prepared material slots on the one GPU-only probe, 32,718 current/history comparisons including 14,022 previous, zero mismatches. Shader library loads without missing-shader/default-material warnings. ON capped/readback mean 20.233 ms, P95 34.365 ms; this is functional-only and 120 FPS remains unmet. See CROWD_GPU_ORIGINAL_MATERIALS.md. Next production work is low/near palette coverage, independent clocks/bounds and original component ownership/fallback, before removing CPU consumers and profiling uncapped.

