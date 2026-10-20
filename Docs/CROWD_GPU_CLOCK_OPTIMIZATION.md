# Independent GPU pose and clock ownership

## Plan and implementation

1. Keep each selected character's private animation tracks, skeleton and inverse-bind palettes. Do not assume a common pose or identical meshes.
2. Opt-in `CrowdIndependentGpuOwnClock` adopts the complete native character only after all components are registered and supported. Disable its CPU component ticks/pose evaluation; send independent clip/time parameters to resident GPU pose and palette passes. Preserve original native meshes, materials, outlines and shadows.
3. Derive conservative animated bounds from all active-clip keys and weighted section palettes. Preserve native current/previous buffer bookkeeping using public ForceMotionVector. This still performs native mesh/render-data updates; it is not a completely GPU-driven draw submission system.
4. Restore CPU ownership on toggle, replacement, unsupported state or detected renderer fallback. Rebase individual playback speed/seek changes, retain per-actor clip selection. Missing clips stay CPU. The current test assets contain one walk per actor; twenty actual walk clips have not been exported or validated.
5. Exact fixed-time whole-character CPU/GPU captures, then same-process CPU/GPU/CPU native Development profiling in the third-person room. No hardware affinity tuning or quality reduction.

## Supported scope

The adoption guard rejects nonzero morphs/curves, animation curves, root motion, active physics, cloth, mesh deformers, nonzero mesh LOD, missing private palettes and visible nonskeletal parts. Face state requiring those systems retains CPU. This is a general guarded path, not a card-index exception. GPU-produced matrices feed native rendering; only source clips/parameters differ between actors.

The conservative bound can be large (C015 about1175cm). It preserves visibility but can reduce culling efficiency; measured render cost must account for this. Tight GPU animated bounds and removal of native frozen-matrix preparation are further work, not claimed complete.

## Current verification

- Native cache/pose oracle R143 passed for compact RigR2 C015, including private skin palettes and history guards. Cache11,479,440bytes.
- Native matrix bridge R149 compared5,253,300 matrices over36 rendered parts: zero tolerance failures; maximum element error0.00013041496. Native previous-matrix count was0, so this run does not establish viewport motion-history equivalence.
- Native Development builds R146/R150/R154/R155 succeeded. Final isolated stage R156.
- Exact visual R158: seven clip times, front/side/back,21 pairs; minimum36 GPU parts and21 GPU passes confirmed. Maximum mean byte error0.00060908; maximum fraction of pixels exceeding8 byte levels0.00190735%. Inspect full character as well as these metrics. Non-temporal captures do not validate moving TAA history.
- CPU/GPU/CPU pilot R157 passed ownership restoration. GPU interval owned one actor with zero CPU pose evaluations and zero renderer fallback. Timings are capped60FPS and concurrent offline exports were running, so they are not performance acceptance.

## Reproduction

Use `Tools\lua\lua.exe Tools\test_crowd_gpu_clock.lua` with a verified native stage, shader patch, selection and cache or shard index. `--benchmark --profile` runs uncapped45-second CPU/GPU/CPU cases with independent traces; export jobs must finish first. Each GPU case requires all selected actors owned and zero CPU pose evaluation/render fallback. The test records frame mean/P95 and GT/RT/RHI/GPU times plus background CPU activity. A CPU-control drift gate is evidence of timing stability, not proof of an otherwise idle machine.

Use `Tools\lua\lua.exe Tools\run_crowd_development.lua --gpu-clock-visual-compare --pilot-card ...` for exact captures, then `Tools/lua/lua.exe Tools/compare_crowd_gpu_clock_images.lua <absolute-prefix>` to measure all21 pairs. Use a fresh tag. F8 controls CPU/GPU; F9 displays owned-clock actor count and renderer coverage.

## Full100 native profile

R168 same-process native Development CPU/GPU/CPU completed with100 loaded/animated/distinct cards,100 GPU-owned clocks, zero CPU pose evaluations while owned, zero GPU renderer fallback, and CPU ownership restored. Eight workers, unchanged models/materials,60Hz animation, third-person room,1600x1000 TAA. All100 resident cache shards loaded on the worker in5291.76ms; reported process loading met60seconds.

| Mode | Mean frame ms | P95 ms | GT ms | RT ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
| CPU control |21.647|26.948|18.302|21.635|3.352|1.964|
| Independent GPU clock/pose |39.345|50.887|22.198|39.336|4.726|13.870|
| CPU restored |24.215|38.275|19.130|24.238|3.615|1.999|

CPU controls drift11.87%, so this is not clean final speedup acceptance. It does show that the current GPU path must not become the default performance path: moving pose evaluation alone did not reduce total frame cost.120FPS remains unmet.

