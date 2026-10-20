# Crowd Room independent behavior demo — 2026-10-06

## Implementation

Launch `CrowdRoomDevelopmentBehaviors.cmd`. This uses native Cooked Development, all 100 distinct R4 characters, original high face, existing near shadows and material policy. WASD / right mouse and F8 CPU/GPU / F9 coverage are retained. Existing launchers keep the continuous-walk baseline.

Each non-player resident has a private seeded random state, walking rate 0.8–1.15, walking periods 5–13 seconds, pauses 2.5–6 seconds, initial stagger 3–11 seconds. Walking alternates with rest or procedural whole-body looking. Half-second braking/resuming integrates per-resident path time, so paused time does not teleport the character ahead. Looking fades after braking and before resuming; this is whole-body yaw, not head IK. Routes retain the existing separated circular cells inside the room. No collision avoidance/social interaction is claimed.

Animation selection inspects the current resident's library and outfit. Walk / idle / stand / tachi names are recognized. GPU candidates must exist in that character's private bank with exact clip path and duration. No shared pose assumption, foreign card asset substitution, runtime geometry generation or extra high-model loading. Unsupported clips are not selected, preventing old palettes from persisting after an invalid clip switch. Existing GPU clock restoration now guards asset identity before restoring time.

## Current animation limitation

Published R4 libraries currently provide walking only. The visible demo changes walking speed, stops and looks around. Rest is a paused walking pose; it is **not an authored standing, chat or gesture clip**. `behavior_missing_idle_choices` reports this explicitly. Multi-clip selection is implemented, but authored idle/emote banks and their transitions are not yet visually accepted. Adding those requires private per-character baking/export and Cook; selecting another character's animation is not a valid shortcut. Complete animation-variety acceptance remains outstanding.

## Functional verification

- Build BehaviorR355 passed; immutable stage BehaviorFinalR356.
- Native final report: DevelopmentBehaviorFinalGpuR357.json, complete=true, loaded=100, animated=99 (stationary WASD player intentionally paused).
- Runtime route oracle simulates 100 independent residents for 60 seconds at 120 Hz; finite monotonic route time, no movement while rate is zero, and repeated stop/restart checks passed.
- Final scene transitions: 386, stops: 207, starts: 179.
- GPU-owned clocks: 100; CPU pose evaluations: 0; render fallbacks: 0.
- CPU/GPU/CPU functional harness BehaviorToggleR354 passed on the immediately prior BehaviorR351 stage. All phases completed with behavior transitions, correct 0/100/0 ownership. Concurrent compilation affected timing, so these capped phases are not performance comparison evidence. The final executable adds GPU clip-membership preflight and is separately verified by R357.
- Final native mean 13.191 ms; P95 18.235 ms; process load 13.250 seconds. No performance gain or stable 120 FPS acceptance is claimed; target 8.33 ms remains unmet.
- Launch descriptor contains the verified executable SHA256. Launcher ValidateOnly passed.

## Deferred acceptance

Full temporal CPU/GPU image comparison of multiple authored action clips, idle pose quality, blended clip transitions, wandering/social behavior, and quiet repeatable 120 FPS acceptance remain outstanding. Current stopping/looking and clock ownership functionality are implemented; geometry, clothing intersections and previous hair speckles are unchanged.

## Runtime APNG correction — 2026-10-06

The player selects arbitrary portable APNG cards. The fixed `/Game/.../C###` Cooked crowd and Editor snapshot bake are correctness fixtures, not the production loading implementation. `CrowdBehaviorBake` uses UnrealEd, animation data controllers and SavePackage; it must not be called by a packaged game. Its all-card bake is retained only as a comparison corpus.

Implementation order:
1. Admit runtime-created, private GPU animation banks directly from memory, with asynchronous joint validation, bounded bytes and exact selected identities. Implemented in `OKPrepareRuntimeGpuAnimationBundleAsync` / `OKCreateCrowdIndependentGpuFromRuntime`; native Development build R379 passed. Existing rendering, complete-character ownership preflight and CPU/GPU switching remain shared.
2. Connect actual APNG loading to runtime low-mesh/render resources and stable content-derived private bone/mesh/clip identities. No dependency on pre-imported C### packages. NOT implemented in this change.
3. Prepare only chosen outfit, walking clip and one suitable idle/action at runtime. Source curves can ship with the game; character-specific matrix banks remain private. Background jobs must own independent sessions; UObject/render-resource publication must obey their thread requirements. No process-global environment policy toggles in concurrent runtime jobs.
4. Cache low geometry/materials and private banks by APNG payload identity, outfit, geometry/material recipe, source clip identity and converter/schema version. A changed card or recipe invalidates cached products. Cache hits are optional acceleration; a new APNG must work without Editor/Cook or external KKS files.
5. Validate cold APNG selection-to-controllable-100 load <=60s, temporal CPU/GPU images, hand accessories, face/hair, and native Development mean/P95 <=8.33ms. Include all runtime preparation/upload time. Existing 13.250s Cooked load is not APNG cold-load evidence.

The new in-memory entry does not itself read APNG, generate geometry, generate animation keys or solve clip streaming. Producers must enforce allocation budgets before creating buffers; admission validation limits upload but cannot undo peak allocations that already occurred. `UAnimSequence` and imported-actor playback metadata are still required by the current bridge, so Editor-generated sequences cannot be substituted as a finished runtime solution.

Long idle R378 remains unaccepted: GPU matrix error 0.00114369 exceeds the 0.001 gate despite vertex error 0.000432cm. Sparse-key savings were insufficient for full long-idle banks. Do not publish that cache or claim idle visual acceptance. Future runtime clip preparation must be bounded rather than preload all future twenty styles for every actor.

### Scope adjustment and regression evidence

The R373 all-card Editor bake was stopped after the runtime requirement; 46 completed fixture libraries were preserved and none published. Receipt: `Saved/Crowd/BehaviorBake-SnapshotAllR373.cancelled.json`. This is an intentional scope change, not successful 100-card behavior acceptance. No existing player launcher was replaced.

Geometry reduction also currently uses the Editor-only `OKPackCrowd -OptimizeQuality` tool. Therefore runtime geometry generation is a separate required component, not something already provided by the new GPU-bank entry. Existing runtime skeletal construction in KKUnityBundle is a candidate renderer integration; using it must preserve original face geometry, private weights and hand-accessory bindings without reverting to the preview rendering path.

R381 runtime admission regression passed: one audited private bank accepted from memory on a worker; stale identity, duplicate selection, 1-byte budget and malformed track were rejected. Worker validation measured 8.7374ms for this one-bank fixture; this is preparation time, not frame time or 100-APNG loading time. Proof `Saved/Crowd/RuntimeBankR381.json`. Native Development build R379 passed in 27.78s. APNG end-to-end and gameplay invocation remain unverified/unconnected.

## Native APNG preparation progress — 2026-10-06

Implemented generic runtime producers:
- `KKRuntimePose` is a separate Runtime module with Core/CoreUObject/Engine dependencies and the native extractor library. No UnrealEd, material graph generation, UAnimSequence authoring/compression or SavePackage dependency.
- Portable APNG -> private animation snapshot can run asynchronously. The resulting rig identity hashes the actual snapshot bytes.
- Private snapshot + original curve bytes + plain bone/part metadata -> 60 Hz GPU keys/tracks runs in an independent native session on a worker. Signed affine weights and three-column palette representation remain supported. Rigid bind references are supplied by the character builder; no card-specific attachment exceptions.
- `OKBuildRuntimeGpuActorBank` assembles arbitrary private clips, rebases offsets and validates joint semantics. Incorrect hierarchy fails without replacing the previous bank. Existing in-memory GPU admission retains exact selected-identity and byte-budget checks.
- Keys collapse only bitwise constant tracks. No lossy sparse-key approximation or reduced animation rate.

R383 actual C011 APNG functional fixture passed without external KKS root or saved sequence creation: APNG open/snapshot 1462.129ms; five-second standing pose preparation 1400.555ms; repeat 1398.484ms; 2309 tracks, 559 constant, 301 samples, 25,310,832 key bytes. It used imported metadata as a fixture and did not render/verify GPU rows. Do not extrapolate a 100-card loading result.

R385 two-private-clip bank assembly/admission passed: clip offset rebasing, stale/duplicate selection rejection, byte budget rejection, malformed track rejection and preservation of prior bank on bad hierarchy. Fixture prepare validation 9.719ms, not frame time.

R386 tried enabling the complete legacy KKUnityBundle in native Development and failed on Editor-only material expression and animation data-model APIs plus an Editor test include. Corrective architecture is a separate runtime preparation module, not hiding missing operations behind no-op stubs. R387 independent-runtime native Development build passed in 67.49s. Stage R389 is a regression executable snapshot only; no player launcher was replaced.

Remaining production work is explicit: generate the metadata and low render resources from arbitrary APNG in runtime, enforce session-local clothing/physics policy, choose/stream bounded active clips, integrate logical playback without a cooked UAnimSequence dependency, and verify actual 100-card rendered scene cold load/frame timing/images. The old scene remains Cooked fixture gameplay. A 25.3MB single idle already demonstrates why all future clips cannot be blindly made resident under the current 2GiB aggregate admission cap.

## Native APNG background preparation R394 / automatic rig follow-up

R394 passed in non-Editor Development: actual C011 APNG with an empty KKS root, both snapshot and pose jobs on worker threads, no cooked character load. Warm single-card snapshot preparation was 253.4635 ms; pose preparation 1312.4983 ms; 2309 tracks / 25,310,832 key bytes. This used a plain metadata fixture and does not establish scene integration, 100-card loading time, GPU visual equivalence, or frame rate.

The next runtime path derives native bones, exact palette slots and neutral rigid attachment inverse binds directly from each APNG snapshot. The regression can take one shipped raw animation curve resource through `-CrowdRuntimePoseCurves=` instead of a per-card metadata recipe. This change is under native compile/test; it is not yet enabled in the player launcher.

## Runtime preparation R404 and GPU matrix harness R400

R404 passed native Development worker preparation for C011, C002 and C000 using actual APNG files and one common 5-second shipped authored idle curve. No per-card metadata fixture or cooked character was loaded. Automatic rig counts differ from old imported fixtures because discovery includes all currently visible bound renderers (C011: 2315 bones). Reports and input/executable SHA256 receipts: `Saved/Crowd/RuntimeApngAutoR404/summary.json`. The helper `Tools/test_runtime_apng_pose.ps1` reproduces this bounded regression. This is warm preparation while other work is running, not a cold100 load or frame benchmark.

R400 passed the repaired Editor-host runtime sampler GPU matrix audit: six phases including both clip ends, maximum component-matrix error 0.0000617047 against the CPU decode, below unchanged 0.001. Determinism, finite data and preallocation budget rejection also passed. Its bone metadata remains a fixture; actual rendered skin, face/material/hand images and native runtime-generated rig comparisons remain separate gates.

## Native GPU APNG regression R408

