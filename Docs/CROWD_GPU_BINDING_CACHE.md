# Independent GPU bridge binding cache

2026-10-05. Caches character, clip and mesh path resolution without changing geometry, materials, 60 Hz animation or hardware policy. This does not remove native CPU animation evaluation.

Enabled frames check complete actor component membership using weak object identity/serial numbers. Same-count replacement and allocator address reuse invalidate component lists. Mesh changes refresh lookups; clip identity/duration remain checked. Removed actors/leaders drop metadata/callbacks. Existing primitive IDs rebind when mesh, manifest actor or part changes; render palette metadata resets on rebind. Unsupported sources retain native CPU rendering. OFF still avoids all-character scanning/playback upload.

Standalone Development builds BindingCacheBaselineR1, BindingCacheImplementationR1 and BindingCacheFinalR2 succeeded. Formal BindingCacheFunctionalR1 passes OFF/ON/OFF, retaining 100 distinct animated cards. ON: 172,056 compared matrices including 5,331 previous, zero mismatches, max scalar error .000091553 <= .001. Cumulative metadata counters: 100 character lookups, 10 clip lookups, 251 mesh lookups, 28 component rebuilds. Final weak-identity refinement requires fresh evidence below.

## Pre-change timing: excluded from speedup acceptance

Formal Development CrowdRoom, third-person 1600x1000 TAA 100%, 60 Hz animation, eight workers/default affinity/Normal priority, unchanged quality. Each 60-second window excludes its first ten seconds.

| Mode | Frame mean ms | P95 ms | Game ms | Render ms | RHI ms | GPU ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU before | 18.70 | 25.26 | 12.42 | 18.69 | 4.23 | 1.85 |
| GPU bridge | 17.22 | 21.68 | 11.08 | 17.21 | 4.05 | 2.00 |
| CPU after | 17.61 | 24.74 | 11.39 | 17.61 | 3.90 | 2.20 |

BindingCacheBeforeR1-processes.json records significant foreign UnrealEditor/rustc CPU work. CPU controls drift 5.78%, exceeding the 5% guard. No speedup is accepted; comparison to a later quiet session cannot prove improvement. These results do not meet 8.333 ms / 120 FPS.

Tools/watch_crowd_processes.ps1 records read-only process CPU deltas keyed by PID/start time. Protected/exited processes are unavailable, not zero usage; foreign GPU use is unmeasured. No foreign process is stopped.

The formal harness separates functional near/far cycles (-VerifyGpu -FaceCycle), CPU trace diagnostics (-Profile, no readback) and throughput (-Benchmark, no trace/readback). CPU pose and a ten-card cache fixture remain; all-card GPU ownership and complete visual/load/FPS acceptance remain outstanding.

## Dynamic transition validation in progress

The steady-state pass does not establish near/far correctness. R2/R6 exposed current palette mismatch on restored low parts; R7 adds the same required-bone refresh used when detail followers are added. R7 and R9 still exposed previous palette mismatch immediately after restoration. R10 sends explicit current/prior finalized clip times and resets native/GPU motion history on required-bone topology changes. Existing bindings are no longer exempted from previous checks merely because renderer submissions skipped frames.

The explicit-history implementation currently evaluates prior and current GPU poses separately; its added GPU cost requires profiling. It does not yet eliminate CPU pose evaluation. No 120 FPS or all-card GPU acceptance is claimed. See CROWD_GPU_POSE_ERRORS.md and fresh R10 evidence for results.

### R10 functional result

Standalone Development build succeeded (25.36 s). Formal CrowdRoom OFF/ON/OFF and near/far round-trip passed, retaining 100 distinct animated cards. ON compared 164,028 matrices including 3,273 prior matrices, zero mismatches, max error approximately .000090599. Metadata counters: 100 character lookups, 10 clip lookups, 151 mesh lookups, 19 component rebuilds. Receipt: Saved/Crowd/GpuAnimationControl-PoseTransitionHistoryR10.json. This validates the current fixture and transition implementation, not complete all-card visual or performance acceptance.

## R11 current implementation throughput (not clean acceptance)

Same formal scene, uncapped Development, no readback/trace; OFF/ON/OFF 60-second windows, first ten seconds excluded. No new hardware tuning or quality reduction.

| Mode | Frame mean ms | P95 ms | Game ms | Render ms | RHI ms | GPU ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU before | 17.849 | 21.708 | 11.504 | 17.834 | 4.152 | 1.833 |
| GPU bridge | 17.947 | 21.643 | 11.406 | 17.939 | 4.196 | 1.904 |
| CPU after | 17.516 | 21.218 | 11.170 | 17.508 | 4.119 | 1.836 |

