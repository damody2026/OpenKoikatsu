# Development 120 FPS without visual reduction — 2026-10-04

## Scope and implementation
100 unique prepared characters, bright atrium, third-person camera, 60 Hz animation, current low-body/near-face/hair assets, native resolution/TAA and near-detail current-mesh shadows retained. No texture/model/shadow quality reduction in this change.

Implemented an optional scene-local bone-transform provider (`-CrowdParallelBonePacking`). Scatter allocation stays serial; packing writes disjoint spans on workers. It preserves the engine current/previous pose rules, bone maps, reference identity and full floating-point output. It changes CPU scheduling rather than moving pose evaluation to GPU. No engine source changes are required. Integration uses UE 5.8 private Renderer APIs and needs review on engine upgrades.

`-CrowdVerifyBonePacking` compares output byte for byte against serial engine encoding whenever a larger job population appears during loading. Movement/detail round-trip tested 600 jobs with zero mismatches and no missing near-shadow components. The option stays off by default until a matched, uncontended A/B shows a whole-frame benefit. Renderer task time alone does not justify enabling it globally.

## Evidence and limits
Untraced static third-person runs (milliseconds; separate runs with differing near-detail populations/background load):

| Run | Frame mean | Frame P95 | Result |
| --- | ---: | ---: | --- |
| NoTrace8R1, engine provider | 11.400 | 19.530 | failed 120 FPS |
| NoTrace16R1, 16 workers + full CPU affinity | 18.337 | 20.369 | failed 120 FPS |
| PackingNoTraceR1, 8 workers | 15.814 | 22.547 | failed 120 FPS |
| LateControl8R1, engine provider | 26.066 | 37.600 | failed 120 FPS |
| PackingForeground4R1, split 4+4 and AboveNormal | 15.212 | 21.840 | failed 120 FPS |

Raw files: `Saved/Crowd/DevelopmentRoom120*.json` and associated background-process reports. Do not infer a causal FPS gain from this table: background load drifted, and automatic initial follow-camera placement changed nearby character counts. 16-worker trial also changed affinity; foreground trial also changed priority. Neither isolates a single variable.

Baseline trace: mesh-object bone packing task averaged about 3.05 ms per call. Candidate trace: packing task about 1.22 ms per call. Trace overhead and different workloads prohibit translating this directly into FPS gain. GPU averages remained about 1.6–1.7 ms in static runs; RT exceeded the 8.33 ms target. Cross-worker inclusive totals overlap and cannot be summed as frame time. Moving-camera functional runs are not stationary-crowd acceptance.

The attempted F16C encoder was inactive because USE_COMPRESSED_BONE_TRANSFORM=0. Removed the dead candidate. The actual engine float transpose already uses SIMD. No hardware-conversion speedup is claimed.

## Best next steps preserving output
1. Use one resident scene and a fixed camera/phase for provider A/B. Record near-visible component counts, P95, RT/RHI and background contention. Default remains 8 workers; measure 16 with the same affinity separately.
2. Investigate redundant current/previous bone uploads across body, face, eyes and hair. Reuse only after verifying bone-map, inverse-reference and private accessory hierarchy compatibility; shared KKS skeleton alone is insufficient.
3. Reduce unchanged primitive/render-state updates and repeated upload allocations. Preserve updates required by motion, current shadows and near-detail swaps.
4. Investigate cached mesh draw commands and immutable material/texture resource reuse. Different characters cannot automatically instance into one draw; preserving compatible sections and transparency ordering is required.
5. Evaluate GPU gathering of already evaluated matrices only if it removes measurable CPU packing/upload work without increasing transfer volume or altering precision. Do not move the entire rendering submission pipeline to GPU by assumption.

Acceptance: complete Development package, same scene/camera/resolution/character selection, frame mean and P95 <= 8.33 ms after warm-up, then a third-person movement/near-detail visual regression. **Not achieved for the stationary scene this turn.** First-time arbitrary-card conversion/load time is also not established by prepared-asset runs.

Errors and rejected hypotheses: `Docs/CROWD_GPU_POSE_ERRORS.md`.

## Final build and functional gate
Quality120FinalR3 isolated Development build and staging succeeded. Quality120FinalFunctionalR3 completed: 100 loaded, 0 failed, movement/detail-switch runner checks passed, near-shadow policy passed with 0 missing components, 600 verified packing jobs and 0 byte mismatches. Moving-camera mean 4.016 ms / P95 6.841 ms; this is a functional route result, not stationary-scene acceptance. The candidate remains optional. Editor overall build was blocked by an unrelated commandlet holding engine NetCore.dll, although the project DLL compiled/linked. See error log.

## No hardware-specific tuning — 2026-10-04
At the user's request, removed the cache-group/core-affinity placement implementation and its runtime/control/report hooks. Optimized launch and fixed-camera comparison tools now explicitly use Default OS affinity and expose no cache-affinity choice. Legacy diagnostic scripts/raw results remain historical evidence; they are not the recommended gameplay configuration. No CPU-model detection or core pinning is part of this change. Current visual quality remains unchanged.