All three cards passed native Development GPU readback at six phases, using runtime-discovered skeleton/palette metadata rather than per-card fixtures. C011: 2315 bones, 25,397,520 key bytes, maximum matrix error 0.0000617047. C002: 2541 bones, 28,835,568 bytes, error 0.0000519100. C000: 2694 bones, 31,046,112 bytes, error 0.0000747419. Each retains private keys and transforms; a common authored curve resource is only the animation input. Receipts: `Saved/Crowd/RuntimeApngGpuR408/summary.json`; all retain `scene_integration_verified=false` and `visual_equivalence_verified=false`.

Reproduction: build with `Tools/build_crowd_development_host.ps1 -RuntimeApng -Tag <fresh-tag>`, snapshot its executable, and run `Tools/test_runtime_apng_pose.ps1` with actual APNG paths, raw shipped curve bytes, `-GpuCheck` and the matching `-ShaderPatchDirectory`. The default host option continues the existing cooked-character build; neither option replaces the player release.

## Final current-function regression R412 / R414

The reproducible `-RuntimeApng` build helper passed native Development (R412, 68.87s). Its fresh immutable executable snapshot R413 passed all three APNG native GPU checks again (R414), after the shared native library was rebuilt. Common curve bytes are now preserved as a game resource at `Content/IndependentResources/RuntimeAnimations/idle_02.okanim` with SHA256 and clip provenance in its manifest, so reproduction need not use per-card recipe fixtures.

| APNG | Runtime bones | Snapshot/rig ms | Pose preparation ms | Maximum GPU matrix error |
|---|---:|---:|---:|---:|
| C011.png | 2315 | 639.099 | 1455.653 | 0.000061705 |
| C002.png | 2541 | 602.225 | 1341.604 | 0.000051910 |
| C000.png | 2694 | 1034.330 | 2309.929 | 0.000074742 |

These are individual preparation timings with concurrent unrelated work, not frame timings or a100-card cold-load measurement. The current function is verified; actual low mesh/material generation, bounded active animation residency, cooked-sequence-free scene playback, rendered CPU/GPU image parity, and final100-card scene performance acceptance remain pending. No player launcher/release has been replaced. The last previous100-character native scene mean13.19087ms/P9518.2353ms (R357) is still a failing120FPS baseline; R414 provides no new mean/P95 frame claim.

## Runtime private playback / multi-clip plan (2026-10-06)

Implement a private, sequence-free logical clock in KKCrowdGpuPacking; advance only clip indices, per-clip time, rate and blend weight. Retain each APNG's independently prepared keys/palettes. Validate looping, pause, reverse/rate changes, non-loop end clamping, invalid inputs preserving state, and100 separate clocks using20 catalog indices. During a two-input blend, queue the latest incoming clip request rather than abruptly replacing a three-pose state with an unsupported two-pose blend.

For real GPU validation, export the application's common authored walking curve once (no character bake), then prepare idle/walk privately from arbitrary APNG at runtime and assemble a two-clip bank. Compare CPU decode and resident GPU matrices across15 samples covering steady poses, transition start/intermediate/end, pause and return transition. Keep the existing0.001 matrix threshold; scene image/quality and frame benchmarks remain later gates.

R416 failed compile and is recorded in CROWD_GPU_POSE_ERRORS.md. Corrected R417 native Development passed. R419 passed three APNGs with two actual authored clips and all clock oracle checks; maximum matrix errors were0.000092412(C011),0.000087569(C002),0.000094594(C000). R420 adds explicit total bank key bytes, pause reuse rejection and reverse-clock coverage for the final current-function rerun.

## Current-function acceptance R420 / R422

R420 native Development build passed(18.06s). Fresh executable R421 passed R422 on three APNGs with two private authored clips:5-second idle and2.066666842-second walk. Each audit compares15 resident GPU samples with CPU local-TRS decode and hierarchy composition. Loop/rate/reverse/pause/non-loop/queued-transition/invalid-input preservation checks passed;100 independent logical clocks covered20 catalog indices. Paused playback was required to reuse a resident GPU pose. No UAnimSequence/cooked character/per-card rig fixture is used by this path.

| APNG | Two private clips key bytes | Maximum GPU matrix error |
|---|---:|---:|
| C011.png | 36,194,640 | 0.000092412 |
| C002.png | 41,071,536 | 0.000087569 |
| C000.png | 44,207,424 | 0.000094594 |

The20-index oracle is logic coverage, not20 authored styles or100 rendered characters. Actual scene integration/images and average/P95 frame benchmarks remain pending. The three full-source rigs also demonstrate why all clips cannot simply stay resident for100 characters: two clips already take36–44MB per card, before palettes/geometry/materials. Next production work must compact the runtime render rig and bound active/blending/preloaded clip residency; do not bypass the2GiB admission cap or load every future style at startup. Runtime scene low mesh/material generation and replacement of the old scene clock remain unfinished.

The generic common-curve export option accepts an explicit Unity bundle Source and clip name. R423 successfully re-exported walking with explicit Source, producing byte-identical data to the shipped walk_01.okanim. It is an application-resource authoring step performed once, not per-card preprocessing. Native player preparation still uses only APNG plus those shipped bytes with an empty KKS root.

## Runtime CPU pose residency admission plan / implementation R424

Add a general shared cache keyed by private character identity, clip identity and a versioned recipe hash of snapshot bytes, curve bytes, sampling rate/duration and exact bone/palette binding metadata. Preparation jobs reserve conservative key/track capacity before worker start. Publishing accounts for allocated array capacity, rejects stale tickets and mismatched recipes, and converts the reservation to resident buffer usage. Shared immutable acquisition handles pin playback/blend/upload source buffers; only unpinned entries are eligible for LRU eviction. Insufficient pinned/reserved capacity defers new work without evicting unrelated entries.

Cancellation contract: release a reservation only after the worker is completed/joined, or before a worker was started. Merely requesting cancellation must not release capacity while the worker can still allocate. GPU copies, native sessions/input snapshots and render fences remain separately budgeted; this is CPU key/track admission, not a complete GPU residency manager.

R424 native Development build passed(42.70s). The actual APNG regression reserves before each private preparation, publishes moved results without duplicating cache payloads and pins the first clip while preparing/blending the second. A generic deterministic cache oracle covers pinned-budget rejection, release/LRU eviction, duplicate pending work, stale publication/cancellation and incompatible recipe rejection. No per-card fixes are introduced.


## Current-function acceptance R426: CPU pose cache with native APNG GPU playback

Fresh R425 executable snapshot of the R424 native Development build passed all three actual APNG regressions. Both authored clips were prepared privately on workers; the first clip remained pinned during second-clip preparation and GPU blending. Cache policy rejection/eviction checks, private clock checks and GPU matrix readback passed. Every report ended with zero pending preparation reservations.

| APNG | Resident CPU key/track bytes | Peak resident + reserved bytes | Maximum GPU matrix error |
|---|---:|---:|---:|
| C011.png | 36,282,368 | 39,478,672 | 0.000092412 |
| C002.png | 41,156,608 | 44,285,680 | 0.000087569 |
| C000.png | 44,302,336 | 47,429,408 | 0.000094594 |

Receipt: `Saved/Crowd/RuntimePoseCacheR426/summary.json`. The reusable test helper now requires cache verification, zero pending reservations and (when two clips are requested) logical playback verification. Summary completion requires all helper gates to pass, including after a failure in a gate beyond the probe's own passed flag.

This verifies CPU pose cache admission/residency only. GPU buffer upload/fence residency, actual runtime mesh/material scene integration and rendered image parity remain separate unfinished work. No scene average/P95 frame measurement was taken in R426. The last scene baseline remains R357 mean 13.19087 ms / P95 18.2353 ms, which does not meet 120 FPS. Individual warm preparation timings are not a 100-card cold-load guarantee. The player launcher/release was not replaced.


## GPU pose buffer admission plan / implementation R427-R428

Add a per-pose-owner logical buffer cap before any initialization upload. Account keys/tracks/actor/task/inverse-bind buffers, two pose output buffers and one actor-selection replacement. Reinitialization conservatively includes the old bank plus both old pose outputs. Reject excess before creating new GPU buffers, retaining old generation, upload counters and playback resources. Shared immutable initialization uses the same conservative per-owner rule (shared bytes are deliberately overcounted). Resident logical bytes and peak admission bytes are distinct from cumulative uploaded bytes.

Native APNG GPU regression now explicitly rejects a one-byte-budget replacement, verifies that generation/initialization/upload counts remain unchanged, then evaluates the retained pose again. Report GPU admission verification and byte counters. Test helper requires these checks for GPU runs. Compilation R427 failed on a missing direct RenderGraphBuilder include; repaired and recorded before R428.

Scope: this is per-owner initialization admission, not aggregate 100-character GPU residency, retirement fences, a strict global VRAM limit or driver allocation telemetry. RDG/RHI retain queued resources; repeated replacements and release do not prove GPU completion. Global retirement accounting and bounded clip streaming remain production work. No graphics setting, key precision or mesh quality is changed.


## Current-function acceptance R431 / R433: retained GPU bank admission

R431 native Development build passed (27.41s); immutable executable snapshot R432 passed all three actual APNG two-private-clip GPU regressions R433. A rejected one-byte-cap replacement preserved generation, initialization count and immutable upload bytes. Subsequent GPU rows were bitwise identical to the last pre-rejection rows. Existing 15-sample animation/transition CPU-versus-GPU matrix checks, pause reuse, CPU cache and private logical clock checks also passed. The helper complete flag is true only after these gates. Default cap remains compatible with the existing 2 GiB-minus-one bundle ceiling; explicit smaller limits remain supported.

| APNG | Current logical GPU buffer bytes | Peak planned initialization bytes | Maximum matrix error |
|---|---:|---:|---:|
| C011.png | 36,528,016 | 36,639,152 | 0.000092412 |
| C002.png | 41,437,456 | 41,559,440 | 0.000087569 |
| C000.png | 44,595,376 | 44,724,704 | 0.000094594 |

Receipt: `Saved/Crowd/RuntimeGpuAdmissionR433/summary.json`. These byte counts describe this diagnostic pose bank, not skin-palette renderer buffers, all GPU memory or measured driver VRAM. This is not a performance optimization claim: no new frame benchmark or rendered image acceptance was run. Remaining aggregate fence retirement/clip streaming, runtime scene meshes/materials and sequence-free scene integration are not complete. Player launcher remains unchanged. Last actual scene mean is still 13.19087 ms / P95 18.2353 ms (R357), above the 8.33 ms goal.


## Aggregate GPU residency and retirement plan / implementation R434-R441

Introduce a render-thread aggregate logical bank budget. Managed banks reserve before uploading, include immutable bank bytes, two pose outputs and actor-selection replacement, and retain that charge while active. Retire holds pooled-buffer references and writes a GPU fence after the last executed graphics-queue graph. Collection only returns capacity after the fence polls complete, without blocking gameplay. Unsubmitted reservations can be cancelled; stale cancellation is rejected. Destruction of a managed pose also queues retirement instead of silently returning its lease.

The module owns a process-wide crowd budget and collects at render-frame end, so collection continues after scene objects are removed. Scene current/history poses and diagnostic probe poses use this same manager; shared immutable history is conservatively charged as a full owner. The module removes its frame callback on the render thread during shutdown. Only managed banks are covered; skin-palette/renderer/driver allocations remain outside this logical bank cap. This implementation currently uses the graphics queue, not async compute.

