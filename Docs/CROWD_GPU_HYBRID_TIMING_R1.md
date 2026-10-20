# Formal Development intermediate GPU bridge timing — 2026-10-05

120 FPS requires frame mean and P95 <= 8.333 ms. This run did not meet the target.

Actual cooked CrowdRoom, 100 distinct animated cards, third-person player camera,
1600x1000 TAA 100%, 60 Hz animation, eight workers, default affinity/Normal priority.
Same current quality policies; all three endpoints have 45 near high faces and
45 shadow-active residents. VSync off, FPS cap zero, no diagnostic GPU readback,
no owned build/oracle running. Each phase lasts 60 seconds with first ten seconds
excluded; all remaining frames retained. CPU means here refer to the same new
executable with its GPU bridge disabled and the same selected cache present.

| Mode | Frame mean ms | Approx FPS | Frame P95 ms | Game mean ms | Render mean ms | RHI mean ms | GPU mean ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| CPU before | 14.393 | 69.5 | 19.241 | 9.385 | 14.383 | 3.155 | 1.790 |
| GPU bridge ON | 14.042 | 71.2 | 18.252 | 8.847 | 14.032 | 3.063 | 1.834 |
| CPU after | 17.377 | 57.5 | 25.167 | 11.338 | 17.347 | 3.745 | 1.880 |

Measured windows are 50.017/50.016/50.029 seconds, with 3475/3562/2879 frames.
Mean FPS is 1000 / mean frame ms. Thread stages overlap; do not add them.
Prepared scene load wall time was 5.687 seconds, not random cold conversion proof.

The GPU fixture contains ten cards and only seven actors / fifteen rendered
components use it in this matched view. All 100 native CPU pose evaluators remain;
this intermediate comparison cannot establish benefit from removing CPU pose work.

## Invalid speedup comparison

CPU after is 20.73% slower than CPU before. Read-only process delta monitoring
recorded other application CPU use, including a foreign UnrealEditor process
created during the run at local 14:34:19. Foreign applications were not terminated.
The initial 0.352 ms GPU-before difference is smaller than control drift; no
GPU speedup claim or final FPS acceptance is valid. The raw timings still document
that this tested intermediate version did not sustain the requested 120 FPS.

Evidence: `Saved/Crowd/GpuAnimationControl-UncappedHybridR1.json`, phase `-1/-2/-3.json`,
`Saved/Crowd/UncappedHybridR1-process-deltas.json`.
Tested executable SHA256: `2B309E6D944635D8546AB760EA706DDAD928AA9FB295C46F444E54C1BA111FAE`.

Next acceptance requires production all-card cache and removal of supported CPU
pose/palette work after previous-transform/near/shadow/bounds compatibility checks,
then repeat matched uncapped controls in an uncontaminated session. Always report
frame mean/P95 and Game/Render/RHI/GPU means. No hardware-specific tuning or quality
reduction was introduced for this comparison.