The corrected fixed-camera comparison tool resets phase/layout, settles near-detail streaming and freezes each completed report. FixedWorkers8AllR2 matched camera and near-resident population: engine frame means 19.222 and 19.580 ms versus range packing 21.588 ms; respective P95 values 23.425, 24.615 and 25.136 ms. Parallel packing stays opt-in. These runs do not pass 120 FPS, and background contention prevents claiming uncontended acceptance.

FixedABEngineProfileR1 separates idle waits from useful work: original matrix packing averaged approximately 2.85 ms per task. In the three-second CPU event window, RHI WaitForTasks occupied about 2.30 seconds; RT waited for visibility and mesh-pass setup tasks. Wait scopes overlap and must not be added. Increasing RHI width alone has not established an improvement. GPU matrix gathering remains an investigation, not an implemented or validated optimization.

NoHardwareTuningR1 isolated Development build and staging both succeeded after removal. Generated-host sources contain no placement hooks. No new FPS claim is made from this rebuild; current 120 FPS acceptance remains unmet.

## 2026-10-05 Universal work-reduction experiments
No core/cache placement, CPU-model tuning or visual-quality reduction. ExactPoseReuseR2 and SubmissionBatchR1 isolated Development builds/staging succeeded. All controlled cases retained 100 unique animated residents, 45 high-face residents and zero missing near-shadow components. The same resident process resets the camera/layout per case.

Implemented optional packing mode 3: reuse encoded output only for bitwise-identical source matrix arrays, identical bone-map storage and matching active output lengths in one upload callback. Both current/previous destinations remain written. No persistent revision/pointer cache. Synthetic tests reject changed matrices, signed-zero differences, maps and counts; real independent encoding comparison passed with zero mismatches. Sampled warm reports had zero reused bones. The candidate remains off, as does parallel packing.

ExactPoseReuseABR1 (engine / duplicate reuse / engine) frame means: 18.099 / 21.751 / 17.034 ms; P95: 23.948 / 28.299 / 22.841 ms. Own compilation overlapped the candidate case; this is rejected as an uncontended timing comparison, not a causal slowdown estimate.

Added in-process command-list batch controls to measure_crowd_case.ps1 and test_crowd_bone_packing.ps1. Invalid batch values are rejected, applied values are verified against reports. SubmissionBatchABR1 used mode 0 throughout, 8 workers, OS default affinity and Normal priority, with no own build during timed cases:

| Minimum draws/list | Frame mean ms | P95 ms | RT ms | RHI ms |
| --- | ---: | ---: | ---: | ---: |
| 64, initial baseline | 20.308 | 26.146 | 20.300 | 4.575 |
| 128 | 19.556 | 25.124 | 19.548 | 4.355 |
| 256 | 19.466 | 26.222 | 19.438 | 4.302 |
| 512 | 19.534 | 27.150 | 19.525 | 4.331 |
| 64, repeated baseline | 26.830 | 37.016 | 26.819 | 5.695 |

Camera/near-population comparison passed, but repeated baseline drift is approximately 32.1%. Do not adopt a batch size from the intermediate differences. The wrapper now reports drift and rejects timing comparison above 10%; passing this screening check alone does not establish uncontended or statistically significant gains. Defaults remain unchanged. Development 120 FPS acceptance is still unmet.

Scale diagnostics: a mode-3 warm report contained 639 upload jobs and 237,336 encoded bone transforms (about 10.9 MiB at 48 bytes each). Main-pass enabled section count was 4,654 and shadow-enabled sections 2,028, including outside-frustum components. These are section/configuration counts, not measured draw calls; job/bone counters are the last provider pass, not whole-run totals. GPU matrix gathering is still unimplemented and unvalidated; do not claim GPU offload from this CPU-only experiment.

## 2026-10-05 GPU matrix gathering implementation
Added optional mode 5 in the scene-local provider and the early-loading KKCrowdGpuPacking shader plugin. CPU animation, private rigs/physics, meshes, materials, 60 Hz policy and current/previous update rules are unchanged. CPU copies complete float32 source matrices; GPU gathers the engine's existing bone-map entries and transposes rows into the existing transform buffer using uint-bit moves. No baked GPU-animation dataset or card-specific code is introduced. GPU readback compares all written ranges against independent engine-style CPU encoding; mismatches disable the candidate. Unsupported compressed encoding, immediate RDG or unavailable shaders use the engine path. The candidate remains off by default.

Implementation iterations:
- R3 used a temporary packed bone-map buffer and 16-byte scatter units. GpuBoneGatherABR1: engine / GPU / engine frame means 21.370 / 24.370 / 21.942 ms; GPU P95 38.082 ms. 653 verified proxies, zero mismatches/fallbacks. No adoption.
- R4 reused Context.BoneMapBufferSRV and whole-matrix scatter units. GpuBoneGatherABR2: 17.928 / 20.042 / 21.078 ms; GPU P95 26.123 ms. 655 verified proxies, zero mismatches/fallbacks. Repeated baseline drift 17.6%; reject timing comparison.
- R5 keeps the original shared scatter builder and uses mapped QueueBufferUpload fills. GpuBoneGatherABR3: 23.740 / 17.181 / 16.899 ms; GPU P95 21.361 ms. 655 verified proxies, zero mismatches/fallbacks. Repeated baseline drift 28.8%; the large difference from the first baseline is not a GPU speedup claim.