R436 native build passed (18.41s), and R438 passed three APNG GPU regressions. Each keeps two real uploaded banks resident, rejects a third before upload, preserves current pose rows, marks both released banks retiring with the full charge still held, and then verifies zero charge/retiring banks after GPU fences complete. Aggregate peaks: C011 73,278,304 bytes; C002 83,118,880; C000 89,449,408. Maximum pose matrix error remains below 0.000095. Receipt: `Saved/Crowd/RuntimeGpuRetirementR438/summary.json`.

R439 native build passed (22.54s) with the existing scene current/history path routed into the shared manager. R440 fixes shutdown callback/global-pointer race exposure; R441 will include scene admission diagnostics after a bounded retry of a transient source-file sharing failure. Final native APNG and bounded scene regression are pending; no player release replaced. This is bank-lifetime safety/infrastructure, not evidence of lower frame time. Runtime APNG low mesh/material scene construction and sequence-free gameplay integration remain pending.


## Final current-function acceptance R450 / R452 / R453

R450 native Development build passed (20.86s). Final executable R451 passed all three APNG regressions R452. The aggregate manager now counts shared immutable data once, charging each history only its private output/selection allowance. Dependent leases protect the original bank until every user retires and all required fences complete. Unused parent cancellation while a child exists is rejected. Independent private banks remain independent and are charged in full.

| APNG | Aggregate peak logical bank bytes | Final charged bytes | Final retiring banks | Maximum matrix error |
|---|---:|---:|---:|---:|
| C011.png | 73,500,560 | 0 | 0 | 0.000092412 |
| C002.png | 83,362,832 | 0 | 0 | 0.000087569 |
| C000.png | 89,708,048 | 0 | 0 | 0.000094594 |

R453 existing third-person hundred-character room functional gate passed: 100 loaded/animated, 100 GPU actors/private clocks, backend ready, zero render fallback and zero CPU pose evaluations. Scene admission logged two owners with 1,201,776,160 charged logical bytes, below the unchanged aggregate ceiling. This verifies current/history scene connection, not arbitrary APNG runtime-generated scene geometry. The player release/launcher is unchanged. R444, which exposed shared-history overcount, remains a failed functional test and is not performance evidence.

Observed warm scene timings (2933 frames): mean 11.93825 ms, P95 16.95100 ms; Game Thread 9.48909 ms, Render Thread 11.92842 ms, RHI 3.51828 ms, GPU 2.90530 ms. These overlapping pipeline stages must not be added. 120 FPS remains unmet (8.33333 ms). Existing Cooked scene process-to-load was 11.01146 s; this is not arbitrary100-APNG cold-load acceptance. Background rustc consumed about 0.98 equivalent CPU core over launch-to-exit; other background processes also ran. Do not compare this with R357 to claim a speedup or accept final performance.

Receipt: `Saved/Crowd/GpuRetirementAcceptanceR454.json`; scene: `Saved/Crowd/DevelopmentGpuRetirementSceneR453.json`; frame series: `Saved/Crowd/DevelopmentGpuRetirementSceneR453.frames.csv`; background: `Saved/Crowd/DevelopmentGpuRetirementSceneR453.background-cpu.json`. Screenshot was inspected for scene rendering but is not CPU/GPU visual-parity acceptance; the third-person character head occupies much of the view. No shader/geometry/texture/animation precision change was introduced.

Remaining production work: bounded active/blending/preloaded clip selection, runtime low geometry/material construction from APNG, sequence-free logical playback connection to that scene, rendered CPU/GPU parity, and quiet controlled native Development profile/cold-load acceptance. Allocation safety alone does not reduce draw command submission cost.


## Bounded GPU clip window plan / implementation R455-R456

Add a worker plain-data selector for exact private character/clip IDs, at most three clips per character (active, blend and preload). Validate source semantics, selected identities and logical byte capacity before constructing selected keys/tracks; preserve caller output on rejection. Keep rig/reference identities, tasks and skin palette unchanged, rebase selected key/track offsets and rebuild owned clip tables. The first selected clip starts at time zero; callers must remap logical clocks by clip ID when adopting a window. Selection is not frame-time work and does not prepare missing clips or automatically release source CPU buffers.

Native diagnostic selects walking from the two privately prepared APNG clips, compares selected keys bitwise and six GPU phases with the original private CPU pose. It rejects stale character, missing/duplicate clip, more than three clips and insufficient capacity without mutating output. It then reverses the two-clip order on a worker and runs the existing GPU playback/blending/residency/retirement checks, so index reconstruction is exercised. R455 compile failure and repair are in CROWD_GPU_POSE_ERRORS.md. Native build/regression is pending; no player release replaced.

The current code limits a prepared bank's upload payload, not APNG pose preparation scheduling or a streaming gameplay controller. Those remain necessary before startup can skip unchosen authored styles entirely. Future20-style logic cannot be represented as20 actual tested resources; this test uses two real authored clips.


Runtime constraint reaffirmed: gameplay starts from player-selected APNG, plus shipped common resources only. Per-card mesh/pose/material preprocessing must execute in runtime background work; no character-specific cooked fixture is a production dependency. The 60-second limit must be measured on first selection of100 APNGs, including resource preparation and scene entry. Cooked fixture room load is not this gate; persisted derived data or already resident inputs cannot establish first-load acceptance.

Clip-window adoption preserves playback time/blend by private clip ID and rig reference identity, including shuffled actor ordering. Active blend inputs missing from the new window reject without mutating playback; an unused blend endpoint may map to the active slot. This is a plain parameter remap, not CPU animation evaluation. The100-person/20-ID fixture checks ownership/reindexing with synthetic constant poses; it does not establish20 authored styles or100 runtime APNG scene entry.


## Current-function acceptance R461 / R463: runtime private clip window

R461 native Development build passed (43.41s). Fresh executable R462 passed R463 on three actual APNGs with common shipped idle/walk curve bytes; no per-card rig recipe, cooked character asset or external KKS root was used by preparation. Selection runs on a worker. It keeps only requested private clips (maximum three per actor), preserves rig/skin binding metadata, copies selected keys bitwise and rebuilds indices/ownership. Walking-only windows passed six GPU phases against private CPU poses. Reversed two-clip windows passed the existing 15-sample playback/blend/private bank retirement audit. Matrix threshold remains0.001.

| APNG | Original two-clip key bytes | Walking-only key bytes | Worker selection ms | Maximum GPU matrix error |
|---|---:|---:|---:|---:|
| C011.png | 36,194,640 | 10,797,120 | 23.4212 | 0.000065118 |
| C002.png | 41,071,536 | 12,235,968 | 26.5536 | 0.000063672 |
| C000.png | 44,207,424 | 13,161,312 | 29.4500 | 0.000089378 |

Key payload reduction is about70.2 percent for these two particular clips when only walking is selected, not a universal loading-time or frame-rate improvement. Source CPU buffers remain alive through worker selection; the production scheduler still needs to skip unchosen clip preparation and release inactive buffers at the correct upload boundary.

The synthetic ownership oracle also passed100 actors with20 constant-pose IDs each, selecting different three-clip windows with the selection list reversed, then reading GPU blend results. This proves multi-actor indexing, not20 actual authored styles or100 runtime APNG scene rendering. Playback remap preserves time/blend, rejects missing active blend inputs without changing prior output and permits dropping unused endpoints. Stale character, missing/duplicate clip, more than three clips and undersized budget reject without mutating prior bank. R460 malformed test-array construction crash was repaired and retained as failed evidence.

Receipt: `Saved/Crowd/RuntimeClipWindowAcceptanceR464.json`; raw input/executable receipts: `Saved/Crowd/RuntimeClipWindowR463/summary.json`. Current function is verified. No new scene frame profile was taken; previous observed R453 mean11.93825ms/P9516.95100ms remains over budget and was affected by background work. The100-APNG first-load <=60s gate, runtime mesh/material scene construction and final rendered parity remain unverified. No player release/launcher was replaced.


## Runtime pose preparation scheduler plan R465

Implement a game-thread admission owner with non-blocking Tick, configurable simultaneous worker count (default2) and bounded queued/completed items (default8). Submit only explicitly requested active/blending/preload clips, reject invalid/oversized requests, defer admission when immutable pose buffers are pinned/reserved, and cache exact character/clip/recipe results. Running jobs retain capacity until their future completes, even after cancellation or a selection-generation change. Obsolete completions are discarded before publication. A new selection clears queued obsolete work and pending completion pins. Native sessions/inputs and GPU buffers remain separate budgets; job-count bounds do not constitute whole-process memory accounting.

Connect actual APNG idle/walk preparation to this scheduler rather than launching its futures directly. Deterministic controlled-future tests exercise2 concurrent workers,3 queued requests, queue rejection, pinned running reservations across generation change, delayed replacement admission, stale result discard, exact cache hit and queued cancellation. Actual native Development APNG/GPU regression follows compilation. This unit does not yet connect runtime mesh/material production or establish100-card first scene entry within60s; full scene profile and rendered parity remain outstanding.


## Current-function acceptance R466 / R468: runtime preparation scheduler

Native Development build R466 passed in51.18s. Immutable executable R467 passed R468 on three actual APNGs using only shipped common idle/walk curves for runtime preparation. Both private jobs were submitted together and reached2 concurrent workers. Existing GPU matrix, logical clock, private ownership, bounded clip-window, aggregate retirement and pose cache gates all remained passing. Controlled-future admission/cancellation/generation tests passed before APNG loading. Final pending reservations were0 for every card; CPU pose output budget was128MiB. No lossy pose, model, material or sample-rate changes were made.

| APNG | Snapshot ms | Idle worker ms | Walk worker ms | Peak charged CPU pose output bytes | GPU max matrix error |
|---|---:|---:|---:|---:|---:|
| C011.png | 742.23 | 1558.35 | 881.92 | 47,522,832 | 0.000065118 |
| C002.png | 581.30 | 1231.26 | 681.78 | 52,162,160 | 0.000063672 |
| C000.png | 915.23 | 1688.41 | 963.96 | 55,302,944 | 0.000089378 |

Worker durations overlap and cannot be added as wall-clock entry time. These are functional preparation samples, not a controlled serial/parallel speedup comparison or a100-card60s gate. Source/session memory is outside the CPU pose output budget. Keep one scheduler alive across selection changes and use BeginSelection; consumer handles pin active/blending/uploading buffers, and deferred requests must be retried after capacity becomes available. This scheduler is connected to the actual APNG diagnostic, not yet to production runtime geometry/material scene construction.

Receipt: `Saved/Crowd/RuntimeSchedulerAcceptanceR469.json`; raw native/executable/input evidence: `Saved/Crowd/RuntimeSchedulerR468/summary.json`. No new frame profile was taken for a startup-admission unit that does not alter the current cooked room frame path. Last observed room frame mean11.93825ms/P9516.95100ms (R453) still exceeds8.33ms and was affected by background programs. Runtime low mesh/material production, compact render rig, production clip adoption, actual rendered CPU/GPU parity and quiet100-card cold-entry/profile acceptance remain pending. Player release/launcher unchanged. No compile/runtime failure occurred in this unit; cancellation guardrails were added to the error record.


