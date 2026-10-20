# Constant-key scale decode cache (optional)

This is a runtime, per-animation cache of single-key scale values. It does not share poses between characters, bake frames, change animation assets, lower animation frequency, or change any rendering setting. Private rigs and all other animated channels keep their original paths. Following exact-output checks and two matching forward/reverse comparisons, the compatible game crowd scene enables it by default. The Engine-wide cvar still defaults off; other games and authoring Editor are unaffected.

## Engine compatibility

ConstantScaleCache.patch targets local UE 5.8 AnimCompress.cpp and includes the three BitwiseDecode profiling scopes. Apply this complete patch OR the profiling-only BitwiseDecodeScopes.patch, not both. Inspect existing Engine changes before applying. The API is cpp-only and does not change class layout. Build/stage the isolated monolithic Development host; authoring Editor Engine DLLs are not replaced. Project capability detection prevents unsupported linking and the registration API rejects Editor usage.

Only explicitly registered, immutable cooked BitwiseCompressOnly/ConstantKeyLerp clips with a complete scale offset table qualify. Unsupported clips continue through the original decoder. Registration decodes once with the existing scale codec, preserving its exact native values; only tracks with exactly one scale key are cached. Variable tracks continue through the original codec with their original time/interpolation and mapping.

Registry keys include the compressed-data identity. Weak owning sequence references, compressed-byte/scale-offset addresses, sizes and decoder-mode checks prevent using stale data or mixing the scalar/SIMD paths. The registry owns scalar values, never animation assets. Expired entries are removed on registration. Mode/data/owner incompatibility falls back to the original decoder. Revalidate this immutable-data assumption if runtime recompression or mutable compressed assets are introduced.

## Validation

Launch `Tools\lua\lua.exe Tools\run_crowd_development.lua` with `--constant-scale-cache --verify-constant-scale-cache` for functional checks. The diagnostic independently invokes the original scale decoder on every requested pair, then compares each native FVector byte for byte. Any mismatch makes the experiment fail, restores original output on that call, and disables subsequent reuse. This diagnostic adds substantial work and cannot be used for FPS acceptance. Counters are cumulative, not saved milliseconds.

ConstantScaleVerifyR1 passed 100 prepared distinct animated characters, 100 registered clips, zero unsupported clips and zero mismatches across 308,493,063 compared scale vectors. 221,993,505 values were served from the single-key cache (about 72%); 86,499,558 variable values used the original codec. Face/hair LOD round-trip, low-body and current-shadow gates passed. Warm prepared low-only load was 7.850 seconds; this is not first-time arbitrary-card conversion or cold loading proof. The changing-detail diagnostic frame time is not a benchmark.

## Performance comparison

Use `Tools\lua\lua.exe Tools\test_crowd_bone_packing.lua --tag <fresh-tag> --modes 0,0,0 --constant-scale-cache-cases 0,1,0 --seconds 30`. The launcher registers clips first; control switches reuse off/on/off, with a reset and warm-up per case. Verification is off during timing. Actual mode, registration/hit/failure counters and current near shadows are checked. First and final baselines must use the same cache, verification, decoder, render and quality configuration. Do not infer benefit from a difference smaller than baseline drift.

ABR1 off/on/off Render Thread means: 16.303 / 15.541 / 16.122 ms (1.11% baseline drift). ABR2 on/off/on: 15.040 / 16.011 / 15.244 ms (1.37% baseline drift). Camera, 45 near faces and current-shadow population matched. Gains exceed the measured baseline drift in both orders; the observed RT reduction is approximately 0.6–1.0 ms, not a 120 FPS result or a guarantee on other selections/systems.

Disable with -CrowdDisableConstantScaleCache on the game command line, or `--no-constant-scale-cache` in `Tools\run_crowd_development.lua`. The comparison wrapper explicitly disables registration when no ConstantScaleCacheCases are supplied, preserving isolation of historical provider/debug/matrix experiments. Unsupported game animation data always retains the original decoder.

The scalar/SIMD diagnostic is separately exposed as -ConstantKeyISPCCases 1,0,1. It is not adopted: timing differences fell within 4.36% baseline drift and the read-only sample had 3,149 nonidentical float matrices out of 13,854 (maximum element difference 0.000132936). The scale cache keeps the original decoder mode and passed exact comparisons.