Local UE 5.8 Execute waits Compile setup tasks before SubmitBufferUploads; a readiness marker preserves the original mesh-update prerequisites. Raw fill precedes work-descriptor fill in the upload queue. Readiness checks catch ordering violations; immediate-mode RDG is excluded. This relies on local engine ordering and private Renderer interfaces, so re-audit on engine upgrades.

Profile evidence (different captures/background loads; task-level observations, not whole-frame savings):
- Profile-GpuGatherEngineR1: original encoding task 2.425 ms/call; 1,312 scatter-builder calls for 1,312 frames, averaging 7.150 ms inclusive.
- Profile-GpuGatherComputeR1 (R4): raw copy 1.372 ms/call; 2,070 scatter-builder calls for 1,035 frames. Inclusive scatter total/frame ~7.28 ms. The premature flush split the work into two tasks and consumed the encoding savings.
- Profile-GpuGatherQueuedR1 (R5): raw copy 0.932 ms/call; scatter builder back to one/frame (1,040 calls), 6.368 ms inclusive. Trace frame mean ~19.231 ms. GPU packing is functional, but a stable whole-frame gain and 120 FPS have not been established.

Final functional/packaging evidence: isolated Development R5 build/stage, shader Cook R3 and root project Editor module rebuild GpuPackingConsumerEditorR2 succeeded. Controlled tests completed 100 unique animated characters with no missing near shadows. Prepared-asset ProfileR2 run initial low-only load: 6.400 s from load start / 7.755 s from process start; initial high meshes and high/large textures both zero. This is a warm prepared-selection result and does not prove first-time arbitrary-card conversion or cold disk loading.

Reproduce with `Tools\lua\lua.exe Tools\test_crowd_bone_packing.lua --modes 0,5,0` for untraced A/B, or `--modes 0,5 --seconds 45 --trace` for diagnostic captures. Shader/plugin, consuming Editor module and shader Cook must match before staging. Mode 5 acceptance additionally requires actual GPU execution, completed GPU verification, zero fallbacks and zero mismatches. Traced cases are never 120 FPS acceptance. Defaults and visual quality remain unchanged; no CPU affinity/cache-group tuning is used.

## 2026-10-05 CPU-only wait attribution and 16-worker test

Corrected an analysis limitation: UE 5.8 ExportTimerStatistics includes GPU timers even when CPU thread filters are supplied. Previous tables remain raw mixed-source diagnostics. New bounded CPU-event accounting separates real thread scopes, subtracts direct children for exclusive time and records parent scopes. It rejects GPU/frame tracks and invalid nesting; four accounting tests pass. All scope exports must use timers=* for valid exclusive accounting. Tool: Tools/summarize_crowd_cpu_events.py. Each subsequent exporter invocation writes a statistics warning beside the legacy CSVs. No runtime visual setting changed in this iteration.

Profile-PrimitiveWaitCpuR1 uses the existing R5 GPU trace, interval 37.1–40.1 seconds, covering 163 complete UpdatePrimitiveTransform occurrences. These are traced diagnostic observations, not new FPS acceptance:
- RenderThread 0 exclusive WaitForTasks 972.058 ms plus FTaskBase::WaitImpl_StateChangeEvent_WaitFor 985.112 ms. These are distinct exclusive event intervals on one thread (~12.01 ms per 163 scene updates), not independent pipeline stages to add to frame time.
- 363.658 ms of the latter wait is directly under FSkinningSceneExtension_FUpdater_PreGPUSceneUpdate. Local engine source confirms this waits AllocBufferSpaceTask. It does not prove buffer allocation itself consumes that duration; prerequisite/queue waits are included.
- UpdatePrimitiveTransform exclusive 34.3 ms total (~0.21 ms/update), considerably smaller than its inclusive value. Reducing transform arithmetic alone cannot recover the complete RT budget.
- Extract Pose From Anim Data and MeshObject Update are large worker-side costs; MeshObject Update also consumes 524.657 ms exclusive on GameThread in this window. Cross-worker totals overlap and cannot be reported as potential frame-time savings. GPU final matrix packing does not remove CPU pose extraction or per-mesh dynamic-data matrix construction.

Untraced Development tests use engine provider mode 0, OS default affinity/Normal priority, identical ordered cards, fixed third-person camera, 1600x1000 native viewport, 60 Hz pose cap and existing near-detail/shadows:

| Workers | Frame mean ms | P95 ms | Render Thread ms | RHI ms |
| --- | ---: | ---: | ---: | ---: |
| 8, first | 25.119 | 42.320 | 25.049 | 5.172 |
| 16 | 21.097 | 23.988 | 21.081 | 5.514 |
| 8, repeated | 19.364 | 26.346 | 19.339 | 3.897 |