## Runtime render rig compaction plan R470

The automatically discovered GPU rig currently includes all native skeleton bones plus independent exact renderer palette columns. Implement a plain-data render-use closure: keep the scene root, all three affine columns for every visible renderer slot (including signed/fallback slots), and optional explicitly requested native socket bones with every ancestor. Preserve names/reference transforms and rebuild numeric parent indices/old-to-new mapping. Invalid/missing palette, socket, hierarchy or identity rejects without changing output. Never strip bones from the native snapshot or physics/session computation. This reduces sampled/uploaded GPU pose metadata and keys, not source mesh triangles or native simulation fidelity.

Native diagnostic will prepare compact idle/walk and a full private idle reference through the bounded scheduler, compare every retained sampled key bitwise and track parents through the remap, then run existing GPU readback/blend/admission/retirement checks on the compact rig. Synthetic tests cover signed palette columns, optional socket ancestor closure and rejection preserving output. Full reference preparation is diagnostic only; production would submit the compact request without this extra reference. Actual scene geometry/material production and final100-APNG <=60s/rendered parity/120FPS acceptance remain pending.


## Current-function acceptance R473 / R475: exact render rig compaction

Native Development build R473 passed (17.15s). Fresh immutable executable R474 passed R475 on three actual APNGs. Each card prepares compact idle/walk plus a full native idle reference through the bounded scheduler. Every retained idle sampled key is bitwise equal to the full private reference; track parent indices match the old-to-new map. Existing GPU pose/blend/clock/window/buffer admission and aggregate retirement gates pass on the compact rig with unchanged0.001 matrix threshold. Native snapshot/physics bone sets remain complete. This function changes render-pose output only; it does not remove geometry, simplify weights or lower60Hz sampling.

| APNG | Full pose bones | Compact pose bones | Bone/task reduction | Full idle keys bytes | Compact idle keys bytes | GPU max matrix error |
|---|---:|---:|---:|---:|---:|---:|
| C011.png | 2,315 | 1,600 | 30.89% | 25,397,520 | 23,102,400 | 0.000011057 |
| C002.png | 2,541 | 1,864 | 26.64% | 28,835,568 | 26,916,672 | 0.000012657 |
| C000.png | 2,694 | 2,017 | 25.13% | 31,046,112 | 29,127,216 | 0.000013952 |

Key savings are smaller than task savings because many removed native tracks were already constant-folded. They must not be advertised as equal loading-time/frame-time improvements. Preparation worker times overlap and include a diagnostic full reference; they do not establish100-card loading throughput. Optional native sockets are retained by explicit name with ancestors; callers must adopt rebuilt indices, and no runtime mesh/attachment consumer is considered validated until it does so. All exact renderer palette columns, including signed/fallback cases and private cloth/accessory results, are retained.

R472 failed cache publication revealed logical-length admission did not account for allocator quantization. The repair matches Unreal DefaultCalculateSlackReserve for keys/tracks and uses explicit Keys.Reserve before SetNum. R475 validates the repaired reservation path on compact nearly fully animated output. The failed receipt remains incomplete; errors/guardrails are in CROWD_GPU_POSE_ERRORS.md.

Receipt: `Saved/Crowd/RuntimeRenderRigAcceptanceR476.json`; raw executable/APNG/curve hashes and gates: `Saved/Crowd/RuntimeRenderRigR475/summary.json`. Current function is verified in the native runtime diagnostic. Runtime APNG geometry/material construction and remapping into its actual mesh consumer still need integration, followed by rendered CPU/GPU parity and100-card cold entry <=60s. No new room frame profile was taken because the existing cooked room does not consume this new runtime rig path; prior R453 observed mean11.93825ms/P9516.95100ms remains over8.33ms and had background interference. No120FPS or production scene acceptance claim; player release unchanged.


## Runtime APNG mesh binding plan R477

Add a combined background source capture that opens APNG once (empty KKS root), copies the neutral animation snapshot, discovers its private rig, evaluates the common initialization clip, and owns visible indexed geometry before closing the native session. Retain renderer IDs/names/roles/shader-kind/blend metadata, original triangles,4UV channels, vertex colors, normals/tangents and exact private palette bindings. Use up to12 balanced16-bit affine-column influences per vertex (same21845 triplet quantization as existing exact importer), including negative/fallback skin slots and rigid socket placement. Preflight geometry array allocation using Unreal allocator quantization under256MiB; no UObject/editor asset build or character-specific cooked data.

Map every influence to the compact render rig transactionally; fail if a used bone was removed. Native GPU diagnostic adds initialization-clip/time0 readback and deforms every source vertex through the resulting GPU world rows, comparing with native initial posed positions at0.01cm tolerance. This is owned source geometry/binding verification, not rendered mesh creation. Source triangles are intentionally retained during this unit; runtime decimation to body15k/clothes15k/hair+accessory30k, material/texture creation and actual mesh render integration remain required. No first-load60s or FPS acceptance claim.


## Current-function acceptance R481 / R483: runtime owned source mesh binding

Native Development build R481 passed in39.28s. Immutable executable R482 passed R483 on three actual APNGs. Combined worker capture opens each APNG once with empty KKS root and shipped common resources, owns its visible source geometry and neutral rig snapshot, and closes native sessions before consumer use. Pose preparation still uses separate private snapshot sessions; no per-card cooked mesh/recipe asset is loaded by this path. Every influence is remapped to the compact pose rig. Synthetic tests reject removed/outside bones, duplicate remap targets and invalid weight totals before mutating any vertex; valid shuffled numeric maps preserve weights.

The diagnostic reads GPU world pose rows at initialization idle/time0, then reconstructs every captured vertex on the CPU with its12-or-fewer16-bit affine-column influences. Comparison is against native posed source positions. This verifies coordinate conversion, exact palette/rigid binding and compact numeric indices; it does NOT execute a mesh vertex shader or render the character. Position tolerance remains0.01cm; existing sampled pose/blend/clock/window/retirement gates pass.

| APNG | Visible parts | Owned vertices | Source triangles | Rigid parts | Geometry array bytes | Capture ms | Max position error cm |
|---|---:|---:|---:|---:|---:|---:|---:|
| C011.png | 40 | 67,243 | 105,834 | 5 | 10,825,156 | 1087.00 | 0.002421443 |
| C002.png | 53 | 80,511 | 119,451 | 4 | 12,886,336 | 873.82 | 0.002260274 |
| C000.png | 30 | 45,514 | 66,073 | 1 | 7,256,212 | 1424.55 | 0.002309550 |

Triangles above are ORIGINAL visible APNG triangles, not newly generated low meshes and not the already cooked room's low mesh counts. No new triangle/texture-quality setting was applied. Geometry allocation preflight accounts for Unreal array quantization under256MiB; native-session/snapshot/input/string memory and pose/GPU arrays have separate ownership/budgets. Shader kind/blend/role/name metadata and UV/color streams are retained, but encoded textures and full common material construction remain to be connected. Source array capture must not be called a complete render-ready character. Tangent/normal rendered appearance and vertex parity beyond initialization time0 are still unverified.

Receipt: `Saved/Crowd/RuntimeMeshBindingAcceptanceR484.json`; raw executable/APNG/curve hashes and gates: `Saved/Crowd/RuntimeMeshBindingR483/summary.json`. R477 compile failure was corrected with explicit4D-to3D conversion and logged in CROWD_GPU_POSE_ERRORS.md. Next production work is runtime topology-safe reduction to high head/eyes, body15k/clothes15k/hair+accessories30k, transient mesh render resources, common shader/material and256 textures, then scene GPU playback adoption and animated/rendered parity. No new room profile: its current cooked fixture mesh path is unchanged. Last observed frame mean11.93825ms/P9516.95100ms remains over8.33ms and had background interference.100 selected APNG first-entry <=60s and native Development120FPS remain unverified. Player release unchanged.


## Runtime transient skeletal render resources plan R485

Background-pack visible source geometry into triangle-preserving sections using the engine-configured legal GPU bone limit. Keep one material slot per source renderer, never merge vertices by geometric proximity, and map skin influences through per-section local bone maps. Admit worst-case packed vertex/weight/index arrays including allocator quantization before constructing buffers. Preserve all triangles,4 full-precision UV channels, vertex colors and12-influence16-bit skin weights. Construct transient USkeletalMesh/USkeleton and direct FSkeletalMeshLODRenderData on the game thread; initialize RHI resources without editor/imported mesh build, SavePackage or per-card Cook assets. The caller owns UObject lifetime; diagnostic retains CPU buffers while production may release them.

Native oracle will verify initialized GPU position/skin buffers, section/global skin identity, original triangle count and exact positions/UVs. Synthetic coincident vertices from different source indices must remain separate; forced3-bone sections exercise partitioning/local bone maps, tiny capacity and oversized triangles must reject. This unit uses shared default surface material solely to validate buffers; source material/texture recreation, runtime low reduction, animation-provider registration and rendered scene parity remain pending.


## Current-function acceptance R489 / R491: transient skeletal RHI resources

Native Development build R489 passed (23.92s). Immutable executable R490 passed R491 on three actual APNGs. Background section packing uses the engine's configured legal bone limit, retains all original triangles and keeps vertices separate by source renderer/index. Synthetic3-bone sections preserve coincident but distinct source vertices and correct local/global skin maps; too-small array admission and a triangle exceeding the section bone limit reject. Only unused source vertices may disappear when building indexed sections; no geometric simplification or topology welding occurs.

The game-thread factory builds transient USkeletalMesh/USkeleton plus direct single-LOD render data and initializes RHI position and skin buffers without editor/imported mesh construction or per-card cooked assets. CPU-retained diagnostic buffers match positions and all4 full-precision UV channels exactly. Every global bone/16-bit influence weight remains unchanged with total65535; Unreal's descending variable-weight sort is accepted only as a permutation, not loss/truncation.12-influence truncation is explicitly rejected when shared cooked feature support is missing.

| APNG | Render sections | Indexed render vertices | Original triangles | Background packing ms | Max binding position error cm |
|---|---:|---:|---:|---:|---:|
| C011.png | 40 | 67,243 | 105,834 | 60.12 | 0.002421443 |
| C002.png | 53 | 80,511 | 119,451 | 63.01 | 0.002260274 |
| C000.png | 30 | 38,560 | 66,073 | 41.28 | 0.002309550 |

These are original APNG mesh counts, not the requested low mesh budgets. Packing time is one-time background preparation, excludes game-thread buffer construction/upload, and is not a frame time or100-card scene entry time. This resource diagnostic binds the shared default surface material; it does not render a production character or verify normal/tangent/material/texture appearance. GPU pose readback with CPU reconstructed source vertices still proves initialization-time binding, not actual rendered vertex-shader skinning. Native/full-compact key equality, pose blend/window/admission and retirement gates remain passing.

