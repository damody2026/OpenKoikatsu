# GPU renderer history checkpoint — 2026-10-05

## Implemented and verified

Actual renderer-buffer diagnostics now compare current and previous matrices to
native CPU results after mesh-update prerequisites. Initial ON/reset deliberately
duplicates current and does not compare its previous slot with pre-switch history.
Eight successive diagnostic samples plus component population growth are checked.
Per-actor held playback selects current as previous independently of other actors.

The OFF path skips all-character mesh scans and playback/finalized-clock render
commands; a single transition disables the renderer bridge. ON rescans and resets.
No geometry, texture, material, shadow, near-face or animation-rate quality change.

DevelopmentHost-IndependentGpuHistoryR3 built successfully. Formal cooked scene
RenderHistoryR2 passed launch ON plus OFF/ON/OFF, all 100 distinct cards animated,
15 actual GPU components in ON, zero GPU actors/components after OFF.
Cumulative matrix comparisons: 179,439 total including 12,762 previous matrices;
maximum scalar error 0.000186920166015625, zero mismatches (tolerance 0.001).
This does not cover world-transform motion vectors or complete visual acceptance.
Physical F8 key focus was not injected; control JSON exercises the same state path.

## Uncapped current-version timing

Formal Development scene: 100 distinct cards, third-person 1600x1000, TAA 100%,
60 Hz animation, eight workers/default affinity, VSync off and t.MaxFPS=0.
CPU/GPU/CPU phases each last 60 seconds, excluding the first ten. No readback,
owned build or oracle overlaps timing. Each phase ends with 45 high-face and
45 shadow-active residents. All measured frames retained.

| Mode | Frame mean ms | Approx FPS | Frame P95 ms | Game mean ms | Render mean ms | RHI mean ms | GPU mean ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| CPU before | 22.504 | 44.4 | 31.590 | 14.852 | 22.493 | 5.026 | 1.825 |
| GPU ON | 22.668 | 44.1 | 32.738 | 14.972 | 22.658 | 5.115 | 1.885 |
| CPU after | 21.800 | 45.9 | 32.892 | 14.511 | 21.779 | 4.979 | 1.808 |

CPU control drift is 3.13%. Background process monitoring confirms another
ok-preview-pack conversion process was actively consuming CPU throughout.
This is contaminated timing evidence, not clean speedup acceptance; no improvement
claim is made. Do not compare these absolute numbers to an earlier quiet-session
baseline as proof of code regression or benefit. The tested intermediate version
does not meet 120 FPS (8.333 ms frame mean/P95).

GPU fixture still contains ten cards; seven actors/fifteen actual rendered components
use it in the matched view. Native CPU animation remains enabled for every actor.
This is history correctness and idle-OFF overhead work, not completed CPU offload.
Prepared scene load wall time: 7.118 seconds, not cold random conversion acceptance.

Evidence: Saved/Crowd/GpuAnimationControl-RenderHistoryR2.json and phase reports;
Saved/Crowd/GpuAnimationControl-UncappedHistoryR1.json and -1/-2/-3.json;
Saved/Crowd/UncappedHistoryR1-process-deltas.json.
Binary SHA256: BA902787D423DB9C96BCB50312AC23DA1933F166FF5B7BC4B8066531166D5514.

## Next implementation boundary

Ordinary skeletal dynamic-data construction still calls UpdateRefToLocalMatrices.
Stopping native pose without replacing near followers/bounds/render-data ownership
would freeze required consumers. Implement a private GPU transform provider or
GPU-owned skeletal dynamic-data path, plus source-validated per-card/near-part
cache delivery, before removing supported CPU pose/palette work. Distinct characters
keep independent skeletons/clips/phase; sharing is not required. Full acceptance
follows after that integration is complete.