All functional gates passed: 100 unique animated residents, 45 high-face residents, zero missing near-shadow components and zero packing mismatches. Repeated baseline drift 22.9%; comparison rejected. No evidence to adopt 16 workers, and 120 FPS remains unmet. No own builds, staging or trace exports overlapped these timed cases. Separate launches remain sensitive to external contention. Tools/compare_crowd_worker_cases.ps1 verifies actual ordered cards/camera/resolution/quality and records drift without automatically adopting settings. Evidence: Saved/Crowd/CpuWaitWorkerScalingR1.json.

Next implementation target: eliminate repeated upstream MeshObject Update matrix construction/copying only where exact pose revision, private bone mapping, inverse-bind matrices, active LOD, morph/deformer/cloth and previous-frame history can be preserved. Do not freeze moving transforms, drop current shadows, share incompatible private poses, or remove previous transforms merely because the current pose is unchanged. Prioritize this work over increasing RHI width or tuning physical CPU placement.

## 2026-10-05 Upstream mesh-update experiments implemented and validated

Implemented minimal cpp-only local Engine changes with reviewable/reapplicable patches in Tools/EnginePatches. Source authoring/foreign Engine modifications were preserved. No card ID cases, new animation assets, shader change, hardware placement or visual LOD changes were introduced. Both candidates remain off by default; existing Engine paths remain the gameplay default.

1. LeanDevelopmentDebugPose.patch: optional r.SkeletalMesh.CopyDebugPose=0 omits the Development-only bone/physics debug-drawing transform copy in Game. Original ReferenceToLocal/previous matrix paths are unchanged; Editor always copies. Empty debug data returns nullptr so debug overlays do not index an empty array. Default remains 1. Restore 1 before game bone/physics debug drawing. LeanDebugPoseR2 build/stage succeeded. A/B means 16.611 / 13.018 / 19.688 ms, P95 21.066 / 19.658 / 26.764 ms; repeated baseline drift 18.5% and final near-face population 43 versus 45. Comparison rejected.
2. ExactComponentMatrixCache.patch mode 1: bounded thread-local pure-conversion cache, owned FTransform source bytes and matrix values. Current/previous source arrays are independently processed; every mesh's inverse bind, private map, hidden-parent handling and LOD still run normally. Independent original-formula checks verified 982,801 reused conversions, zero output mismatches, 193,809,584 misses (~0.50% hit rate). ComponentMatrixCacheR2 build/stage succeeded. A/B means 26.064 / 19.731 / 23.706 ms, P95 39.841 / 26.549 / 34.981 ms; camera/population matched, drift 9.05%. Passing screening is not proof of causality; low coverage cannot support claiming the whole intermediate difference as saved work.
3. Same patch mode 2: only exact identity quaternion bytes plus exact unit scale qualify for direct float32 translation-matrix construction. All others use original ToMatrixWithScale. No thread cache allocation in this mode. Independent recomputation verified 1,124,581 accelerated conversions, zero mismatches/failures, 249,979,209 fallbacks (~0.45% coverage). ComponentMatrixIdentityR1 build/stage succeeded. A/B means 16.862 / 15.728 / 16.069 ms, P95 21.163 / 19.415 / 19.857 ms; camera/45 near-face population matched, drift 4.70%. Low coverage and external contention limit attribution. Mode 0 remains default.

All cases used 100 distinct animated characters, fixed third-person camera, native 1600x1000 viewport, 60 Hz pose cap and existing detail/shadow policy; missing near-shadow and packing mismatch counters were zero. No own compile/stage/export overlapped timed cases. Initial prepared warm low-only load for ComponentMatrixVerifyR1 was 9.026 s from process start; this is not cold arbitrary-card conversion proof. Source-scene/host module compile and actual Engine cpp rebuild were successful; root authoring Editor Engine was not rebuilt or replaced. No shader Cook needed for these cpp-only changes; staged prepared assets passed the real 100-character gates.

Scene control, report and PowerShell validators now expose requested/actual copy_debug_pose and component_matrix_cache modes plus independent verification counts. Failed equality disables subsequent matrix reuse and returns the original conversion for the failing sample. Counters are process-cumulative, not per-frame costs. Unsupported Engine capability is rejected; source capability detection guards optional stats linking. Both patch reverse-checks passed. Comparison script syntax and git whitespace checks passed. The candidate screenshot was inspected for room/character/near-shadow presentation; bitwise checks establish the pure-conversion result, not a full moving-scene pixel identity test.

Development 120 FPS remains unmet. These experiments show that exact duplicate conversion and exact pure translation cover too little of this workload to be the primary solution. Do not repeat cache enlargement or identity shortcuts without new profiling evidence. The remaining structural work is the volume of required pose/decompression/private palette data, per-mesh dynamic updates and the mesh-pass task dependencies; move forward from that measured workload while retaining current appearance and private rig behavior.