R488 first rejected semantically unchanged skin because the oracle compared influence positions instead of global-bone/weight sets; repaired oracle checks every nonzero stored influence without relaxing fidelity. Failed evidence remains incomplete and is recorded in CROWD_GPU_POSE_ERRORS.md. Tangent capture's extra handedness minus was corrected to match the established source builder; final rendered normal/tangent parity remains required.

Receipt: `Saved/Crowd/RuntimeRenderResourcesAcceptanceR492.json`; raw executable/APNG/curve hashes and gates: `Saved/Crowd/RuntimeRenderResourcesR491/summary.json`. Current resource factory is verified. Next integration is runtime topology-safe role-budget reduction, shared source shader/material and256 texture recreation, GPU-provider registration/logical clocks on actual transient mesh components, then animated/rendered CPU/GPU parity and100-card first entry <=60s/profile acceptance. The current cooked fixture room still does not use this new APNG mesh path; no new room frame profile or player release replacement. Prior observed R453 mean11.93825ms/P9516.95100ms remains above8.33ms and had background interference;120FPS is not accepted.

## R493 runtime role-budget reduction plan

Use UE Runtime GeometryProcessing QEM on owned APNG mesh data in a background worker, without editor reduction, Blender, or per-card cooked geometry. Protect head/eye/brow/lash geometry; allocate body15000, clothes15000 and hair/accessory30000 triangles. Use original-endpoint collapse only, retain all4UV/color/normal/tangent/12-weight attributes, do not spatially weld vertices. Lock source mesh boundary vertices and skin-support transitions. Validate disconnected vertex components after reduction; reject invalid/non-manifold input or component disappearance by retaining the original part and reporting a retention reason. Budgets are targets, never a license to lose geometry. Wire reduced data into the transient skeletal render factory and GPU initialization binding oracle. Test coincident disconnected legs, head retention, source endpoint identity, invalid topology retention and invalid index rejection, then three actual APNG cards in native Development. Final appearance/100-card entry/scene frame acceptance remain separate gates.

## R499 runtime reduction current-unit verification

Native Development build R493 succeeded59.27s; refill build R496 succeeded19.67s. Immutable executable R497 SHA40FD1FBBFB0B3D117DE09272704DF2ED12E29DBDE6611AAFAFAC31550FAE3874. Evidence RuntimeReductionRefillR498/summary.json complete with3 actual APNG cards and unchanged common idle/walk hashes; receipt RuntimeReductionAcceptanceR499.json. No editor or per-card cooked character mesh was used by the diagnostic.

Runtime GeometryProcessing QEM runs on owned plain data in the background. Head/eyes/brows/lashes retain original geometry; source-index identity is never spatially welded. Fixed source openings/skin-support boundaries and original-endpoint attributes protect disconnected legs, UVs, colors, normal/tangent and12 quantized skin influences. Non-manifold/duplicate/degenerate input or lost component retains its source part with a reason.3 role-group refill rounds use spare safe reduction capacity. Synthetic tests verify coincident disconnected private-bone legs remain separated, head indices/count unchanged, endpoint attributes unchanged, invalid topology retained and bad indices rejected. Actual reduced meshes initialize transient skeletal RHI buffers and still pass initialization-time GPU-pose binding audit (<0.003cm), full/compact rig equality, two private clips, clocks, window/admission and retirement gates.

| APNG | Source triangles | Low triangles | Head original | Body | Clothes | Hair/accessories | Budgets met | Worker reduction ms | Worker packing ms |
|---|---:|---:|---:|---:|---:|---:|---|---:|---:|
| C011 |105834|69058|9228|15000|14846|29984|yes|217.50|33.15|
| C002 |119451|84301|9228|15000|14832|45241|no|217.92|45.46|
| C000 |66073|50595|9228|18005|18726|4636|no|89.26|28.35|

These are triangle counts, not quad faces. Reduction and packing times are separate one-time worker measurements; they exclude resource upload, scene entry and recurring frames. Target body15000/clothes15000/hair-accessory30000 is not met for every card; boundary/invalid-topology protection wins over destructive thinning. This unit does not prove visual quality or fix all previously reported deformation bugs. Actual production source materials/256 textures, registered GPU animation components, runtime room entry and rendered CPU/GPU comparisons remain to integrate. No new frame profile: the existing cooked room does not consume this APNG pipeline yet, so repeating it would not measure this work.100 APNG <=60s and stable120FPS remain unverified; previous interfered R453 average11.93825ms/P9516.95100ms is not a new performance result. Player launcher remains on the prior release.

## R500 runtime material/texture source plan

Capture complete source material metadata through additive native JSON API, rather than a card-specific hardcoded material-property list. Own resolved mesh-view values and every standard/extra shader PNG before the native session closes. Deduplicate identical PNG bytes plus creation settings within a card, enforce encoded256MiB/prepared128MiB/single decode128MiB admission, decode and downsize to <=256x256 with complete small mips on the worker, then create transient texture resources on the game thread without image decode or shader compilation. Preserve sampler addressing/filter/sRGB and native encoded-mip semantics. Verify owned metadata/slot mapping and exact prepared/uploaded mip bytes on actual APNGs. Shared parameterized cooked shader/material catalog and production rendered parity are still subsequent integration; texture preparation alone is not material visual acceptance or <=60s100-card entry.


## R512 runtime material capture / low texture resource current-unit verification

Native Development R509 succeeded110.54s including build wait/startup, action time15.65s; immutable R510 executable SHADD5C4227A69C98F7AB5E77B7765CF056AD4E778DE5369EE6CBC1F26C281B4988. Final R511 summary complete with3 actual APNGs; receipt RuntimeTexturesAcceptanceR512.json. Earlier R504/R505 remain incomplete for the isolated initialization-state regression.

Additive native material_metadata JSON API captures complete source material (case-sensitive floats/colors/texture slots/keywords/render queue/source wraps) and extra shader PNG keys, without changing existing ABI layouts. Worker capture owns this metadata, resolved mesh-view values and all standard/extra PNG slots before native session close. PNG+creation-settings dedup uses SHA plus full compressed-byte comparison. Explicit runtime filter/per-axis wrap/sRGB/encoded-mip provenance and static source wraps survive. When static settings lack provenance, the current standard-slot defaults are retained; shader-specific default interpretation and rendered fidelity remain to verify.

Workers decode and generate <=256x256 complete mip chains. Source admission budgets: retained encoded arrays256MiB, prepared pixel arrays128MiB, single raw image128MiB; these are array budgets, not a global process/scratch/RHI cap. Each downsample may also temporarily hold its source and destination. Native encoded-mip textures use encoded channel averages with floor alpha127 at half coverage. Ordinary sRGB colors average in linear space and re-encode; alpha/data are averaged independently. Odd dimensions and1xN retain texel coverage. No runtime shader compilation. Game-thread factory creates transient PF_B8G8R8A8 textures with all mips, source samplers and RHI resources. These small transient textures retain all mips (NeverStream=true); do not describe them as cooked streaming. Cross-card CPU/GPU texture sharing is not implemented by the within-card dedup.

Controlled tests cover encoded RGB/alpha127, linear-space sRGB187/188, constant odd3x5 and1xN, bad source rejection preserving output, invalid pixel/mip data and duplicate terminal mip rejection. Actual card tests verify every captured extra shader slot is represented, part counts match mesh capture, RHI resources/mip counts exist, platform bulk mip bytes exactly match prepared data, source sampler and SRGB settings remain. This is platform-buffer fidelity, not a GPU pixel readback or actual material-rendered image acceptance.

| APNG | Material parts | Texture slots | Unique textures | Mips | Prepared MiB | Worker texture/material ms | Total APNG source capture ms |
|---|---:|---:|---:|---:|---:|---:|---:|
|C011.png|40|153|69|595|17.98|1399.43|2569.72|
|C002.png|53|223|87|759|25.09|2738.72|3613.31|
|C000.png|30|135|60|512|13.69|837.20|1800.45|

The combined capture time includes texture/material time, so do not add them. These one-card background preparation measurements are not recurring frame times and cannot establish100-card first entry <=60s.

C000 source initialization binding regression was caused by using the live clip-install API after it gained continuous private-particle behavior. New offline prepare_shared_clip explicitly starts fresh accessory particles while live install remains continuous. KKRuntimePose source discovery/capture and bank sampling now use the same preparation contract. Repaired C000 GPU/source error0.00230955cm; all3 remain below0.003cm, with existing low reduction, transient skeletal resources, full/compact rig equality, two private clips, clock/window/admission and retirement tests passing. Preserve first sample failure in the diagnostic instead of overwriting it with a secondary paused-clock check.

Shared parameterized Cook shader parents/material instances, source-specific eye/hair/shadow appearance, actual transient component GPU-provider registration and production APNG room integration remain. The resource oracle still uses the default surface material for skeletal geometry; it does not render these source textures on a production character. No new room frame profile or player launcher replacement.100 APNG <=60s / Development120FPS unverified; prior interfered R45311.93825ms mean /16.95100ms P95 is not a performance result for this new pipeline.

## R513 common parameterized material plan

Port the existing Forge MainOpaque forward arithmetic into one common material parent: replace baked per-character float/int/bool uniforms with dynamic scalar/vector parameters (separate alpha scalar for4D values), and texture-object constants with parameter slots. Preserve bit casts, default textures, coordinate conversion and automatic camera/time uniforms. Build only common parent/default resources in an isolated Editor tool host, then Cook those common assets. Runtime reads exact source metadata and binds private values/textures through MID without shader compilation; strict family/schema/parameter validation rejects unsupported paths. Begin with family2, explicitly report other families unsupported. Verify native Development with actual APNG parts and a shared parent, before claiming complete card/material/room integration or visual/FPS acceptance. Tool host dependency is conditional so legacy audit hosts do not acquire KKRuntimePose dependency unintentionally.

### R516–R523：APNG runtime 共用 ForgeOpaque 材質第一階段
- 共用 M_ForgeOpaque 保留既有 Forward shader 算式，來源材質常數改成 scalar/vector 參數；四維值以 RGB + 獨立 alpha 保存。貼圖改成 TextureObjectParameter。Cook 僅含共同父材質與三張共同預設貼圖，無卡片輸入或每卡專用資產。
- runtime 只建立 transient MID 並绑定已在 worker 解碼的貼圖，不呼叫 Editor 材質建置或 shader compile。
- R518 Editor 與 R520 原生 Development 建置成功；R519 common patch 10,259,001 bytes；R521 immutable executable。
- R523 三 APNG D3D12/SM6 執行成功。C011: 0 eligible / 40 unsupported；C002: 2 / 51；C000: 7 / 23。每個 eligible MID 共同父材質、scalar/vector/texture pointer 檢查成功，錯誤父材質拒絕。合計 9 個真實部件；沒有把 unsupported 部件換成錯誤材質。
- 來源參數 oracle 檢查 UV ST、顏色 alpha、數值，以及 unsupported / invalid slot 不修改輸出。R524 接續加入缺少 schema/material 與 malformed color 拒絕。
- APNG snapshot (含網格/材質擷取) C011 2722.83 ms、C002 3835.16 ms、C000 2110.84 ms。這些是单卡診斷數據，非 100 人 ≤60秒證明，且不能與其中包含的 texture preparation 重複相加。
- 原有 pose GPU readback、初始化網格 binding、reduction、texture RHI、scheduler/clip window/retirement gate 仍成功。未渲染 MID 做像素對照；未完成其他 shader 家族/outline/shadow 或正式 APNG room 的整體整合；未做每幀效能 profile，120FPS 未驗收。
- 最終 R524 Development build / R525 snapshot / R526 三 APNG regression 全部通過；缺少 schema/material、畸形 color 等輸入拒絕 oracle 通過。C011 snapshot 2039.15ms、C002 3248.31ms、C000 1814.83ms。R527 unit receipt 明確保留 visual / scene / 120FPS / 100卡60秒未驗收旗標。

