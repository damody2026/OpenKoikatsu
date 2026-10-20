# Independent GPU animation scene control

The formal CrowdRoom scene owns the independent-animation request control and
an opt-in resident pose/palette renderer bridge. A validated matching cache is
required for GPU execution; absence or unsupported mesh uses CPU fallback.
The current integration retains native CPU animation for correctness. It does
not activate the old `CrowdGpuPose` MatrixBank prototype or CPU-matrix gather.

## Controls

- F8 toggles the request while playing, including third-person WASD mode.
- F9 shows/logs the current request and actual execution status for eight seconds.
- Console: `r.OKCrowd.IndependentGpuAnimation 1` requests ON; `0` requests OFF.
- Launch: `-CrowdIndependentGpuAnimation` requests ON before loading residents.
- Blueprint on `AOKCrowdScene`: `SetIndependentGpuAnimationEnabled`,
  `IsIndependentGpuAnimationRequested`, `GetIndependentGpuAnimationStatus`.
- Existing performance-control JSON accepts a Boolean
  `"independent_gpu_animation": true/false` for automated formal-scene runs.

Only 0/1 is accepted by the console control; invalid values normalize to CPU.
The console request is process-wide; scene state synchronizes on the game thread.
Repeated identical requests do not increment transitions. Status text and the
JSON report deliberately distinguish request state from execution. GPU-requested
runs cannot pass full GPU FPS acceptance while native CPU pose is retained. A request changed
inside a sample window invalidates that window even if eventually disabled;
a new performance-control window clears that marker.

## Current report fields

`independent_gpu_animation_requested`,
`independent_gpu_animation_backend_ready`,
`independent_gpu_animation_executed_components`,
`independent_gpu_animation_request_changes`,
`independent_gpu_animation_changed_during_samples`,
`independent_gpu_animation_status`.

Backend-ready and executed component/actor counts reflect actual registered
renderer execution. Without a cache they remain false/zero. Reports also include
render passes, verification components/matrices/error/mismatches and
`independent_gpu_animation_cpu_pose_retained`. Do not use a capped diagnostic
ON/OFF test as a throughput comparison.

## Reproducible verification

Build the isolated Development host, build the oracle host for the same plugin
sources, Cook a matching SM6 global-shader patch, then run the control test:

```powershell
./Tools/run_independent_gpu_pose_oracle.ps1 -Tag SwitchShaderCapabilityR1 -ManifestSynthetic
./Tools/build_crowd_development_host.ps1 -Tag IndependentGpuSwitchR3
./Tools/cook_crowd_gpu_shader_patch.ps1 -Tag ControlR2
./Tools/test_crowd_gpu_animation_control.ps1 -Tag FormalR3 -ShaderPatch C:/portable/OpenKoikatsu/Saved/Crowd/GpuShaderPatch-ControlR2_P.pak
```

Use fresh tags for reruns. The smoke test uses a separate executable/save directory
and mounts a separate shader patch via Development's `-pakdir`; existing Engine
and cooked assets are shared through junctions. Stable/authoring executables and
source assets are untouched. It exercises launch ON then runtime OFF/ON/OFF,
checks 100 distinct animated cards, low-only startup, third-person/player control,
request transition counts, truthful CPU fallback and FPS gate. It is capped at
60 FPS to reduce unnecessary load; all results are functional-only. It does not
inject physical F8/F9 presses or verify key focus through UI automation.

Independent hierarchy traversal requires SM6. A render-thread capability check
rejects unavailable resident initialization/evaluation before GPU allocation or
dispatch. FXC/SM5 cannot compile the dynamic ancestor stack; this is a software
capability requirement, with CPU fallback, not hardware-model-specific tuning.

## Remaining delivery

Deliver production source-freshness/catalog and all-card caches; validate near
parts and previous rendered transforms, then remove CPU pose work for supported
components. Compare CPU/GPU/CPU and reversed controls in the formal cooked
Development 100-character third-person scene at identical quality and 60 Hz
animation. Full visuals/loading/120 FPS acceptance remains open.

