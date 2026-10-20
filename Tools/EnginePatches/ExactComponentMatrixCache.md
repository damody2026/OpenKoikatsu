# Exact component matrix conversion cache

Apply ExactComponentMatrixCache.patch from the UE 5.8 engine root after
`git apply --check <absolute patch path>`. An already applied patch is verified
with `git apply --reverse --check`. Rebuild the consuming Development Game
target; no header or shader changes are needed. The project and generated host
detect the stats function in engine source and compile the optional query only
when available and outside Editor. Missing Engine capability is rejected by
the controlled-test mode gate. Source detection does not replace rebuilding.

`r.SkeletalMesh.ReuseComponentMatrices` defaults to 0. Mode 1: each active worker owns
1024 direct-mapped slots. Each slot stores all source FTransform bytes and the
original float32 matrix conversion result. A byte-identical source can reuse
that pure conversion. Changed data, signed-zero differences, padding
differences, index collisions and memory-address reuse cause a miss. No source
array/component pointer or pose revision is trusted for equality. The cache
has no card/skeleton identity dependency and no cross-thread locks; memory is
bounded per worker. In local LWC layout a slot is about 176 bytes.

Only `(FMatrix44f)ComponentTransform[index].ToMatrixWithScale()` is cached.
The current/previous input arrays, visibility tests, removed-bone fallback,
required LOD bones, each part's private leader map and inverse-bind matrix
multiplication still follow the original engine code. This is not pose sharing,
bone reduction or an animation-rate change. World/primitive motion is untouched.

`r.SkeletalMesh.VerifyComponentMatrices=1` independently recomputes every cache
hit with the original expression and compares all 64 output bytes. Any mismatch
returns the original result and disables cache use for subsequent passes.
Stats publish once per conversion pass to reduce contention and are exposed in
the scene report: hits, misses, verified conversions, mismatches and failed.
Reports are cumulative process counters, not per-frame timings. The candidate
stays off by default. Verification runs are functional evidence, not FPS tests.

Example functional command:
`Tools\lua\lua.exe Tools\test_crowd_bone_packing.lua --tag <fresh> --modes 0
--component-matrix-cache-cases 1 --verify-component-matrices --seconds 30`.
Untraced performance command:
`Tools\lua\lua.exe Tools\test_crowd_bone_packing.lua --tag <fresh> --modes 0,0,0
--component-matrix-cache-cases 0,1,0 --seconds 30`.
Leave debug-copy mode constant when measuring this candidate independently.
Do not adopt from a camera/population mismatch or repeated-baseline drift.

Mode 2 builds only the translation matrix directly when the source quaternion
is byte-identical to FQuat::Identity and all scale components are exactly 1.
It uses the same double-to-float casts for translation. Negative quaternion
zero bits, rotations and non-unit scales keep the original expression. This
mode allocates no thread cache. The same verification checks every accelerated
conversion; report hits mean fast conversions in mode 2. Eligibility is tested
by values, not by card IDs, skeleton names or hardware. In the measured scene,
only about 0.45% of conversions qualified. The candidate remains off.
Use ComponentMatrixCacheCases 2 for verification, or 0,2,0 for a performance A/B.