## 2026-10-05 Required-pose-bone audit implemented in cooked Development

Added opt-in -CrowdAuditPoseBones / run_crowd_development.ps1 -AuditPoseBones. No card IDs or hardware rules. For each independent evaluator it gathers every current LOD section palette, every leader-driven follower including invisible parts, maps private bones back to its own leader and closes parent chains. It compares that set with the actual component RequiredBones and records FillComponentSpaceTransformsRequiredBones. The probe never mutates required bones, mesh data, poses, shadows or CPU resource retention.

The second diagnostic attempts positive-weight analysis only for valid retained fixed-influence CPU buffers; discarded or variable-influence CPU data falls back to the complete palette and marks weighted_closure_complete=false. The weighted-parent count is consequently a conservative estimate, not an exact positive-weight count when incomplete. Physics, virtual bones, sockets, IK/deformers and future part changes are not permission to discard the difference. Rebuilding a component's full hierarchy can be required even when a bone has no directly weighted vertices.

Build/stage PoseBoneAuditR2, PoseBoneWeightsR1 and final PoseBoneWeightsR2 succeeded. Latest actual game evidence: Saved/Crowd/DevelopmentPoseBoneWeightsR2.json. 100 distinct animated residents / 100 evaluators; 45 high-face residents; zero missing near-shadow components, zero palette mapping gaps and zero render-palette bones absent from the existing required set. Initial prepared warm low-only load: 8.317 s from process launch; not a cold arbitrary-card conversion guarantee.

Final aggregate counts:
- Actual required and component-fill bones: 174,832 each.
- Existing render palettes plus follower/private parent closure: 158,410.
- Difference: 16,422 (9.39% of required count). This is not measured work saved or milliseconds and cannot be reported as safe removals.
- 760 component parts lacked auditable fixed CPU weight data; weighted_closure_complete=false.
- Individual leader requirements reached 3,640; the imported private rig volume remains substantial despite compact mesh references.

The optional diagnostic scans execute during report saving and are ineligible for FPS acceptance. Report fps_acceptance_eligible=false; -AuditPoseBones with -Require120 is rejected. Launcher checks leader count, required-palette containment and private mapping gaps. PS syntax and touched-file whitespace checks passed. Default gameplay does not execute the audit. No optimization was enabled or visual quality reduced on the basis of this incomplete count audit. Root authoring Editor engine/binaries were not replaced; final source/host runtime was compiled and staged.

120 FPS remains unmet. This iteration rules out blindly pruning the existing hierarchy as the main solution: even the palette count difference is only about 9.4%, and exact positive-weight data is absent from Cook. Next structural investigation must inspect positive weights in import-time editable mesh data, preserving metadata in a small manifest rather than retaining CPU mesh buffers; separately reduce repeated private pose/matrix preparation with complete current/previous and per-mesh inverse-bind validation. Any future pruning must retain non-render requirements and pass near-face/hair/animation/shadow round trips before performance adoption. The diagnostic timings are not new performance results.

## 2026-10-05 Editable weights and validated required-pose seed

Implemented an independent read-only editor audit host in Tools/PoseBoneAudit, plus Tools/run_crowd_editable_bone_audit.ps1. It reads positive editable mesh weights rather than unavailable cooked CPU buffers, maps every low/near part into its own leader and closes parents. It saves only external JSON metadata; no character packages are changed. Inputs support arbitrary asset paths. The selection convenience wrapper uses the current R9/R6 layout. PositiveWeightManifest100R1 covers 1,645 parts with zero missing mappings/unsupported editable data. All audited parts had no physics asset, sockets or virtual bones; that observation does not authorize omitting those requirements for future cards.

Original required leader total: 174,832. Editable positive-weight/parent totals: low 161,920, low plus near 164,770. Metadata is approximately 8.6 MB, including exact mesh paths, source hashes, reference names/parents and sorted indices. Private affine rigs use three bones per source skin matrix to preserve shear/reflection; these are not interchangeable duplicate bones.

WeightedRequiredPoseSeed.patch adds an optional cpp-only Engine registration API with no class-layout change. The validated seed replaces only the initial single-LOD independent SingleNode mesh seed. Complete render ActiveBoneIndices and original follower/physics/virtual/socket/shadow/parent requirements remain. Mesh or animation-mode changes fall back to normal Engine requirements. A weak registry avoids asset retention; the empty/default path avoids taking its lock. Editor does not call the optional API. Only the isolated monolithic Development game Engine was rebuilt; authoring Editor DLLs were not replaced.

Game validation checks source hashes, exact leader mesh/reference names/parents, integer indices and supported component mode. Optional verification compares every resulting RequiredBones component-space matrix byte for byte with the original path at phases 0, 0.37 and 0.99. WeightedPoseSeedVerifyR1 passed all 100 leaders with zero mismatches/failures; actual count decreased by 10,062 to 164,770 (5.76%). WeightedPoseSeedRoundTripR1 passed near face/hair restoration, low-body preservation and current-shadow checks. Its changing-detail frame times are not steady-quality FPS evidence. The screenshot was visually inspected; matrix equality is not a full pixel-identity test.