CPU drift 1.864%, below 5%; no speedup demonstrated. Render remains dominant and CPU pose remains retained. These are ~56 FPS, not 120 FPS. Process monitor records foreign BloonsTD6, SuperExplorer, Discord and other activity; foreign GPU utilization is unmeasured. Therefore not clean final FPS acceptance. Receipt GpuAnimationControl-PoseHistoryBenchmarkR11.json; process evidence PoseHistoryBenchmarkR11-processes.json. Do not compare against the prior contaminated build to claim metadata speedup.

## R12 warm CPU profile

Formal scene CPU trace completed, separate from readback and throughput. Insights anchor is 44.1756877; bounded CPU export 54.1756877-59.1756877, all timers, summarized by Tools/summarize_crowd_cpu_events.py. 272 renderer/scene scopes in this five-second window. Source Profile-PoseHistoryR12Warm-Exclusive.csv; raw trace GpuAnimationControl-PoseHistoryProfileR12-2.utrace.

| Scope / thread group | Exclusive CPU work ms over window |
| --- | ---: |
| RenderThread WaitForTasks | 2150.07 |
| RenderThread FTaskBase state-event wait | 1053.97 |
| Worker ParallelDraw | 4053.83 |
| Worker rotation decode | 3044.31 |
| Worker translation decode | 2632.32 |
| Worker MeshObject Update | 2781.22 |
| GameThread MeshObject Update | 646.16 |
| Worker native mesh-object transform provider | 948.28 |
| RHI FD3D12Queue ExecuteCommandLists | 592.51 |

These are aggregate CPU scope durations; parallel threads overlap and wait scopes represent dependencies/idle time. They cannot be added into frame time or interpreted as guaranteed savings. RHI VBlank service-thread wait is not an active rendering bottleneck. ParallelDraw/task counts are not draw calls.

CrowdIndependentGpuTick: 272 calls, 31.275 ms inclusive total, .11498 ms average; only two component-cache rebuilds (.0075 ms total). Metadata scanning is not the dominant cost. Major remaining work is genuinely removing native CPU animation/decompression and palette/mesh updates for supported independent GPU actors, and reducing render-command work. Current bridge preserves CPU evaluation as its reference/fallback, so successful matrix dispatch does not establish CPU offload. GPU ON is not recommended as a performance default from these results.

## R14 independent current/history resident caches

InitializeShared shares reference-counted immutable key/track/task/bind/initial-selection buffers, but each pose evaluator owns its playback selection, clock and output. No per-card or shared-pose assumption. Self-sharing, missing initialization and already-evaluated source are rejected. A source release does not invalidate the sibling's referenced immutable resources. Both states release during scene teardown. Current/history generation checks use each evaluator's generation.

Development build R14 succeeded in 21.43 s. Formal OFF/ON/OFF and near/far round trip passed: 100 distinct animated cards, 164028 compared matrices including 3273 prior matrices, zero mismatches, maximum .000118255615234375 < .001. History immutable upload bytes = 0. Current/history evaluations = 1381 each, reuses = 0 in this capped, sub-60-FPS diagnostic. This demonstrates resource sharing and correctness, not savings in a run whose clocks change each render submission. Throughput R15 is separate and contains no readback.

### Remaining requirement before native CPU ownership can stop

The current manifest identifies low meshes/palette ranges but does not publish bone identities for arbitrary newly loaded near meshes. Do not freeze a native leader while face/hair followers still require it. Full ownership needs validated per-mesh bone identity/inverse-bind mappings for every visible/shadow part, material/morph compatibility, independently driven animation time, and an atomic CPU/GPU handoff on near-detail, clip, outfit and unsupported-feature changes. Unsupported parts must keep the CPU owner. This requirement was discovered before disabling anything; no quality-reducing freeze shortcut was applied. CPU evaluation and native palette/update elimination remain unfinished.

### R15 throughput: excluded due control drift

Same standalone Development formal scene / quality / third-person camera, OFF/ON/OFF without readback/trace. CPU before 21.453 ms mean / 27.795 P95; GPU bridge 21.551 / 28.197; CPU after 25.174 / 30.254. GPU ON Render 21.535 ms, RHI 4.626 ms, GPU 1.938 ms. CPU control drift 17.346% fails the 5% guard. The read-only process monitor records another OpenKoikatsu process (521532 CPU ms) and UnrealEditor (313891 CPU ms) during this run. Their GPU load is unmeasured. Do not infer speedup/regression or compare to R11. 120 FPS remains unachieved; current/history resource separation is functionally verified only. Receipts GpuAnimationControl-SeparatePoseHistoryR14.json and GpuAnimationControl-SeparatePoseHistoryBenchmarkR15.json.
