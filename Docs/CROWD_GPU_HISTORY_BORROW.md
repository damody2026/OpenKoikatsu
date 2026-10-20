# Exact resident pose borrowing (2026-10-05)

`FOKResidentGpuPose::TryReuseCurrent` registers the last resident current
buffer when every actor's owned clip selection and all four playback
parameters match, and generation/count/lifetime checks pass. It does not
advance frame history, upload parameters or dispatch shaders. A miss returns
an empty result; the existing independently evaluated history path remains.

The production provider queries before current evaluation replaces the pooled
buffer. RDG holds the borrowed resource in the current graph. Per-component
initialization and native finalized-pose history/reset rules remain unchanged.
No shared animation, skeleton or character assumption is introduced. The
whole batch must match; no approximate time tolerance is used.

`independent_gpu_history_pose_reuses` now includes direct resident borrowing
as well as history-owner cache hits. `history_pose_evaluations` counts actual
history dispatches. Native CPU pose is still retained.

## Validation

- Standalone Development build R21 succeeded.
- Synthetic R24: 100 independent actors, 20 owned clips each; selection,
  changed time/clip, stale generation, wrong actor count, released cache and
  replacement within the same graph checks pass. Query leaves evaluation and
  upload counts unchanged; replacing current preserves the borrowed buffer.
  28,800 GPU elements compared, maximum error 0.00000143051147.
- Formal R21 near/far run: 585,045 current matrices, zero mismatches. However,
  zero previous matrices were available, so the strict harness rejected this
  run. It is not complete history acceptance.
- Formal R23 OFF/ON/OFF with GPU-only provider probe passed: 1,303,881 native
  comparison matrices including 10,242 previous matrices, zero mismatches,
  maximum error 0.000344276429 against existing tolerance 0.001. GPU-only probe
  compared 14,022 previous matrices with zero mismatches, max 0.0000152587891.
- R23 current evaluations 1,597; history evaluations 20; history reuses 1,577.
  About 98.7% of separate history evaluations avoided in this diagnostic.

This saves redundant GPU history evaluation and its graph/upload setup; it
does not remove CPU animation or draw-command costs. Matrix correctness is
not full visual acceptance or evidence of 120 FPS. Ten-card resident fixture
coverage is not 100 GPU-owned characters. Timing evidence is recorded below
only after the separate uncapped/no-trace/no-readback R25 run completes.

## R25 uncapped timing (not clean performance acceptance)

No trace, matrix readback or provider probe. Same formal 100-card scene,
resolution and quality, OFF/ON/OFF with 50-second sampled windows each.

| Mode | Mean / P95 ms | Game ms | Render ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|
| CPU before | 18.777 / 24.561 | 12.307 | 18.768 | 4.250 | 1.852 |
| GPU bridge | 21.960 / 28.622 | 13.440 | 21.943 | 5.061 | 1.951 |
| CPU after | 19.606 / 26.312 | 12.480 | 19.598 | 4.613 | 1.835 |

CPU control drift 4.415% passes the harness's 5% threshold, but independent
process monitoring records substantial foreign work: rustc 209624 CPU ms,
SuperExplorer 127418 CPU ms, multiple cl compilers and dotnet. These were not
terminated. Foreign GPU use is unmeasured. This run is not a clean comparison
and does not establish speedup, regression, or 120 FPS. The ON bridge retains
CPU pose, and ends with 31 executing GPU components; it remains incomplete
GPU ownership. `passed=true` in the receipt means the harness completed,
not performance acceptance; `speedup_acceptance=false` and
`full_gpu_fps_acceptance=false` remain explicit.

Evidence: Saved/Crowd/GpuAnimationControl-HistoryBorrowTimingR25.json and
Saved/Crowd/HistoryBorrowTimingR25-processes.json. Launcher ValidateOnly passed
with the updated standalone binary SHA256
3F743FED8B84139439CA1723F37E4BDC8162DF8680CEA153F123E4FE0117A2DA.

Next structural work remains complete GPU component ownership, independent
production clocks/bounds/morph support, coherent native fallback, and removal
of native CPU pose/dynamic updates for fully owned actors. This history-cache
change alone cannot address the dominant native costs identified by R20.