Same-process untraced A/B uses original seed / weighted seed / original seed, identical 100 ordered cards, fixed third-person native 1600x1000 TAA, 60 Hz pose cap, 45 near-face residents and current shadows. No own build/stage/audit ran during measurement; OS default affinity and Normal priority remain.

| Trial / seed | Frame mean ms | P95 ms | Render Thread ms | RHI ms |
| --- | ---: | ---: | ---: | ---: |
| ABR1 original first | 15.501 | 18.542 | 15.496 | 3.259 |
| ABR1 weighted | 14.279 | 17.861 | 14.274 | 3.068 |
| ABR1 original repeat | 17.509 | 22.473 | 17.500 | 3.655 |
| ABR2 original first | 20.048 | 29.730 | 20.044 | 3.961 |
| ABR2 weighted | 19.247 | 26.364 | 19.238 | 3.848 |
| ABR2 original repeat | 21.865 | 30.618 | 21.812 | 4.156 |

ABR1 baseline drift 12.95% rejects the comparison. ABR2 drift 9.06% passes screening but is larger than the first-baseline candidate difference; it does not establish causal whole-frame savings. Required count restored from 164,770 to 174,832 when disabled. All cases passed population/current-shadow gates with zero seed failures. The optimization remains off by default and Development 120 FPS is unmet. Usage/reapplication details: Tools/EnginePatches/WeightedRequiredPoseSeed.md.

## Actual codec identification before further animation work

AnimationTrackPilotR3 successfully built and ran in the isolated audit host with required AnimationData/ControlRig dependencies. Its valid AnimationSequencerDataModel has an empty legacy raw-track view, while compressed data contains 2,309 tracks (6,899,388 bytes); 1,594 compressed tracks map to its low leader. Empty legacy data is not an empty animation and must not trigger destructive clip rebuilding.

Final WeightedPoseCodecReportR1 game build/stage and actual 100-character verification passed: **all 100 current clips use AnimCompress_BitwiseCompressOnly**, with 245,648 compressed tracks total. The earlier ACL whole-pose hypothesis does not apply to this measured workload. Local constant-key Bitwise decoding iterates requested track pairs (or sends their count to ISPC), so excess stored tracks do not prove excess full-clip decompression every frame. Blindly removing/recompressing stored tracks is not a demonstrated RT optimization.

DevelopmentWeightedPoseCodecReportR1.json reports 100 distinct animated residents, 100 validated/verified seeds, zero seed failures, 164,770 live required bones, 45 near-face residents and zero missing current-shadow components. Prepared warm low-only load was 8.265 seconds from process start. Initial high meshes remain absent. This does not prove cold loading or first-time arbitrary-card conversion. Its standalone mean 13.471 ms / P95 19.550 ms is a functional diagnostic observation, not an A/B improvement claim or 120 FPS acceptance.

Next optimization work must profile the actual requested-track Bitwise decode and upstream per-mesh current/previous palette preparation. Do not optimize unused ACL code, enlarge low-hit conversion caches, assume common base skeleton makes private affine/physical palettes identical, or replace appearance with a shared pose. No quality, animation-frequency or hardware-specific policy was changed in this iteration.

Final tool checks: WeightedPosePilotFinalR1 passed the corrected actual-population launcher gates (one loaded/animated leader, one applied/verified seed, zero failures, 1,567 live required bones). The normal launch still independently requires 100 distinct cards. Four touched PowerShell scripts passed parsing; touched-file whitespace checks and Engine patch reverse-check passed. Source scene and isolated host scene SHA256 matched. Owned audit/game/build processes finished.

## 2026-10-05 Actual Bitwise subphase profile and constant-scale implementation

Added independent CPU trace scopes for Bitwise translations, rotations and scales in AnimCompress.cpp. The profiling-only patch is BitwiseDecodeScopes.patch; the complete ConstantScaleCache.patch includes those scopes and must not be stacked with the profiling-only patch. No Engine header/layout or shader change was needed. Authoring Editor Engine DLLs were not rebuilt/replaced. New monolithic game build/stage BitwiseDecoderControlR1 and ConstantScaleCacheR2 succeeded; the first scale-cache compile failure and tooling corrections are recorded in CROWD_GPU_POSE_ERRORS.md.

CPU-only bounded accounting, Profile-BitwiseDecodeCpuR1-Scopes.csv, interval 38–41 seconds, full timer nesting:

| Scope | Calls | Cross-thread exclusive work ms |
| --- | ---: | ---: |
| BitwiseDecodeTranslations | 17,392 | 1,498.15 |
| BitwiseDecodeRotations | 17,392 | 1,510.50 |
| BitwiseDecodeScales | 17,392 | 1,233.69 |
| Extract Pose From Anim Data, excluding new children | 17,392 | 13.58 |
| Build Anim Track Pairs | 17,392 | 275.65 |
| MeshObject Update, excluding children | 109,495 | 2,088.14 |
| SkelMeshCopyDebugPose | 109,545 | 1,404.44 |

