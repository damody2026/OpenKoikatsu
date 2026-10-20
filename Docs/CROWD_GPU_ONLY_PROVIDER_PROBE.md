# Independent GPU-only transform provider — 2026-10-05

## Implemented milestone

UOKIndependentTransformProvider implements a private UTransformProviderData,
reports IsGpuOnly=true and owns a fresh GUID per object. Each mesh instance thus
has an isolated skeleton-batch provider key; independent private rigs are not
silently combined by matching source Skeleton GUID. One concurrent pose slot per
mesh is independent of its available clip count; the resident GPU data supports
owned ClipA/ClipB/time/blend parameters without sharing between actors.

The provider's FTransformProviderRenderProxy registers the actual native
FInstancedSkinningSceneExtensionProxy on the render thread. Skeleton batch callbacks
have empty primitive/proxy arrays, so the adapter uses that owned proxy plus the
batch transform offsets. GPU pose and private inverse bindings feed actual renderer
current/previous slots. UInstancedSkinnedMeshComponent has no skeletal animation
instance/CPU pose evaluator and its component tick is disabled in this probe.

Opt-in diagnostic flag: -CrowdIndependentGpuProviderProbe. The launcher exposes
-ProviderProbe and requires -VerifyGpu; it rejects -Benchmark with this diagnostic.
Runtime OFF/ON follows the formal scene request controls, hiding/disabling the
probe on OFF. The main crowd stays on its existing native correctness path.

## Real scene proof

DevelopmentHost-IndependentProviderProbeR11 built successfully. GpuOnlyProviderR7
passed formal cooked CrowdRoom launch plus OFF/ON/OFF with 100 distinct, animated
cards and the current third-person/player controls. All windows end with 45 near
high-face and 45 shadow-active residents.

ON probe: one actual GPU-only component, CPU pose evaluator absent/tick disabled,
32,718 actual renderer-matrix comparisons including 14,022 previous matrices,
maximum scalar error 0.0000152587890625 and zero mismatches (tolerance 0.001).
This includes no per-card constants or rig replacement. The selected fixture
contains ten actual private rigs; the probe chooses a matching part by manifest.
OFF returns the main crowd to zero active bridge components; the probe is hidden.

The source CPU component is retained as a diagnostic oracle, with render-upload
suppression bypassed only for that reference. GPU batch and native primitive phases
differ; diagnostic GPU submission is aligned to the native callback in the same
graph/frame. A graph-owned context is never dereferenced in another frame. Every
diagnostic frame retains native current matrices to validate independent previous
GPU history even when native UpdateMode=None. This reference copying exists only
for comparison and does not add an animation evaluator to the GPU-only component.

Evidence: Saved/Crowd/GpuAnimationControl-GpuOnlyProviderR7.json and -1/-2/-3.json,
GpuAnimationControl-GpuOnlyProviderR7.log.
Binary SHA256: C25DB8920A6C616EE18C27C4AEBB8671E84566CFE0227A03BE53C086393F1E4E.

## Scope and next steps

This is a single diagnostic mesh instance, using a neutral diagnostic material.
Original materials/assets are untouched; the probe is not used by normal gameplay.
All 100 original native CPU pose evaluators remain as correctness references.
No claim of full crowd CPU offload, appearance acceptance or 120 FPS is made.
The diagnostic's native reference lifetime is the retained scene-owned source;
interactive source replacement/rebinding remains a production integration check.

Before adopting this path for formal characters, prepare instanced shader usage
and matching Cook PSOs for original materials, validate private low/near palettes,
provide conservative clip bounds, retain CPU consumers for required sockets/morph/
interaction, and implement supported component ownership with seamless fallback.
Then remove supported CPU components and profile the formal 100-card scene without
diagnostic copying/readback, retaining mean/P95 plus Game/Render/RHI/GPU means.
Full visual, loop, world-motion-vector and load/120 FPS acceptance remains open.

## Functional-run frame times (not throughput acceptance)

This run is capped at 60 FPS and includes native reference copying and GPU readback.
It is functional evidence, not a performance comparison or a 120 FPS test.

| Mode | Frame mean ms | Frame P95 ms | Game mean ms | Render mean ms | RHI mean ms | GPU mean ms |
|---|---:|---:|---:|---:|---:|---:|
| OFF before | 18.230 | 25.452 | 12.005 | 17.474 | 4.083 | 1.800 |
| ON diagnostic | 21.210 | 30.883 | 14.211 | 21.200 | 4.778 | 1.973 |
| OFF after | 18.956 | 24.440 | 12.630 | 18.658 | 4.284 | 1.829 |

120 FPS requires 8.333 ms; it is not delivered by this intermediate implementation.