R166 unfiltered CPU-only2s event window of the earlier R162 GPU trace (73 GT/72 RT frames) measured RT Skinning8.503ms inclusive per call and GT CrowdIndependentGpuTick6.618ms inclusive per call. Worker totals and overlapping scopes cannot be added as frame time. Native frozen-matrix preparation, per-part palette RDG dispatch/graph setup, repeated ownership checks and conservative bounds still cost work. Tight bounds and grouped palette dispatch are the next priorities; do not claim draw submission or native mesh updates have moved entirely to GPU.

R165 removes the adoption-time quadratic pose-name lookup and bounds parts independently, retaining the face/hair guard. R168 repeats the full scene and remains slower. Process launch-to-ready12.699s; this loading measurement starts in CPU control with the GPU cache resident, not a cold first GPU adoption benchmark. Keep the existing CPU RoleBudget launch as the default.

R172 actual-GPU-bounds comparison failed the pixel gate because GPU bounds reveal shoes omitted by the default native CPU culling metadata. This is not accepted full-runtime visual equivalence. R175 same-pose/same-camera/**same conservative bounds** isolates pose/shading: all21 pairs pass, maximum mean byte error0.00060908 and maximum pixels over8 levels0.00190735%. Full native culling/temporal equivalence remains pending. Do not substitute this diagnostic for that acceptance.

Experimental launchers: `CrowdRoomDevelopmentGPU.cmd` and `CrowdRoomDevelopmentGPUClock.cmd`, descriptor `Saved/Crowd/GpuClockDevelopmentExperiment.json`. F8 CPU/GPU toggle; F9 actual ownership/coverage. The descriptor explicitly records no120FPS acceptance, no default-game adoption, and no native-culling equivalence. R173/R174 executable changes only diagnostic captures from the normally profiled R165/R167 runtime;100-character functional results are retained. Existing CPU RoleBudget remains the default. The previous ten-card CPU-retained matrix bridge is preserved as `CrowdRoomDevelopmentGPUMatrixBridge.cmd`.


## Batched private skin palettes (R177 onward)

Opt-in `CrowdIndependentGpuBatchPalettes` / launch script `-IndependentGpuBatchPalettes` groups all supported native components in a provider context into one compute dispatch. Each job retains its own bone mapping, inverse bind matrices and current/previous native destination ranges. Different characters still choose independent clips and playback times. This does not merge meshes, materials, draw calls or skeleton poses.

Immutable entries are cached by retained GPU resource identity, not a raw component pointer. Per-frame uploads contain only job ranges and 64-bone group indices. New/replaced mappings extend the catalog; stale entries are bounded and rebuilt. Whole-batch validation rejects overlapping cross-component/history writes, output overflow, stale generations and source aliases before submission. A rejected batch uses native fallback and relinquishes GPU clock ownership. Native frozen-matrix preparation and loose bounds still remain.

Native Development R177/R180 built successfully. New global shader Cook R181 and isolated snapshot R182 use a separate patch directory; existing validated private animation shards are reused because their format/data did not change. No model or material recook/quality change was required.

R179 real C015 oracle passed six phases, 5,921 private palette bones and actual editable vertices. Each phase compares two sparse batch jobs against the legacy shader byte-for-byte: one preserves previous pose, the other resets history to current. Canary gaps remain unchanged; empty jobs, cross-job overlap and overflow are rejected. Legacy CPU-reference maximum matrix error 0.0000610352 and vertex error 0.0000762999cm remain below original tolerances. This does not establish viewport TAA motion-history acceptance.

R186 native scene captures compare seven clip phases from front/side/back, 21 pairs, with at least 36 GPU parts. Pixel gate passed: maximum mean byte error 0.00060908; maximum pixels over 8 levels 0.00190735%. Manual side capture inspected. Both use identical conservative bounds; the earlier native CPU culling difference remains unresolved. Tight clothing/stocking intersections are visible on both and remain a separate deferred issue.

Formal full100 CPU/GPU/CPU batch and legacy controls are being recorded under fresh tags. Do not use pilot or visual-capture frame timings as crowd FPS evidence. Dispatch count is reported separately as `independent_gpu_palette_dispatches`; `independent_gpu_animation_render_passes` counts provider contexts, not individual compute dispatches.


### R184/R185/R189/R190 measured outcome (not final performance acceptance)

Both full100 native Development tests pass GPU ownership, zero CPU pose evaluation, zero renderer fallback and CPU restoration. The same executable/shader patch is used; only the private-palette batch switch differs. Average legacy palette dispatches per provider context: 3,628.37; batch: exactly 1. Models, materials and animation frequency are unchanged.

| GPU mode | Mean frame ms | P95 ms | GT ms | RT ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
| Legacy per-part dispatch R185 |34.555|47.280|20.145|34.546|3.917|11.982|
| Batched private palettes R184 |21.296|23.330|15.396|21.288|3.051|3.223|

These are raw diagnostic measurements, **not accepted clean speedup or120FPS evidence**. CPU control drift is10.80% in R184 and7.93% in R185. The recorded foreign UnrealEditor process consumed4.68 CPU cores during R185; no foreign process was stopped. Comparison receipt: `Saved/Crowd/PaletteBatchComparisonR191.json` explicitly marks performance acceptance false.

Unfiltered CPU-only2s trace windows R189/R190 show Render Thread Skinning inclusive per call4.118ms batched versus12.050ms legacy. These timings inherit background interference and include palette setup/renderer bookkeeping; they are not GPU kernel times or guaranteed savings. Remaining batch costs include GT CrowdIndependentGpuTick6.371ms per call and RT primitive-octree insertion1.665ms per call. GT native mesh updates, repeated component compatibility checks and large animated bounds remain. Do not add overlapping scopes/thread totals to obtain frame time. Next work should cache unchanged ownership validation, bypass redundant native frozen-matrix preparation safely, and tighten animated bounds while preserving visibility/current-previous history.

R184 process launch-to-ready11.010s / load wall9.597s; R18512.755s /10.957s. Both meet60s in this prepared-cache startup, initially CPU-owned with GPU cache resident. Cold APNG conversion and cold first GPU adoption remain separate acceptance cases.

`CrowdRoomDevelopmentGPU.cmd` now uses verified batch snapshot R182/patch R181 and the original private animation shard index. `CrowdRoomDevelopmentGPU.cmd -LegacyPalettes` compares the previous GPU submission path. F8 remains CPU/GPU ownership toggle; F9 actual ownership/coverage. Windows PowerShell5 ValidateOnly passed for both launch modes. The original descriptor is backed up as `Saved/Crowd/GpuClockDevelopmentExperiment-R174.json`; the CPU RoleBudget default is preserved. Native culling/TAA equivalence and120FPS remain unaccepted.

## R192–R197: remove unused RDG imports and repeated membership work

Batch validation now queries existing external buffers instead of importing unused per-part palette resources (two imports per job). Output/current/previous overlap, generation/layout and cross-job write checks remain. Clock membership compares saved adoption order once, and now also tracks adopted mesh assets to recompute bounds on same-component replacement. A reordered component list restores/readopts safely. No mutable physics, morph, cloth, deformer or visibility guard is disabled. Added CPU scopes CrowdGpuOwnedClockTick, CrowdGpuClockEligibility and CrowdGpuClockAdvance.

Native builds R192/R196 passed; R197 snapshot uses the unchanged verified global shader patch R181 and unchanged private animation shard format/data. R193 real palette oracle passed with history/reset-history byte equality, canaries and previous numerical tolerances. R195 fixed-pose same-bounds CPU/GPU21 pairs passed (max mean byte error0.00060908, max pixels over8 levels0.00190735%). R196 only adds mesh-identity invalidation to R192; it does not change shader pose arithmetic. Full100 control profiles are recorded separately before adoption.


### R198/R199 full100 functional and profile result

Both runs pass100 GPU-owned actors, zero CPU pose evaluations, zero render fallback, exactly one palette dispatch per provider context, and CPU ownership restoration. R198 is the previous batch snapshot R182; R199 uses R196/R197 existing-resource queries and linear mesh-aware membership. No shader arithmetic, geometry, texture, material, shadow policy or animation frequency change.

| GPU version | Mean frame ms | P95 ms | GT ms | RT ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
| Before R198 |28.533|38.810|20.569|28.525|3.907|3.134|
| After R199 |20.066|26.874|15.603|20.055|3.249|3.141|

CPU controls drift17.03% before and15.29% after; these raw times are diagnostic, not clean speedup/120FPS acceptance. Prepared-cache process loading12.332s before /12.283s after meets60s, initially CPU-owned with GPU caches resident. Cold conversion/first GPU adoption remain separate.

CPU-only unfiltered2s traces R202/R203 at65..67s show RT Skinning mean inclusive5.085ms/call before versus2.501ms/call after. GT CrowdIndependentGpuTick7.359ms versus7.038ms/call. New scopes isolate owned-clock tick3.758ms/frame, eligibility0.02984ms/actor (about2.984ms per100 actors), advance0.006546ms/actor (about0.655ms per100). These are nested/overlapping scope measurements, not additive frame time. RT octree insertion remains1.692ms per call and primitive-transform update3.656ms inclusive. VSM marking overflow persists with loose bounds; no quality settings were reduced.

Comparison evidence `Saved/Crowd/PaletteQueryComparisonR204.json` sets performance acceptance false. The experimental GPU launcher now uses R197 with the unchanged R181 shader patch. Windows PowerShell5 batch and legacy ValidateOnly passed; previous R182 descriptor is backed up. The mesh-replacement guard is implemented and full100/toggle behavior passes, but a dedicated live mesh-replacement scenario is not yet part of final acceptance. Original native culling/TAA and20 real walk styles remain pending.

Next priorities from the new trace: reduce repeated registration/eligibility work with explicit invalidation, safely bypass native frozen-matrix preparation, and replace whole-part-box inverse-bind reach with per-bone influenced-vertex bounds. The latter needs numerical clip/interpolation containment and visibility tests before changing culling; never shrink bounds solely to improve FPS.


## R205–R227: generic influenced-vertex bounds, fixed and variable weights

Replace whole-part inverse-bind box corners with the actual maximum inverse-bind distance of vertices having nonzero influence on each bone. Translation and scale operator-norm limits still use every key of each actor's private active clip and propagate through its private hierarchy, so the bound covers interpolation rather than just sampled frames. Nonnegative skin weights use the actual maximum weight sum. Preserve native/face bounds union and numerical/WPO margin, original geometry/materials/textures, shadows,60Hz animation, private bone maps, and independent clocks. No hardware affinity change or pose sharing.

Cache immutable reach by weak mesh, active weight buffer and GPU position/weight/lookup resource identities; clock membership also checks mesh, active weight buffer and exact component-relative transform. Compute once at adoption; subsequent ordinary playback does not read vertices or read back GPU data. Handle fixed and variable influence layouts using native per-vertex offset/count; never infer ownership from anatomical bone names. If CPU arrays are absent, batch position/weight/lookup buffer staging copies per actor, poll fences with a10s initialization limit, and release staging/raw arrays after extracting reach. Failure retains the older conservative whole-box bound. Raw arrays are retained only for opt-in visual diagnostics.

R205 failed FVector3f API compilation; R206/R207 built the CPU-only partial approach. R209/R212/R215 covered11/38 parts. R215 reason logging proved the other27 had variable influence layout, not evidence of Cook stripping. R216/R221 native Development builds succeeded. C015 now covers38/38, maximum bound radius1176.155→586.330cm. Full100 covers3833/3833 parts; these remain conservative and some characters still have radii exceeding10m, so bounds/VSM work is not solved entirely.

R218 normal and R224 forced GPU-readback visual diagnostics each pass7 phases ×3 views,21 CPU/GPU pairs. R224 batch reads38 parts in0.039694s and validates positions, palette indices and normalized8/16-bit weights against native retained data. All300,951 native skinned vertices across7 phases lie within bounds (max radius ratio0.860731). Pixel max mean byte error0.00060908; max pixels with channel error>8 is0.00190735%. This is same-conservative-bounds, non-temporal pose equivalence, not native CPU culling/viewport TAA acceptance. Pre-existing tight-garment intersections remain deferred.

### Actual100-character native Development scene: CPU → GPU → CPU

| Run/mode | Mean frame ms | P95 ms | GT ms | RT ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
| R220 CPU |21.550|26.026|17.575|21.536|3.257|2.019|
| R220 GPU |18.113|21.222|15.191|18.100|3.002|2.957|
| R220 CPU restored |21.562|25.762|17.477|21.554|3.247|2.007|
| Final R226 CPU |20.545|24.876|17.319|20.531|3.177|1.996|
| Final R226 GPU |17.197|20.413|15.305|17.182|2.981|2.937|
| Final R226 CPU restored |20.702|24.722|17.321|20.691|3.195|1.988|

Third-person scene,100 distinct selected characters,8 general workers, uncapped1600×1000, same quality/shadow policy. R220 control drift0.052%; R2260.762%, both pass the harness5% drift gate. R226 GPU is about58.15FPS and16.6% lower frame time than the mean CPU controls. These quantify the complete GPU mode versus native CPU mode; they do NOT isolate this bounds change from earlier GPU optimizations. Older R199 GPU20.066ms has15.29% CPU control drift and cannot establish a clean bounds-only speedup.120FPS/8.333ms remains unmet.

Both100-character runs have100 owned clocks,0 CPU pose evaluations and0 renderer/clock fallback; restoring CPU succeeds. The prepared Cook/cache loading report is stored in InfluenceBoundsComparisonR227.json; it does not time cold APNG conversion. R223 is the final private executable (R221 build) and R226 records its exact SHA256. The experimental CrowdRoomDevelopmentGPU.cmd descriptor now points to R223; F8CPU/GPU andF9 actual coverage remain available. Default CPU launcher is unchanged, prior R197 descriptor retained.

### Remaining CPU work from the unfiltered CPU-only trace

R222 anchor61.82794s; R225 exports62..64s and106 frames. RT Skinning1.829ms/call, primitive transform update2.995ms inclusive, octree insertion1.498ms/call. GT independent GPU tick6.256ms inclusive; owned clock3.914ms, eligibility3.287ms per100 actors and advance0.525ms per100. These scopes overlap and are not additive frame times. RT waits and native primitive registration/mesh updates remain; moving pose evaluation to GPU has not removed all CPU rendering submission. Compared with R203 raw scopes Skinning2.501ms and primitive update3.656ms, this gives a new diagnostic direction, not an isolated timing claim.

Next work should reduce repeated eligibility/registration checks through explicit invalidation and native MeshObject/primitive-transform updates while preserving history and moving visibility. Do not suppress revision/history updates or freeze moving bounds to obtain a faster number. Final acceptance still needs viewport TAA, original culling visibility, near-face asset transitions,20 distinct real clips, and randomized-card/cold-load coverage.

Final release check: runtime source/inl equals the R221 build copy; R223 binary equals R226 profile SHA256. Windows PowerShell5 experimental launcher ValidateOnly passes (batch ON). VSM marking queue overflow still occurs; no bound-only/120FPS acceptance is claimed.


## R228–R238: single-pass live eligibility with explicit0/1 control

Merge the live skin support/registry check with adoption-order/mesh/weight/relative-transform identity comparison. Read the skeletal mesh once per part, stop at the first unsupported part, and bypass native GetClothingAssetsInUse section scanning only when the live mesh clothing-asset list is empty. The native lookup cannot resolve any section GUID without a clothing asset. If an asset is added, the full native query resumes immediately. This is a live predicate shortcut, not a periodic or indefinite state cache. Keep per-frame registration, predicted LOD, simulation, deformer, morph weights/curves, external morph sets, extra visible meshes, clip/rate/state and mouth checks. Invalid weak pointers short-circuit before reading component buffers/transforms.

R228 first compiled implementation is not used: review found a potential dead-component identity null access. R229 fixes Good&& before identity reads; R230 snapshot/R231 full100 CPU→GPU→CPU passes100 ownership,0 CPU pose evaluations,0 clock/renderer fallbacks, CPU restoration. GPU frame18.312ms/P9520.271, GT13.876, RT18.305, RHI2.944, GPU2.943. CPU controls20.928/21.169ms drift1.155%. Relative to R226 GPU17.197ms, total frame did not improve even though GT15.305→13.876ms. Do not claim complete-frame speedup.

R232 trace anchor61.13336; R233 exports62..64s. CPU-only eligibility mean0.022664ms/actor (~2.266ms/100), skin checks/identity0.019423ms/actor (~1.942ms/100). Owned clock2.867ms/frame, independent GPU tick5.289ms/frame. Prior R225 eligibility~3.287ms/100, owned clock3.914 and independent tick6.256ms/frame. Nested scopes overlap and cannot be added into frame time. Native cached skeletal and mesh draw commands are already1; bRenderStatic is not a safe workaround for deforming meshes.

R234 adds console `ok.CrowdGpuEligibilityPass 0/1`, explicit validated control JSON `gpu_clock_eligibility_pass`, and report field of the same name.0 reproduces the old two-pass/full native cloth query;1 uses the new single traversal/empty-clothing shortcut. Switching preserves clocks, clip phases and rendering. R235 private native executable uses unchanged Cook and R181 shader patch. Harness -EligibilityComparison holds GPU animation ON in all three phases and checks actual0→1→0 mode, ownership, CPU evaluation and fallback counts; ordinary CPU/GPU/CPU behavior remains its default.

### R236 same-process100-character GPU0→GPU1→GPU0

| Eligibility pass | Mean frame ms | P95 ms | GT ms | RT ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
|0 before |18.167|20.685|14.924|18.156|2.965|2.944|
|1 fast |18.634|20.293|13.859|18.627|2.937|2.974|
|0 restored |18.470|20.825|14.798|18.460|2.913|2.953|

Baseline GPU control drift1.666%, within harness5% gate. All phases own100 clocks with0 CPU pose evaluations and0 renderer/clock fallback. Fast saves about1.002ms GT relative to the mean baseline, but does not improve mean complete frame. The fast path is implemented and available; the experiment launcher will explicitly select0 until whole-frame benefit is established. Console0/1 or launch -EligibilityPass0/1 can compare it without restarting. Evidence EligibilityComparisonR238.json deliberately sets whole_frame_speedup_accepted=false and fps_120_accepted=false. No model, material, texture, animation Hz, shadow or hardware-affinity change. No new CPU work was moved to GPU in this step; redundant CPU work was removed alongside the already GPU-owned animation.


### R237/R239: exact executable toggle checks and no-trace frame comparison

R237 final R235 binary passes100-person CPU→GPU→CPU with100 GPU-owned clocks,0 CPU evaluations/renderer fallback, and restored CPU ownership. This is a capped functional test, not120FPS evidence. R239 reruns same-process GPU eligibility0→1→0 uncapped without trace to remove recording overhead:

| Eligibility | Mean frame ms | P95 ms | GT ms | RT ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
|0 before |17.005|19.527|14.283|16.990|2.898|2.940|
|1 fast |17.644|19.180|13.175|17.632|2.877|2.955|
|0 after |17.270|19.603|14.154|17.255|2.844|2.945|

Use the JSON phases for exact RHI/GPU values. Fast saves~1.044ms GameThread versus averaged baselines, but mean complete frame does not improve; P95 is lower. Accordingly leave the GPU launcher explicitly on eligibility0. R240 combines traced and no-trace evidence and sets whole-frame speedup acceptance false. Default GPU animation stays ON; F8 switches CPU/GPU, F9 shows actual coverage. New console `ok.CrowdGpuEligibilityPass 0/1` changes eligibility live; launch helper `-EligibilityPass 1` selects the fast implementation. Both modes preserve the same dynamic safety checks and renderer/geometry quality. Default CPU RoleBudget launcher is unchanged. R223 descriptor is backed up; R235 binary and source copy identities are verified before release.

Render Thread remains dominant. Further work must reduce primitive-transform/octree and native rendering-submission work, with correct moving bounds, shadow invalidation and motion history. This phase is an implemented CPU-work optimization with measured GameThread benefit, not an additional GPU offload or a120FPS result. Whole-scene final acceptance remains pending.


## R241–R255: exact full native palette cache, rejected as default

Native USkeletalMeshComponent already caches skeletal local/world bounds. The remaining primitive-transform/octree work cannot be safely removed by freezing moving bounds. ForceMotionVector must preserve GPU current/previous revision publication. Investigated memoization of unchanged native CPU reference-to-local palettes instead, without sharing poses between cards. CPP-only Engine patch, public Engine ABI/shaders/geometry unchanged; console r.SkeletalMesh.ReuseRefToLocalMatrices0/1 and independently recomputing verification flag. Dynamic control/report and isolated GPU0→GPU1→GPU0 harness implemented. Default0.

R242/R243 first full-signature implementation verifies448,003 hits with0 bit mismatches. R245/R246 limits exact comparisons to numerical dependencies used by the native formula, compares owned snapshots directly on hits and writes a replacement only on misses. R247 verifies2,165,900 hits with0 mismatches/failure on100 private GPU-owned actors; CPU evaluations/fallbacks0. No per-card fixes or approximate equality.

### Identical100-character third-person native Development scene

| Run / full reference cache | Mean frame ms | P95 ms | GT ms | RT ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
| R248 traced0 before |17.390|20.439|15.238|17.376|3.036|2.936|
| R248 traced1 |21.547|28.424|18.682|20.263|3.338|3.214|
| R248 traced0 restored |17.923|21.004|15.224|17.906|3.004|2.945|
| R251 no trace0 before |17.060|19.620|14.218|17.047|2.986|2.953|
| R251 no trace1 |21.536|28.608|17.888|20.281|3.339|3.184|
| R251 no trace0 restored |17.425|19.680|14.016|17.408|2.953|2.948|

Uncapped1600×1000,8 workers,60Hz pose clock, same original geometry/material/shadow/AA policy. Traced baseline drift3.066%; no-trace2.138%. The new cache is slower (~24.9% versus averaged no-trace controls); do not adopt/default-enable it or claim more GPU work was offloaded. A/B phases remain100 owned/0 CPU evaluations/0 render fallback. These compare cache state with GPU animation already ON; not CPU/GPU mode speedup.

R252/R253 export all CPU timers in bounded baseline16..18s and cache62..64s windows; R254 summarizes115/87 owned-clock frames. Baseline MeshObject Update aggregate21.8824 CPUms/frame across workers, cache47.9423; cache itself37.3865. These are overlapping parallel work totals, not wall frame times. RT Skinning1.6945/2.0843ms/call and octree insertion1.4303/1.4121ms/call remain. Native matrix computation is cheap enough that per-field comparison, entry allocation/eviction and bookkeeping can cost more. Some cache calls exceed14ms due bulk32MiB-limit clearing. R256 corrects admission policy: never flush thousands of allocations on one hot-path miss; if net retained payload exceeds budget, compute natively without admission. Requires its own build/verification/benchmark, not inherited speedup acceptance.

R255 R246 executable passes CPU→GPU→CPU100-person functional toggle with100 GPU clocks,0 CPU evaluations/fallbacks and restored CPU ownership. Capped functional timings are not120FPS evidence. Experimental launcher remains previous validated release while final admission revision is tested.

Next structural candidates: explicit native dynamic-data bridge for an immutable CPU backup palette under GPU pose ownership (needs complete ownership/invalidation and current/previous fallback contracts), or grouping compatible skinned primitives while retaining all material sections/private bone palettes and facial translucent sort groups. Reduce registrations/primitive transforms, not image quality; merging mesh components must not be represented as automatically merging draw calls. No unsafe revision-only or pointer-only cache, RenderStatic flag or frozen movement bounds.120FPS/8.333ms and comprehensive native culling/TAA/20 real walk styles remain pending.


### R256–R259 admission correction and final no-trace comparison

R256 native build/R257 snapshot replaces bulk cache flush with admission refusal at the same32MiB/worker payload limit. Existing entries remain; if net replacement payload does not fit, use original computation without allocation/eviction of the entire working set. R258100-person functional0→1→0 verifies4,791,110 hits against independently recomputed native float32 output with0 mismatches/latches and0 CPU pose evaluations/render fallback. This establishes exact output for observed hits; not20 real clips/native culling/TAA final acceptance.

| R259 full-palette cache | Mean frame ms | P95 ms | GT ms | RT ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
|0 before |16.924|19.707|14.327|16.909|2.997|2.944|
|1 bounded admission |18.041|20.666|15.238|18.028|2.959|2.952|
|0 restored |17.181|19.777|14.268|17.166|2.953|2.939|

Same100 distinct actors, third-person uncapped1600×1000,60Hz animation,8 workers, no recording. Baseline drift1.521%; all100 clocks owned and CPU evaluations/render fallback0. There are7,134,311 observed hits and1,905,522 misses in middle report (~78.9% cumulative coverage), but frame is still~5.8% worse than averaged controls. Eliminating allocator spikes helps, yet exact comparison/bookkeeping still exceeds the saved computation. Reject this optimization as default. CrowdRoomDevelopmentGPU.cmd stays on validated R235 eligibility0/batched GPU mode; do not replace it with a slower experimental mode. Source patch/control/harness remain opt-in for reproducible research.

Prepared Cook/cache initial load10.627s from process start; cold arbitrary APNG conversion is untested. Background process CPU is retained in run receipts; controlled drift is not proof of an otherwise isolated desktop. No120FPS acceptance (baseline about58–59FPS; target8.333ms). No geometry/material/AA/shadow/animation reduction, hardware affinity or worker-count change.

R260 exact final R257 binary passes100-person CPU→GPU→CPU with100 GPU clocks,0 CPU pose evaluations/render fallback and restored CPU ownership. This is capped functional proof, not uncapped FPS evidence. R261 consolidates verification/profile/toggle, verifies exact binary SHA256 equality and runtime source/build-copy identity. Final cache slowdown is 5.797% versus averaged no-trace controls. Default cache remains0 and GPU launcher release staysR235; no positive speedup or120FPS acceptance.

## R262–R266 runtime compatible primitive grouping (opt-in)

Plan: reduce per-frame primitive registrations/transforms and per-component palette work, retain the original complete pose leader, preserve every material section/private skeleton, compare exact vertex/topology/weight payloads, then test native GPU ownership and same-pose pictures. Runtime grouping accepts the current chosen card, not a preselected fixed crowd. `-GroupPrimitives` in the Development launcher/harness enables the prototype and runtime private GPU palettes. No material atlas, triangle reduction, animation sharing or hardware-specific configuration.

Native FSkeletalMeshMerge builds the layout with unique forced section IDs. The wrapper restores actual raw high precision tangent/UV bytes and 16-bit influences, audits all positions, colors, connectivity, material references, private inverse binds and influence encoding. Different transforms, blend/sort classes, morph/mouth/cloth parts remain separate; no compatible group is forced on failure. The original full pose leader stays intact. Runtime data never replaces source Cook assets. Transient merged assets have reflected actor ownership; construction resets previous ownership/counters. Vertex-less or over-limit influence formats are rejected.

R264 C015 CPU/GPU/CPU functional sequence:38→18 parts, one accepted group,0 rejected,21,686 audited vertices,12.187ms initial grouping. R265100-person third-person native Development:3,833→1,744 imported parts,100 accepted groups,0 rejected,2,311,351 audited vertices,1.391s grouping. Prepared-cache process-to-ready11.357s; does not establish arbitrary cold-card conversion time. All100 GPU clocks own their pose,0 CPU evaluations/renderer fallback. Same source geometry/material sections retained; component count is not draw call count.

R265 uncapped traced CPU/GPU/CPU: mean16.706/11.042/16.952ms; GPU P9514.666ms, GT9.136ms, RT11.034ms, RHI3.040ms, GPU2.887ms. CPU controls drift1.47%. These compare animation modes with grouping ON, not isolated grouping benefit. R266 same-binary grouping-OFF control pending; matched-phase original/grouped/GPU capture added for functional validation. Do not claim120FPS or adopt as release default before the remaining comparison.


R266 same R263 binary grouping-OFF trace:CPU21.501/GPU18.919/CPU22.000ms (controls drift2.32%). Grouped GPU11.042ms versus ungrouped18.919ms is41.63% lower in these runs; background process CPU retained in both receipts, not an isolated desktop. Exact CPU-only bounded64..66s event exports,181 grouped frames/102 ungrouped:UpdatePrimitiveTransform1.077/2.971 inclusiveCPUms/frame; octree insertion0.132/1.590; aggregate worker MeshObject Update17.342/22.252. These inclusive/parallel costs overlap and are not additive. Final no-trace repeat is required.

R269/R276/R279 in-capture new original actor lacks visible private parts despite fresh bounds and zero-time pose ticking. Do not accept it as ground truth or claim either hypothesis proved the cause. R281 independent original scene through normal loading/warm-up passes the original-vs-grouped comparison instead. Tools/compare_crowd_gpu_clock_images.py --reference-prefix compares separately warmed scenes after exact character/clip/sample-time identity validation. All21 C015 phases/views pass: originalCPU vs groupedCPU maximum MAE0.010538/255 and pixels>8-byte0.041962%; originalCPU vs groupedGPU0.010628/255 and0.042343%; groupedCPU vs groupedGPU0.000583/255 and0.001907%. Fixed-pose non-temporal diagnostics only; not final viewport/culling/TAA/20-walk-style acceptance.

R283 native build/R284 snapshot removes the unnecessary freshly-spawned capture oracle; no additional actor or animation initialization path remains in capture. Keep the original normal CPU/GPU capture and the separate-process reference workflow. Runtime grouping retains reflected ownership and rejects empty/unsupported influence formats. R285 final binary100-character toggle/uncapped no-trace benchmark pending. Initial launcher remains R235; grouping opt-in only until broader visual/final FPS acceptance.


### R289–R294 final safety build, no-trace test and picture proof

R289 build/R290 snapshot adds common USkeleton owner, independent mouth-hidden state and runtime tangent-recompute exclusions. All100 cards retain the same3,833→1,744 result (100 groups,0 rejected,2,311,351 audited vertices). Source/reference geometry, material sections, raw normals/UVs, weights/inverse binds remain exact. Construction group time 1.402s. R291100-person CPU→GPU→CPU passes with GPU-owned100/CPU pose evaluations0/render fallback0 and CPU ownership restored; control drift 1.952%.

| Final same-binary no-trace GPU mode | Mean frame ms | P95 ms | GT ms | RT ms | RHI ms | GPU ms | Process-to-ready s |
|---|---:|---:|---:|---:|---:|---:|---:|
|R292 grouping OFF|18.329|19.874|13.179|18.322|3.165|2.909|10.512|
|R291 grouping ON|10.579|14.974|8.625|10.571|2.971|2.892|11.994|

Both uncapped native Development,100 distinct moving actors, third-person1600×1000,TAA,60Hz animation,8 workers; no model/material/shadow-quality or hardware-affinity adjustment. Same executable SHA256 F931628655D5A0E71CD9FD67B5DC284C090EC7482B6587442D9688B066D7E8F5. Observed frame reduction 42.29%; not an otherwise isolated desktop and not120FPS. Target8.333ms remains unmet (GT~8.625,RT~10.571). Prepared Cook/GPU-cache load remains below60s; arbitrary cold-card conversion is untested. R285/R286/R287 earlier no-trace repeat was10.686/18.215/10.911ms, grouped-control drift~2.11%, supporting the directional result without trace recording.

R293 exact final binary captures all21 phases/views; grouped CPU/GPU worst MAE0.000583/255,>8-byte pixels0.001907%. Versus warmed ungrouped R281 CPU reference:0.010628/255 and0.042343%. Pixel gates pass with original strict thresholds. Actual private GPU execution and native oracle bounds containment are recorded; screenshots exclude temporal AA and are not final live culling/near interaction/20 real walk styles acceptance. Default GPU launcher remains R235.

`CrowdRoomDevelopmentGPUGrouped.cmd` launches R290 grouped experimental native game, WASD/right mouse and F8 CPU/GPU switch. Add `-Baseline` to launch the same binary/configuration without grouping. The wrapper accepts SelectionFile/GpuShardIndex for prepared selected cards and ValidateOnly for inspection; it does not claim unconverted cards can enter within60s. R294 receipt verifies tested binary equality and runtime source/build-host copies. Errors and failed oracle captures are retained in CROWD_GPU_POSE_ERRORS.md.

Next work: explicit ownership/invalidation contract to remove redundant native CPU fallback-palette preparation while private GPU pose owns the component, preserving current/previous matrices, source/weight/visibility/LOD changes and immediate CPU fallback. Do not disable ForceMotionVector or freeze primitive movement/bounds based solely on frozen CPU pose. Keep actual viewport render/culling/TAA and near-interaction validation separate from fixed-pose diagnostics.
