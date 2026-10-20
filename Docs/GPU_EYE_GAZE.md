# Runtime GPU eye gaze

2026-10-05. Scope: APNG reader/runtime skeletal GPU skin eye sampling. This is independent of the crowd GPU-only animation provider, and is not a 120 FPS acceptance.

## Implementation

- Each GPU eye retains its three loaded sampling streams (main, upper highlight, lower highlight), with a dedicated material instance.
- The source `EyeDraw::apply` operations are affine: card ST, offsets, rotations and gaze ST. The runtime fits each current source channel to its loaded channel, validates all source vertices to 1e-5, and writes two shader rows per channel.
- The material uses regular nodes (homogeneous UV, two dot products) to transform coordinates before sampling. MainTex's static ST runs after the main gaze transform, matching the procedural path. Sampler clamp remains after interpolation, on GPU.
- Only the eye material parameters change on the GPU display path. No skeletal mesh rebuild, vertex/index buffer upload or shader compilation is requested by `SetEyeGaze`.
- Source parsing and generation of target UVs still happen on CPU through the existing Session API; this step does not claim to eliminate that work or move animation pose evaluation to GPU.
- Morph-capable eyes use the visible replacement material; the hidden poseable follower is not the parameter target. The same public `SetEyeGaze` and existing eye-look tracking call path work for CPU and GPU.
- This is driven by source eye data, with no card IDs, character names, hardware settings or shared-pose assumptions.

## Verification workflow

The reader audit accepts `OK_READER_GAZE_PROBE=left,right,vertical`. It exercises neutral, left, right, up, down, two opposing combinations and return-to-neutral before the final requested gaze, and verifies skeletal asset pointers remain unchanged.

`OK_READER_GAZE_SEQUENCE=1` captures nine states on the same loaded character, waiting for frames between each change. Files include `gaze00` through `gaze08`; this tests visible updates without reloading the card. `eye_uv` compares actual GPU buffer coordinates transformed by the live material parameters against fresh Session `eye_sample_uvs`, `uv2`, `uv3`, with .002 tolerance for half-precision buffer quantization.

```powershell
$env:OK_READER_GAZE_PROBE='.8,-.6,.7'
$env:OK_READER_GAZE_SEQUENCE='1'
Tools/run_gpu_skin_visual_audit.ps1 -Tag FreshTag -CardIds '2,1,50' -WornOnly -FacePortrait -FreezeFace -RequireBounds
```

Add `-CpuSkin` for the corresponding CPU screenshots. The image comparison tool matches filenames; image metrics are not an automatic visual acceptance.

## Failed evidence to preserve

`GpuLiveEyeGazeR1`, `GpuEyeGazeSequenceR1` and `LiveEyeGazeComparisonR1` are failed diagnostic evidence despite `valid=1`: the setter's procedural-component guard skipped GPU eyes, and the old audit compared stale cached UVs. Images exposed the lack of eye movement. They must not be counted as live-gaze acceptance. R5 fixes the guard and uses the fresh source UV oracle; see `CROWD_GPU_POSE_ERRORS.md`.

Development Editor build `GpuEyeGazeBuildR5.log` succeeded. Initial compile errors and test-oracle mistakes are recorded in the errors document. Remaining skin/pose geometry differences and formal-scene performance are separate outstanding work.

## Effective functional evidence

- `GpuEyeGazeSequenceR2`: three characters, nine sequential gazes each, 27 GPU images, valid=1/exit=0. `CpuEyeGazeSequenceR1` supplies the matching CPU views (the procedural path was unaffected by the GPU guard bug).
- `GpuEyeGazeSequenceComparisonR2`: 27 matching image pairs. Actual GPU movement is visible in left/right/up/down and opposing combinations. Returning to neutral has full-image MAE .116–.121/255 versus the first neutral frame; this is close, not a claim of pixel identity.
- 54 fresh-source UV checks pass, maximum normalized coordinate error .000343246. Skeletal asset pointers remain unchanged after gaze updates; face outline and bounds checks pass.
- Eye ROI CPU/GPU MAE: March 7th 1.827–1.977/255; Alice 2.685–3.173; Augusta 9.138–10.581. Augusta retains geometry/contour differences, so these functional passes are not full quality acceptance.
- `GpuLiveEyeGazeR2`: all seven outfits of each of the three characters (21 outfits), each exercises nine parameter updates and final combination. All pass valid=1/exit=0; comparison in `LiveEyeGazeComparisonR2`.
- R7 adds support for one-dimensional/constant UVs and absent optional highlight channels, with a non-affine source rejection rather than silent distortion. `GpuEyeGazeBuildR7.log` succeeds; `GpuEyeGazeFinalR1` reruns nine visible Alice states on the final implementation and passes the reader gates.
- `GpuEyeGazeAffineUnitR1.log`: `OpenKoikatsu.Eye.GpuAffineUV` actually completed with Result={Success}, covering an independently specified rotation/shear/offset transform, a collapsed UV axis, constant/absent channels, mismatched stream lengths, and non-affine rejection. This standalone run supplies the unit-test result; the earlier queued-but-interrupted Automation request does not.

These are parallel functional runs, not frame-time/FPS measurements. No Development 120 FPS/8.33 ms or final scene acceptance is claimed.