### R528：共用材質家族擴充計畫與實作
1. 將 binding / MID factory 改為 recipe catalog，保留 Opaque wrapper，後續家族不再複製 binding 與資源驗證邏輯。
2. Forge MainAlpha 匯入原有 Forward HLSL 與 layout；產生共用 AlphaMasked / AlphaTranslucent 父材質，根據來源 _OKNativeAlphaOpaque 在 runtime 選擇。
3. 共用父材質加入 family identity，避免不同 shader 或 blend 交叉套用。
4. 每個 APNG 真實部件按 family 分別計數，另外驗證兩個 Alpha render-state 的 synthetic MID 建立與錯配拒絕。
5. 原生 Development 回歸後保存 unit receipt；畫面 / 陰影 / 正式 room / 120FPS / 100卡60秒仍留到完整整合驗收。
- R528 common authoring / R529 Editor build / R530 Cook / R531 native Development build / R533 immutable snapshot / R534 3 APNG regression 通過。
- 共用資源含 3 父材質 + 3 預設貼圖，patch 10,475,806 bytes；無卡片生成輸入。Family ID 使用於 HLSL 及 runtime factory；Opaque 舊 wrapper 保留。
- 真實 MID count：C011 Opaque0 / AlphaMasked0 / AlphaTranslucent0 / unsupported40；C002 2 / 0 / 1 / 50；C000 7 / 1 / 5 / 17。合計 Opaque9、AlphaMasked1、AlphaTranslucent6、unsupported107。兩種 Alpha 模式均有真實 APNG 覆蓋；每卡 synthetic default-state / wrong-state rejection 也通過。
- R534 snapshot C011 2032.46ms、C002 3535.47ms、C000 1845.24ms；包含 mesh/material 擷取，非百人60秒載入驗收。既有 GPU pose、reduction、mesh binding、texture RHI、scheduler、clip window / residency retirement 回歸全通過。
- R535 acceptance 是參數與資源綁定單元驗證，非像素、透明排序、陰影或角色畫面等價驗證。仍須完成其餘材質家族並接入正式 APNG room，才能進行 CPU/GPU 同畫面 profile 和 120FPS 驗收。

### R536：Forge MainItem 共用材質路徑
- 計畫：保留 MainItem 的原生 Forward 程式、色罩與 _Color / _Color2 / _Color3 等參數，使用共同父材質、runtime MID，避免每卡 Cook 資源。
- 新增 ForgeItem recipe (來源 ShaderKind8)，與既有 Opaque / Alpha 共用 binding / factory；common Cook generator 和 runtime loader 都列舉同一份 recipe catalog 與 parent-name API。
- 不增加額外 UE alpha clipping：原生 MainItem Forward One/Zero 在程式內 discard，OpacityMask 維持 1，避免第二次 .5 clip 改變外观。
- 參數 oracle 檢查色罩顏色與獨立 alpha、各 family identity 唯一、catalog 連續與 layout/program 非空，錯用 Opaque 父材質拒絕。
- Native JSON 新增 shader family distribution 及 MainItem count，後續以實際來源分布選擇擴充順序。
- R536 authoring / R537 Cook / R538 native Development / R539 snapshot / R541 三 APNG D3D12-SM6 回歸全部通過。MainItem 實際 MID C011=4、C002=4、C000=4；支援總數 C011=4、C002=7、C000=17，合計 28；unsupported95。
- 原有 Opaque9、AlphaMasked1、AlphaTranslucent6 保持通過；MainItem12 新增通過。色罩 recolor / alpha oracle 與錯配父材質拒絕均成功。
- 三卡 shader 分布：kind1=34 (頭髮)、kind0=25、kind3=13、kind4=12、kind8=12、kind2=9、kind11=7、kind5=6、kind7=3、kind6=2。下一步優先研究頭髮共用 shader；不要假設所有 kind0 是同種材質，仍需按 blend / unlit / eye / source shader 進一步分類。
- R541 snapshot C0112297.27ms、C0024121.32ms、C0001967.16ms。這是單卡 preparation；未重測正式百人場景每幀平均/P95，未驗收120FPS或百卡60秒。
- R542 receipt 明確保留 rendered_visual_equivalence / scene_integration / 120FPS / 百卡60秒 false。沒有修改玩家既有正式啟動入口。

### R543–R546：Forge 頭髮共用 shader runtime 路徑
1. 核對 installed EC shader bundle 與 Saved DXBC 的逐位元一致，再以 precise_lines / translate 生成 checked-in common Forward recipes；生成工具不使用 APNG 卡片。
2. 新增 ForgeHair / ForgeHairFront families，沿用共同 binding/factory；僅接收來源確定的 Forge hair、masked/unlit。前後 shader 屬性預設值獨立，_SpeclarHeight 分別 .85 / .985。
3. Shader 接入 native 第二組 UV；runtime mesh 保持原始 UV順序，使用 channel1。保留原始雙光澤採樣、face sign、mask discard / 顏色 / ramp / normal 運算。
4. 不使用 Koikano hair 或未知 shader 的猜測替代；兩種 common parents、defaults / wrong-family rejection oracle，加上 APNG真實 family counts。
5. 審查修正 missing UV channel 的 Unity(0,0) fallback 存儲值。最後仍須渲染比對、正式room及性能驗收；current shader recipes 未整合 shadow variants 或 outline。
- R543 Editor build / common authoring、R544 Cook、R545 native build 成功。R546 納入 missing-UV fallback 修正後再建置，R547 immutable snapshot / R549 三張 APNG D3D12-SM6 regression 全部通過。
- 實際來源部件：C011 Hair11 / Front5 / supported20 / unsupported20；C002 Hair6 / Front11 / supported24 / unsupported29；C000 Hair1 / Front0 / supported18 / unsupported12。
- 合計 Hair18 / Front16 新增34，全部支援62（Opaque9 + AlphaMasked1 + AlphaTranslucent6 + Item12 + Hair18 + HairFront16）；剩餘61部件 unsupported。兩種 hair 模式均有實際 APNG 覆蓋；預設值 / wrong-family / unsupported-blend oracle 通過。
- R549 APNG snapshot C0111940.27ms、C0023189.68ms、C0001770.71ms，包含 mesh/material preparation；原有 GPU pose / binding / reduction / texture RHI / clip window / residency retirement gate 全部通過。
- 原始 hair DXBC SHA256 b756d52dc1a6b8745cf61f62ad39ac132660d2d6fb44b8fda6a7b787b4c8310c；front 0904e100797f99dcacbd7390f762c83fd661289e15e56547f9962c8e941a08b2。Source generation 只依共用 shader bundle，runtime 使用 checked-in program 與共同 Cook parents，不依 KKS installation / Saved DXBC / per-card material packages。
- R550 receipt 是參數/資源與 shader compilation 單元驗證。尚未做 pixel readback / CPU GPU畫面對照、hair shadow variants / outline、前髮 native SrcAlpha + ZWrite 行為完整還原；維持目前工程 masked hair 設定，不能說已完整還原原作透明混合。
- 正式 APNG room 整體材質及100人性能未完成，不提供新的平均 frame time；120FPS /100卡60秒未驗收。

### R551–R556：來源分類與 Studio Alpha common recipe
1. R551/R552/R553 native runtime 來源分類：kind4 的12部件皆為 Forge main_item_studio_alpha / translucent / unlit；kind0 的25部件是眼白、眼睛、鼻線與眉毛，不可視為一個generic shader；kind3含opaque/item/alpha。
2. 根據核對 installed KKS shader bundle 的 DXBC raw8/native8 生成 StudioAlpha Forward（SHA256 18cd42cce3f9e177fabae0928f0154b121e1a5ce70923bfc1f57b0e6404da89b）。包含 PatternMask1/2/3、色罩、多色、旋轉、clamp、alpha、emission等參數，無卡片輸入。
3. 共用 recipe 新增 blend / two-sided policy，供 Bake / runtime factory 同用。StudioAlpha 為 SrcAlpha blend / CullBack；不可套用既有雙面 Alpha parent。
4. 核對原始 zWrite=1。當前 common UE Translucent parent 尚未實作原生透明深度寫入橋接；unit 結果只含編譯與綁定，不能算畫面、透明排序或source pass等價。正式 renderer 整合必須處理這項，不能偷偷換成masked或降畫質。
5. 參數 oracle 檢查 _alpha=.375、_patternrotator1=.5、_Patternuv1=(.1,.2,2,3)含獨立alpha；拒絕 wrong translucent family / unsupported blend。
- R554 Editor/common shader preparation、R555 Cook、R556 native Development、R557 immutable snapshot、R559 三 APNG D3D12/SM6 回歸通過。
- 真實 StudioAlpha MID C011=0、C002=12、C000=0。Supported totals20/36/18，合計74；remaining unsupported20/17/12，合計49。既有 Opaque / Alpha / Item / Hair / HairFront 原數量与全部回歸仍通過。
- Pattern/alpha oracle、CullBack/Translucent policy、wrong translucent parent family拒絕、unsupported blend拒絕通過。Shared-material factory 只驗證shader參數與資源，不等於像素畫面驗證。
- R559 APNG snapshot C0112318.42ms、C0023780.23ms、C0001807.47ms。單卡擷取時間不得當成百人60秒證明；尚未測新的正式場景 frame mean/P95，也不能宣稱已達120FPS。
- R560 receipt 明確 source_depth_write=true / source_depth_write_equivalence_verified=false；以及 visual / scene / 120FPS /100卡60秒 false。後續正式render integration不能忽略這項原生深度寫入差異。

## R561–R562：Forge 眼白共用 runtime 材質

計畫：依實際 source shader 分類，補 Shader Forge/toon_eyew_lod0 的共同 shader，保留 APNG 私有材質值與貼圖，在 Native Development 驗證 factory、資源與 pose；不把 MID 綁定當成畫面驗收。Koikano/main_eyew 另有不同 program 與 3D texture，不能直接共用此配方。

原作方向光 fragment：Unity blob 8 / raw DXBC scan 2；CB[8,1]，兩張 2D texture，單面、SrcAlpha/OneMinusSrcAlpha、ZWrite Off。保留 `_Color` alpha 獨立參數。原作 stencil Replace/ref2 尚未映射，畫面遮蔽與完整 shader shadow keywords 仍待驗證。

### R562–R568 本單元結果