These totals overlap across worker threads and cannot be added to frame time or interpreted as saved milliseconds. The trace confirms all three decode phases are substantial, while rebuilding track pairs is smaller. Render Thread still spends much of its critical path waiting for worker/update tasks. The new scopes do not measure FPS benefit.

Implemented an explicit same-process a.ConstantKeyLerp.ISPC control and actual-mode report. BitwiseDecoderABR1 used SIMD/scalar/SIMD with matching camera, 45 near-face residents and current shadows. Frame means 16.785 / 16.294 / 16.054 ms; repeated baseline drift 4.36%. The scalar candidate difference is within drift. BitwiseComparePilotR1 in the independent read-only editor host compared six times including interpolation and loop endpoints: 13,854 float matrices, 3,149 nonidentical, all finite, maximum element error 0.000132936. Scalar switching is not adopted.

The same pilot found 614 constant translations, 597 constant rotations and 1,784 constant scales among 2,309 tracks. Implemented runtime single-key scale caching instead of changing decoder mode or building a large animation bank. Registration decodes once with the existing codec. Per-pose updates copy those exact native values and send remaining requested scale pairs through the original codec. Translation, rotation, parent evaluation, private rigs, retargeting, root motion, morphs, current/previous matrices and render policies remain on their normal paths.

The cache is per immutable cooked compressed animation, with weak ownership and buffer-address/size/mode checks. It does not retain assets. Unsupported codecs, changed data/mode or invalid owners fall back. Diagnostic original-decode comparisons are byte-exact native FVector comparisons; any mismatch restores that call's original output and disables later reuse. Scene/PowerShell checks reject failed experiments. No card-ID cases, affinity tuning, quality reduction or animation-rate change was introduced.

ConstantScaleVerifyR1: 100 distinct animated residents, 100 registered clips, zero unsupported clips; 221,993,505 constant scale hits and 86,499,558 variable scale items. Original-codec verification checked 308,493,063 scale vectors with zero mismatches/failures. Near face/hair round-trip, low-body preservation and current-shadow gates passed (37 near faces at final moving-scene report, zero missing current shadows). Prepared warm initial low-only load was 7.850 seconds. Its changing-detail/equality-diagnostic frame times are not benchmark evidence. This proves neither first-time arbitrary-card conversion nor cold loading under one minute.

ConstantScaleABR1, untraced/off-on-off, same ordered 100 cards/fixed third-person native 1600x1000 TAA/60 Hz/8 workers/45 near faces/current shadows, no own builds/staging/audits overlapping timed cases:

| Cache | Frame mean ms | P95 ms | Game Thread ms | Render Thread ms | RHI ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Off first | 16.310 | 19.472 | 10.993 | 16.303 | 3.450 |
| On | 15.548 | 18.718 | 10.441 | 15.541 | 3.328 |
| Off repeat | 16.129 | 19.347 | 10.930 | 16.122 | 3.407 |

All functional gates passed, zero cache failures/mismatches. Baseline drift 1.11%; candidate mean is 3.61–4.67% below the two baselines. This is a promising first comparison, not proof of a universal steady FPS improvement. A reversed-order repetition is required before default adoption. Development 120 FPS remains unmet; the target budget is 8.33 ms and upstream per-mesh data/update work remains substantial. Usage and limitations: Tools/EnginePatches/ConstantScaleCache.md.

Reversed ConstantScaleABR2 completed with the same 100-card/camera/quality/45-near-face/current-shadow gates and zero cache failures. On/off/on frame means 15.047 / 16.018 / 15.252 ms; P95 18.312 / 19.463 / 18.289 ms. Render Thread 15.040 / 16.011 / 15.244 ms; RHI 3.177 / 3.320 / 3.209 ms. Repeated on-baseline drift 1.37%. Both orders show RT reductions larger than their own baseline drift, approximately 0.6–1.0 ms in this selection/environment. This supports adopting the exact single-key scale cache, not claiming 120 FPS or universal timings.

Compatible non-Editor game crowd scenes now register/enable this cache by default; run_crowd_development.ps1 mirrors the default and supports -ConstantScaleCache:$false. -CrowdDisableConstantScaleCache restores the original game path. Engine-wide default remains off and Editor does not register assets. The provider comparison wrapper explicitly opts out when no scale-cache case list is supplied, preserving its historical baseline. Unsupported clips retain normal decoding. New report fps_acceptance_eligible excludes detailed traces, weight audits and scale-equality diagnostics. Default adoption source build/stage ConstantScaleDefaultR1 succeeded. The functional screenshot was inspected for character/room/current-shadow presentation; the exact scale checks establish numerical output rather than full moving-scene pixel identity.

### Development debug-pose copy retest with stable baselines

The older debug-copy comparison was rejected for 18.5% drift and mismatched near-face population. The new actual CPU-only profile still showed 1,404.44 ms of cross-thread debug-copy work over three seconds. Repeated this unresolved candidate with the validated scale cache held on in every case, same ordered 100 characters/fixed camera/native resolution/60 Hz/45 near faces/current shadows. No own compile/stage/export/audit overlapped timing.

