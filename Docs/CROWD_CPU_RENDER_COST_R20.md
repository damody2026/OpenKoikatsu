# R20 formal CrowdRoom CPU diagnosis (2026-10-05)

Standalone Development, 100 selected different cards, third-person camera,
1600x1000, existing TAA/material/shadow settings, animation cap 60 Hz.
No hardware tuning or quality changes. Binary SHA256:
55F5A66AF466F8B73D8FFCBF922E3ACE25C8F55E372601B897060D61328C20F2.

## Observed frame times

| Phase | Frame mean / P95 ms | Game ms | Render ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|
| CPU before | 17.007 / 20.923 | 11.117 | 16.999 | 3.952 | 1.844 |
| GPU bridge, trace enabled | 22.438 / 32.064 | 14.429 | 22.429 | 4.970 | 2.119 |
| CPU after | 15.679 / 21.263 | 10.012 | 15.671 | 3.494 | 1.865 |

These are diagnostic measurements, not comparable throughput acceptance:
only the GPU phase records CPU tracing, and CPU controls differ by about 7.8%.
No speedup/regression conclusion is justified. None reaches 8.333 ms.
Pipeline times overlap and must not be added. All 100 actors remain CPU
animated; the GPU phase reports 20 executed bridge components from the
ten-card resident fixture, not 100 fully GPU-owned actors.

## CPU-only bounded timeline

Trace: `Saved/Crowd/GpuAnimationControl-CpuRenderCostR20-2.utrace`.
Warm anchor: 44.3173937 seconds; all-timer CPU export window:
54.3173937 through 59.3173937. Summary:
`Saved/Crowd/Profile-CpuRenderCostR20Warm-Exclusive.csv`.
About 200 scene updates occur in this window.

| Thread group / scope | Exclusive CPU work in five seconds, ms |
|---|---:|
| RenderThread 0 / WaitForTasks | 2153.476 |
| RenderThread 0 / task state-change wait | 1034.512 |
| Worker / ParallelDraw | 4063.002 |
| Worker / BitwiseDecodeRotations | 2962.629 |
| Worker / BitwiseDecodeTranslations | 2606.471 |
| Worker / BitwiseConstantScaleCache | 1161.048 |
| Worker / MeshObject Update | 2658.212 |
| GameThread / MeshObject Update | 603.212 |
| Worker / native mesh transform provider | 983.132 |
| GameThread / Calc SkelMesh Bounds | 189.049 |
| GameThread / CrowdIndependentGpuTick | 38.398 |

Worker totals sum simultaneous thread work, not elapsed frame time or promised
savings. ParallelDraw counts are task invocations, not draw calls. Render
waits are dependency/idle time, not directly movable GPU computation.
`UpdateAllPrimitiveSceneInfos` has 1745.903 ms inclusive but only 50.967 ms
exclusive; treating its inclusive duration as local scene-update work would
misidentify the bottleneck. GPU timers in legacy statistics exports are not
CPU-only evidence; the table above uses the bounded all-timer CPU timeline.

## Implementation decision and remaining work

1. Preserve the native bridge as correctness reference. It currently replaces
   matrix output after native evaluation; it does not remove native animation
   decompression or MeshObject update costs. F8 alone cannot demonstrate CPU
   offload, and F9 only reports actual coverage.
2. Promote GPU-only rendering ownership with each actor's private skeleton,
   clips, inverse binds and original materials. Validate every visible and
   shadow component before switching an actor; incomplete ownership falls
   back atomically. Do not assume shared poses or one walking clip.
3. Move production clocks, clip transitions and prior-pose history out of the
   native evaluator. Supply animated bounds, morphology and motion-vector
   behavior, and restore native pose coherently for F8/detail/outfit changes.
4. Only after GPU-owned geometry renders correctly, remove native evaluator
   and dynamic uploads for owned actors. Verify CPU/GPU poses and images using
   separate reference actors so retained reference work does not masquerade
   as eliminated production work.
5. Profile draw-command recording separately after ownership migration;
   GPU bone computation does not merge the sections of different meshes.
   Then run uncapped OFF/ON/OFF without tracing/readback, identical camera,
   resolution, cards and quality. Report mean/P95 and background activity.

No CPU evaluator was disabled in R20. Source inspection shows that the native
provider's indirections depend on DirtyBoneTransforms and MeshObject updates.
Skipping RefreshBoneTransforms/upload alone can stop provider invocation,
leave stale skin-cache revisions and break bounds/morph/history. The provider
registration boolean selects skeleton batches; it is not an always-dirty or
async flag. Freezing updates is therefore not a valid optimization.

This run completes the refreshed diagnosis only. Full GPU ownership, CPU/GPU
visual acceptance, 100-card coverage and Development 120 FPS remain unfinished.