- 共用 Editor authoring 編譯與 shader compilation 通過 R562；11 個共用 assets 和 RuntimeMaterialBake shader library Cook/Pak 通過 R563，無逐卡內容。
- Native Development build R564 成功（19.81秒），immutable snapshot R565；既有兼容 shader patch 加唯一 R563 library 組成 R566，原有 player launcher 未變更。
- 三張 APNG / idle+walk / D3D12 SM6 R567 通過全部既有 GPU buffer、texture/mip、runtime reduction、pose/skin binding、cache、clock/residency gate，以及新眼白獨立 alpha、單面透明與錯誤 family 拒收 oracle。
- C011：共用20、未涵蓋20、眼白0，snapshot1982.780ms；C002：共用40、未涵蓋13、眼白4，snapshot3418.947ms；C000：共用24、未涵蓋6、眼白6，snapshot1757.208ms。合計84/123，未涵蓋39。此時間包含診斷 capture，不是百人場景載入或 FPS。
- R568 receipt 明確保留 stencil 等價、rendered visual equivalence、正式 APNG room integration、100卡60秒、120FPS 未通過；本單元只確認共同 shader recipe/cook/runtime MID 綁定成功。
- 後續：Koikano 眼白含不同 program/3D texture，虹膜、服装、skin/IBL 仍需各自原始來源配方；Studio Alpha ZWrite bridge、眼白 stencil 與實際 pixel parity 待實作/驗證。

## R569–R571：Forge 虹膜 runtime 配方與 UV 契約

計畫：補 `Shader Forge/toon_eye_lod0` 共用 program；修正所有 EyeDraw 部件的 runtime mesh 採樣 UV 捕捉；以 Native Development APNG 測試證明資源/factory/pose 功能。完整畫面/百人載入與 FPS 最後驗收。

來源確認：EC bundle Unity5.6.2，透過 ShaderSubProgram table 精確解出 fragment blob8/DIRECTIONAL，再逐 byte 比對 raw scan0（SHA256 7314be5e7a4b86a6b3588a05969c52173a603b1aac830b08a91eb71d4eab6172）。CB[14,5,1]，四個2D貼圖；CullBack、SrcAlpha/OneMinusSrcAlpha、ZWriteOff、stencil Replace/ref2。眼白 generator 同步加入 native blob table 比對，產生相同既有源碼。

虹膜 vertex output 確認：v1.xy=主UV、v1.zw=高光UV1、v2.xy=高光UV2、v3=世界位置、v4=法線、v5=切線、v6=副切線。不能沿用一般 shader v3法線/v4切線/v6 front-face flag。

通用修正：runtime mesh 所有 EyeDraw 的 UV0 使用已解析 eye_sample_uvs，UV1/2保留原生高光採樣流；拒絕缺失/非有限 UV。Forge 虹膜 shader 對已解析 UV 的 `_rotation` 置0、overlay ST置identity，主貼圖ST仍套一次。顏色/高光開關/高光貼圖採用每張 APNG 的解析值，缺失高光使用原始black default，獨立alpha保持；不依卡名修補。

### R571–R577 本單元結果

- 共用 shader authoring/compile R571成功；12個共同assets與唯一RuntimeMaterialBake library Cook/Pak R572成功，沒有逐卡Cook內容。
- Native Development build R573成功（22.90秒），immutable snapshot R574；R575組合相容patch與唯一R572shader library，原player launcher未變更。
- R576三張APNG/idle+walk/D3D12 SM6全gates通過。新增iris sampling契約、resolved高光texture覆蓋、獨立alpha、rotation/overlay ST不重複、錯誤parent拒收與缺失契約transactional拒收oracle。
- C011共用20/未涵蓋20/虹膜0，snapshot2962.957ms；C002共用43/未涵蓋10/虹膜2，snapshot4106.533ms；C000共用25/未涵蓋5/虹膜2，snapshot2444.771ms。合計88/123，剩35。GPU初始骨架綁定position error最大0.002422cm，原有pose/cache/clock/residency/mesh/texture資源gate全通過。
- R577只確認本功能單元。snapshot為診斷準備時間；未測正式百人平均/P95 frame、APNG100卡60秒、rendered CPU/GPU畫面等價與stencil，不能宣稱120FPS或Alice正式場景五官已修復。
- 更正R568逐卡文字：依R567原始summary，C002為共用41/未涵蓋12/眼白5；C000共用23/未涵蓋7/眼白5。總計84和未涵蓋39正確。舊逐卡文字誤寫，以上以原始JSON為準。

## R578–R580 Koikano眼白與通用texture/control-flow支援

計畫：補Koikano/main_eyew原始方向光fragment配方，支援3D資源與條件分支、正確shader inputs和runtime場景uniform/matrix參數，Cook共同父材質後Native Development APNG驗證。來源Unity2019.4 blob8/DIRECTIONAL、rawscan8；SHA256 6a7758a9eddede66c20689fc1a3dc93e3aa5c1582ea2c7d2aa0a50f7b44a8f33，透過原始12-byte blob table逐byte確認。

3D貼圖為Unity場景共同光照探針 unity_ProbeVolumeSH，不是APNG角色私人內容。translator依每個resource declaration選2D/3D/Cube維度，3D/Cube不反轉V；新增帶bit-mask判斷的if/else/endif與未配對拒收。既有四個Forge generator重產Program全部byte-identical（ShaderCodegenRegressionR579.json）。

共用Cook新增1x1x1 UVolumeTexture；runtime material factory驗證parent texture dimension。Probe matrix四個row與各自alpha均為共同shader的runtime參數，不套用view matrix。Scene.Uniforms提供場景builtin覆蓋，不含逐卡shader compilation。Koikano眼白輸入按native欄位放normal.z與worldposition.w。目前未接正式scene SH二次項/volume內容，stencil與rendered parity仍未驗收；預設未啟用probe volume。

使用者再次確認的正式限制：只從APNG進場，共用資源可出貨，禁止逐卡Cook/特化預計算依賴；角色衍生資料需runtime/背景產生，百人冷載入不得超過60秒。現有三卡診斷不能代替正式100卡驗收。

### R581–R587 本單元結果

- 修正R580 C4456後，R581共同工具build成功（36.71秒），共同shader compile完成。14個共同assets（新增Koikano眼白父材質與共同1x1x1 Volume Texture）R582 Cook/Pak成功，沒有per-card input、沒有runtime shader compilation。
- Native Development build R583成功（21.94秒），immutable snapshot R584；R585唯一新版RuntimeMaterialBake shader library搭配既有相容patch。Player launcher未更換。
- R586：三張APNG/idle+walk/D3D12 SM6所有gates通過。Koikano共享volume UVolumeTexture+RHI Texture3D有效，alpha/cutoff、KanoVerShift、SH override、4-row matrix與各row alpha綁定、跨family拒收、非有限scene矩陣transactional拒收通過。
- C011：共用25/未涵蓋15/Koikano眼白5，snapshot1981.158ms；C002：43/10/0，snapshot3245.948ms；C000：25/5/0，snapshot1850.304ms。共93/123，未涵蓋30；GPU初始骨架position error最大0.002422cm。數字依原始summary/result解析。
- Python translator 5項回歸全部通過（2D V flip、3D z保留、非t0 Cube、巢狀integer mask條件、未知/未配對拒收）；既有Forge Program保持byte-identical。
- R587 receipt只確認本功能單元；source stencil、正式scene SH L2/volume lighting、rendered CPU/GPU畫面、100卡冷載入<=60秒和120FPS平均/P95均仍未驗收，沒有新的FPS數字。診斷準備耗時不能換算為百人載入。
- 正式路徑硬限制維持：玩家APNG + shipped common assets；私人網格/貼圖/pose/cache只可runtime/background推導，不可藉逐卡Cook或事先特化的動畫庫通過載入驗收。

## R588–R596 Koikano 虹膜 runtime 單元

計畫與實作：依原作 Koikano/main_eye 的 Unity2019.4 blob8/DIRECTIONAL fragment 產生共用配方，核對 vertex register 與原生解析後的眼睛 UV，接上 runtime 材質參數，再 Cook 共用父材質並跑原生 Development 三卡回歸。來源 DXBC SHA256 76475799721ba954cb4b3074cfd737c807fad41939d8befb422f4ee7350145d0，native blob 與 raw8 逐 byte 相等；CB[18,5,47,8,7]。

- 高光兩個 UV register 依原作交換映射；runtime UV3 保存主眼 UV 的逆旋轉結果供 expression 使用。已解析的主旋轉/前兩層高光 ST 不重算，第三層高光的原始旋轉與顏色保留。兩個原生 cubemap 共用 sampler0，依實際指令恢复，不採 metadata 的 -1 當 sampler。
- Scene HDR 預設先設定再接受場景覆寫；synthetic oracle 驗證 alpha、第三高光、rotation、HDR、缺少 expression 契約/錯誤 family 拒收、非有限 UV 不修改輸出。共用 cubemap 的 UTextureCube 與 TextureCube RHI 維度通過。
- R590 Editor/common shader build 成功；R591 Cook/Pak 含16個共同 assets，無逐卡輸入或 runtime shader compilation。R592 Native Development 成功（22.14秒），R593 immutable snapshot，R594 僅一份 RuntimeMaterialBake shader library 加既有相容 pak。
- R595 APNG/idle+walk/D3D12 SM6：三卡 complete=true、各 exit_code=0、全部 gates_passed=true。C011 共用27/未涵蓋13/Koikano虹膜2，snapshot1932.851ms；C002 43/10/0，3565.557ms；C000 25/5/0，2088.131ms。共用95/123，未涵蓋28；初始蒙皮 position error 最大0.002422cm。Python translator 5項回歸通過。
- R596 receipt 僅確認本功能單元。正式場景接線、rendered CPU/GPU畫面比較、stencil、場景 SH L2/反射照明與 Forge expression UV 仍待完成。尚無新的正式每幀平均/P95數字，也未通過百人 APNG 冷載入60秒或120FPS驗收。
- 出貨仍以 APNG + 共用資源為唯一角色輸入；本輪 UV 衍生在 runtime 背景 mesh capture 處理，不新增逐卡 Cook 資源。Player launcher 尚未切換至未完成的 APNG 場景路徑。

## R597–R605 Forge 鼻線 runtime 單元

計畫：先核對鼻線/眉毛來源與 pass，再新增可正確接入的通用配方、共用資源 Cook 與 Native Development 三卡回歸。本輪完成 Shader Forge/toon_nose_lod0；Koikano 鼻線 ZWriteOn/ShadowCaster 需深度橋接，眉毛來源未在已檢查 EC/KKS bundle 找到，兩者不套用 Forge 鼻線替代。