ConstantScaleDebugABR1, copy on/off/on:

| Debug copy | Frame mean ms | P95 ms | Game Thread ms | Render Thread ms | RHI ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| On first | 15.540 | 18.597 | 10.440 | 15.532 | 3.383 |
| Off | 10.386 | 13.293 | 7.343 | 10.375 | 2.619 |
| On repeat | 15.382 | 18.226 | 10.328 | 15.374 | 3.342 |

Baseline drift 1.01%, camera/near-face population matched, zero missing current shadows or cache failures. The off screenshot was inspected: player, nearby varied characters, face/hair detail, room and current shadows remain present. This candidate removes the Development-only debug drawing transform copy; normal reference/current/previous skin matrices, morph/cloth paths and authoritative component-space poses are unaffected. Bone/physics debug drawing requires restoring the copy flag. A reversed-order repeat remains necessary before changing the game default. It has not met 120 FPS: even the off mean and P95 exceed 8.33 ms.

ConstantScaleDebugABR2 reversed off/on/off also passed all same-camera/population/current-shadow gates: frame means 10.382 / 15.783 / 10.635 ms; P95 13.808 / 19.574 / 14.835 ms; Game Thread 7.281 / 10.606 / 7.452 ms; Render Thread 10.362 / 15.776 / 10.617 ms; RHI 2.667 / 3.352 / 2.741 ms. Baseline drift 2.44%, substantially smaller than the reproduced difference. No copy-path quality reduction was introduced.

The crowd game now defaults to omitting this Development debug-only copy; Engine-wide default remains 1 and authoring Editor retains original copying. -CrowdKeepDebugPose / launcher -KeepDebugPose restores it before debug overlay use. Historical comparison sessions without an explicit debug-copy case list keep copying to isolate other experiments; the new default is not silently substituted into those baselines. Both exact-scale reuse and no-debug-copy policies are generic across compatible random card selections. Tested timings apply to this prepared 100-card selection/environment, not every future selection. Further GPU packing comparisons must explicitly hold scale reuse on and debug copy off now that the bottleneck changed.

### GPU matrix packing retest after removing debug-copy work

CrowdLeanGameplayR1 isolated Game source build/stage succeeded. LeanGameplayGpuABR1 held scale reuse on, debug copying off and the same native third-person 100-card/45-near-face/current-shadow configuration. CPU/GPU/CPU provider modes 0/5/0:

| Provider | Frame mean ms | P95 ms | Render Thread ms | RHI ms | GPU ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPU first | 10.704 | 14.721 | 10.686 | 2.722 | 1.645 |
| GPU mode 5 | 11.115 | 16.617 | 11.108 | 2.872 | 1.645 |
| CPU repeat | 10.437 | 14.260 | 10.406 | 2.694 | 1.636 |

Baseline drift 2.50%; camera/population matched. GPU executed and verified 655 proxies with zero fallbacks/mismatches, but this retest does not show a gain. Mode 5 remains off. Correct execution is not sufficient to adopt a GPU path; it still consumes CPU dynamic-data construction and transfer work. Do not repeat enabling it as an assumed improvement without changes to that upstream work.

The adopted game path remains original CPU provider, exact constant-scale reuse and no Development debug-copy, with OS default affinity/Normal priority/8 workers and no visual setting changes. Observed current frame mean is about 10.4–10.7 ms, P95 about 13.3–14.8 ms in the isolated comparisons; 120 FPS remains unmet. Next profile must use this adopted path, not attribute old full-debug-copy waits to the new critical path. The remaining targets are upstream current/previous private-palette construction, per-mesh copies and their Render Thread dependencies; shader-side packing alone does not remove those costs.

Final default-policy validation, DevelopmentLeanGameplayFinalVerifyR1.json: 100 loaded/distinct animated residents, default constant-scale cache=1 with 100 prepared clips, default game debug copy=0/effective=false, player_control=true, movement_test_met=true (WASD/idle/diagonal checks), face_round_trip_met=true, body_stays_low=true, zero missing current-shadow components. Native scale verification checked 311,891,991 items with zero mismatches/failures. Prepared warm initial low-only load was 7.422 seconds from process start. The diagnostic moved the camera and switched near details: its mean 4.576/P95 7.722 ms does **not** qualify as 120 FPS evidence. fps_acceptance_eligible=false and target_met=false correctly prevent that false conclusion. No arbitrary-card cold-load/first-conversion promise is made.

Final source/host SHA256 matched, five PowerShell script parsers and touched-file whitespace checks passed, complete constant-scale and debug-copy Engine patches passed reverse-checks. Build/stage CrowdLeanGameplayR1 succeeded and its exact staged binary was used in the final GPU and gameplay tests. Owned games/audits/builds completed. Foreign Editor binaries, authoring character assets and other sessions were preserved. No affinity/cache-group/priority/hardware model tuning was used.