## Verified evidence

`Saved/Crowd/GpuAnimationControl-FormalR3.json`: isolated cooked Development
scene passed launch ON and three completed runtime control windows OFF/ON/OFF.
Each window retained 100 distinct, animated cards, initial low-only startup,
third-person player control, and truthful zero GPU-rendered components. Request
transition counts were 1/2/3; each fresh window had no mid-window request change.
The ON window correctly did not qualify for FPS acceptance. Exit was clean.
Keyboard routing compiled but physical key presses were not injected.

`DevelopmentHost-IndependentGpuSwitchR3.log`: final Development build succeeded;
scene source snapshot and tested executable hashes match. The previous stable
stage was not replaced. The isolated tested executable requires its shader patch
argument; run it with `-pakdir=C:/portable/OpenKoikatsu/Saved/Crowd/GpuAnimationControl-FormalR3.stage/ShaderPatch`.

`IndependentGpuPose-SwitchShaderCapabilityR1.json`: after the capability change,
100 independent actors x20 synthetic owned clips still pass GPU pose/palette
comparison (28,800 elements, max error 0.00000143051147, tolerance 0.0005).
It is a numerical regression, not actual crowd GPU rendering or FPS evidence.

The first shader patch includes a duplicate Cook metadata archive that is not
loaded by gameplay; the helper now excludes metadata copies for future patches.
The tested patch itself is retained unchanged for reproducibility.
## Actual renderer bridge milestone (2026-10-05)

The opt-in `-CrowdIndependentGpuCache=<file>` and `-CrowdIndependentGpuIdentity=<40hex>` now load a jointly validated cache and sidecar manifest asynchronously. Matching low mesh proxies use independent resident GPU pose and private palette writes directly into the renderer transform buffer. Unsupported meshes/near parts remain CPU; native CPU pose remains enabled for this integration milestone. F8 and the existing control JSON toggle the registered bridge in the formal scene.

`GpuAnimationControl-RenderVerifyR4.json` passed OFF/ON/OFF with all 100 distinct cards animated. The ON sample ended with 7 actual GPU actors / 15 GPU components. Actual renderer readback verified 30 components / 25,872 matrices, maximum error 0.00006103515625 and zero mismatches. These counts reflect a ten-card fixture, not 100 GPU actors. Previous-buffer visual/history acceptance, production source freshness/catalog, all-card cache delivery and removing CPU pose work remain open.

`IndependentGpuPose-JointBundleLoaderR1.json` passed two-real-card regression and four worker bundle rejection cases (identity, absent manifest, byte budget, absent cache). Cache/manifest publication is atomic at the worker result boundary; immutable data and render resources remain separately owned.

Use `Tools/test_crowd_gpu_animation_control.ps1 -Benchmark` for uncapped CPU/GPU/CPU timing with 60-second phases and ten-second excluded warm-up. This mode rejects `-VerifyGpu`, reports per-frame mean/P95 and Game/Render/RHI/GPU means, and does not qualify this CPU-retaining hybrid for full GPU FPS acceptance. Default mode remains a capped functional test.

### Renderer history verification

The diagnostic now compares both current matrices and native previous matrices
when UE declares a previous-transform update. Initial ON/reset deliberately uses
current pose for both slots; its previous slot is not compared to an unrelated
pre-switch CPU history. Diagnostics repeat across eight samples and population
growth, rather than verifying only the first component count.

`independent_gpu_animation_verified_previous_matrices` reports how many native
previous matrices were compared; `verified_matrices` is now cumulative and includes
both current and previous comparisons. Mismatch remains a session-wide CPU fallback.
Per-actor held playback selects current as previous even if another actor in the
same independent batch advances. No shared animation or skeleton is assumed.

OFF retains existing registrations but skips per-frame character/mesh scans and
playback-array render-command submission. Finalization callbacks also skip command
submission while OFF. The next ON rescans current components before GPU registration
and resets pose history. Native CPU animation remains enabled throughout.