- Native fragment blob8/DIRECTIONAL 與 raw0 byte equality；CB[4]，SHA256 875ff86184832554b4664cdca4196b992f56af88c2615284f90bcc55544b667c。vertex blob0 SHA256 3d618cd3050096736e64b83519ca3891cf5eca2ef7b2a25e681d06709a798af5；generator 核對唯一 o1 UV 寫入為 mov o1.xy,v1.xyxx。PS 直接執行原作取樣、(1-R)*Color+G、A*Color.a，沒有眼睛專用 UV 變換。
- 新增 ForgeNose family12，共用 M_ForgeNose；原始顏色、alpha、ST 來自 APNG/native material metadata。沒有逐卡 Cook、沒有 runtime shader compile。精確 source/blend/unlit/EyeDraw 選配方；拒收 Koikano、iris 與 masked。共同黑色預設沿用原作，不新增特化貼圖。
- R598 重複模板定義編譯失敗後修正；R599 common authoring build 35.10秒成功，R600 Cook/Pak17共同 assets，R601 Native Development build20.18秒成功，R602 immutable snapshot，R603 唯一新版 RuntimeMaterialBake shader library。
- R604 三卡 APNG/idle+walk/D3D12 SM6：complete=true，三卡 exit_code0/gates_passed。顏色RGB、独立alpha、非identity ST、錯誤家族/深度設定拒收 oracle 通過；既有 GPU pose/readback、private clips、蒙皮、mesh reduction、mip/RHI gates維持通過。translator5項回歸通過。
- C011 共用27/未涵蓋13/Forge鼻線0，snapshot1947.807ms；C002 44/9/1，3179.817ms；C000 26/4/1，1785.070ms。總計97/123，未涵蓋26；初始蒙皮 position error 最大0.002422cm。R605 receipt 記錄精確逐卡值。
- 這是功能單元驗證，正式 APNG 場景接線與 CPU/GPU rendered parity 尚未完成；無新的正式平均/P95每幀時間，尚未通過120FPS或百人冷載入60秒。診斷 snapshot 耗時不能當作百人載入/FPS成績。眉毛、Koikano鼻線、衣服/皮膚/IBL配方及正式場景整合仍待完成。

## R606–R614 原始眼睛 expression UV 通用修正

原計畫查眉毛 shader 並實作配方：找到 Koikano/main_eyeb 位於原作 bo_head_00.unity3d，原始 pass ZWriteOn 且 stencil Replace，需要正式 renderer 深度/stencil 橋接，因此未套用不等價的普通透明父材質。来源、bundle SHA 與 pass 設定保存 RuntimeEyebSourceR606.json；眉毛尚未新增 runtime recipe。

本輪改修已確認的表情 UV 問題：Forge 原生 vertex 直接傳 raw iris UV，主貼圖的 rotation/MainTex EyeLook ST 不該進入 expression；Koikano native vertex 對 expression UV 只套 _texcoord_ST。之前對 sample UV 逆旋轉仍留下 EyeLook ST，因此不足。新增 ok_part_eye_expression_uvs 從 APNG 保存的 iris.raw_uvs 產生原始 vertex varying；不改 OkMeshView ABI，提供 count/capacity/finite 驗證，錯誤不改 caller buffer。runtime worker 捕捉到第四 UV 流，材質要求 expression 契約。

Forge source DXBC 的唯一 expression mad 改由 v7 接收原始 UV，保留原始 -0.06 view parallax 與 expression_ST，其餘指令不變；Koikano 改由同一 native export 提供正確原始 varying。主要瞳孔/高光仍用原有 prepared UV，沒有減少動畫頻率或修改模型品質。本輪只在背景建置 UV，沒有逐幀 CPU UV 處理、逐卡 Cook 或 runtime shader compilation。

- R607 初版逆旋轉方案未用於驗收；R608 正確實作共用 authoring/shader build 成功（59.84秒，含 native staticlib rebuild），R609 Cook/Pak17共同 assets，R610 Native Development build40.85秒，R611 immutable snapshot，R612 僅一份新版 shader library。
- Rust expression_varying_ignores_pupil_and_eyelook_transforms 通過：Forge 原始 varying 不受非identity EyeLook ST/rotation影響；Koikano 只取 vertex texcoord ST，非有限/未知 family拒收。Python translator5項回歸通過。
- R613 APNG/idle+walk/D3D12 SM6 三卡 complete=true，全部 gates_passed、exit_code0；涵蓋 Forge4個和Koikano2個真實虹膜。缺失 expression 契約保持 transactional 拒收，既有 pose/readback、private clips、mesh/reduction、material/mip/RHI gates 全部通過。材質涵蓋仍97/123，未涵蓋26。
- 正式 APNG 場景接線、rendered CPU/GPU畫面、source stencil/深度仍未完整驗收。R614 receipt 明確保留 scene/visual/120FPS/100卡60秒為false；沒有新的正式每幀平均/P95時間，三卡snapshot僅診斷準備時間。

## R615–R623 通用 source depth/stencil/blend 狀態與 GPU 光柵驗證

計畫：在接眉毛之前建立原作 pass 的通用狀態轉換及實際 GPU 光柵回讀測試，再接入角色 mesh pass。這一輪完成前者；角色網格、眉毛配方、scene depth 複製/合成尚未整合，不能以本單元通過宣稱眉毛已完整渲染。

- 新增 OKSourceRasterState：Unity compare/stencil op/mask/ref/blend factors 轉 RHI；只反轉深度比較至 reversed-Z，stencil 保留原數值意義。透明 blending 依原作 RGB/alpha factors；非法枚舉/mask/ref與 RHI不支持的 SrcAlphaSaturate 拒收，不修改上次輸出。當前介面支持雙面相同 stencil、Add blend、RGBA寫入；不同 front/back stencil、其他blend op尚未接入。
- OKVerifySourceRasterGpu 用 Engine OneColor VS/PS，RDG4x1 color + 私有 depth/stencil target 真正 raster draws，非 CPU 模擬。近方半透明紅後畫較遠不透明藍，深度寫入On應保留紅，Off應藍；stencil Replace/ref2後前方綠以Equal/ref2通過、ref3拒收。RGBA255預期依序(128,0,0,64)、(0,0,255,255)、(0,255,0,255)、(128,0,0,64)，各channel容許1。非 sRGB target，無Gamma轉換。
- 私有 target 不修改 UE scene stencil 保留位元；此架構尚需正式角色 raster pass 整合。GPU oracle僅診斷時執行，沒有新增遊戲每幀readback或逐卡資源。使用既有引擎shader，沒有新的global shader或 runtime compilation。
- R616 constructor錯誤後修正，R617 Editor共同工具成功41.50秒，R618共同Cook/Pak17 assets，R619 Native Development成功20.39秒，R620 immutable snapshot，R621唯一新版共用library。
- R622 三張APNG/idle+walk/D3D12 SM6：complete=true、各exit_code0/gates_passed、runtime_source_raster_state_verified=true。既有UV/material/mips、mesh/reduction、GPU pose/readback/private clips gates維持通過。材質涵蓋仍97/123，未涵蓋26。
- C011 snapshot1927.694ms、C0023169.989ms、C0001793.685ms；皆診斷準備時間，不是正式百人載入/FPS。R623 receipt 保留 eyebrow recipe/character mesh pass/scene integration/character rendered parity/100卡60秒/120FPS未驗收；沒有新的正式每幀平均或P95時間。

## R624 source-material GPU mesh pass plan and status

Plan: (1) add common material VS/PS on existing vertex factories, preserving skinned vertex/UV inputs; (2) reject missing shader pairs and stale HLSL snapshots before common Cook; (3) verify exact cooked permutations in native Development APNG probes; (4) draw/read back material pixels and integrate private source depth/stencil plus cached production commands. Step 4 remains pending and is not implied by shader availability.

R631: after synchronous common-material CacheShaders, all 12 common material families passed the exact custom VS/PS availability check on Local, default GPU skin, and unlimited-influence GPU skin vertex factories (36 pairs). Runtime APNG shader compilation remains disabled. The latest diagnostic submission adds preflight rejection and is being rebuilt as R632 before Cook/native verification. No new FPS or 100-card cold-load result in this unit.

R645 checkpoint: R638 common bake verified all 36 custom VS/PS pairs; R640 Cook contains 17 common assets + material shader library, no private card assets. R641 native Development build succeeded (20.62s), immutable R642 executable. R644 C011/C002/C000 each exit 0 and pass exact cooked mesh-shader availability, existing pose/material/raster gates. APNG snapshot durations 1934.698/3213.054/1775.409ms are individual preparation timings, not frame times or 100-card cold-load acceptance. Receipt: Saved/Crowd/RuntimeSourceMaterialMeshAcceptanceR645.json.

Remaining: actual material mesh draw/readback and character CPU/GPU screenshots, eyebrow recipe/source depth-stencil scene integration, cached production submission, APNG room integration, remaining source material families, clean 100-card cold loading and Development frame mean/P95. No new frame-time/FPS result. Current launcher remains unchanged.

## R646 actual source material mesh pixel oracle

Plan: draw a one-shot common ForgeNose material through the new source mesh VS/PS on a private local-VF/primitive-UB fixture, use four known texture texels, read back UNORM pixels, compare native RGB/alpha blend within 1 byte. No card-specific Cook assets, runtime shader compilation, scene stencil mutation, or per-frame readback. This tests actual common shader/VF/UV/material submission; animated character pixel parity and production cached pass remain pending.

R665 checkpoint: R659 common bake verifies 48 VS/PS pairs (12 families × four VFs including the private oracle VF). R660 Cook includes only 17 shared assets plus material shader library. R661 native Development build succeeded in 20.59s; immutable R662 executable. R664 C011/C002/C000 each exited 0 and passed the actual common ForgeNose mesh shader GPU pixel oracle, alongside prior pose/material/raster checks. Four sampled texels match RGBA (26,51,77,64), (0,0,0,64), (128,128,128,64), (0,0,0,0) within 1 byte/channel. The same shader entry points use a diagnostic local-VF implementation with private primitive uniform buffer; this is actual shader/material/UV/blend drawing, not animated character visual parity.

Explicit view and instanced-view RDG references fixed the blank draw. No per-card Cook resources or runtime shader compilation; the one-shot readback is not a production frame-loop feature. Receipt: Saved/Crowd/RuntimeSourceMaterialPixelsAcceptanceR665.json. Animated skinned character screen comparison, eyebrow/source stencil scene integration, production command caching, APNG room integration and clean 100-card frame mean/P95/≤60s cold-load tests remain pending. This unit provides no new FPS result.

## R666 material mesh depth/stencil plan

Extend the actual common material mesh oracle with four independent private targets: single near draw; stencil Replace2 followed by Equal2 accept and Equal3 reject; near depth-write-on followed by a far draw; same geometry with near depth-write-off control. Each target samples four known source texels. Accept/reject and depth on/off controls must produce different alpha-layer counts; no source card exceptions, engine scene stencil mutation, runtime shader compile or per-frame readback. Animated character parity and production cached pass remain pending.

R672 checkpoint: R666 common bake, R667 common-only Cook, R668 native Development build (21.53s), immutable R669 executable. R671 C011/C002/C000 each exit 0 and pass original material pixels plus all four actual material mesh depth/stencil control cases. Stencil Replace2 and Equal2 result in two alpha layers, Equal3 rejects a third; near .8 reversed-Z with depth write rejects far .4, while no-depth-write control accepts it. RGB/alpha error <=1 byte per channel. Receipt Saved/Crowd/RuntimeSourceMeshRasterAcceptanceR672.json. This confirms the generic source-state binding in actual material mesh submission; it does not confirm animated character parity, source-stencil scene compositing or cached production performance. No new FPS/frame mean/P95 or 100-card cold-load result.
