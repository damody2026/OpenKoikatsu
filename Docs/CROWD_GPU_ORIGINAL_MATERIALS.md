# Independent GPU animation: original material preparation

This checkpoint adds an opt-in, card-independent material preparation and Cook
path for the native GPU-only instanced-skinned provider. It does not merge sections,
change geometry, lower texture resolution, change animation frequency, or replace
the normal character path. Full production GPU crowd adoption remains open.

## Preparation and correctness

`Tools/prepare_crowd_instanced_materials.ps1` uses the isolated GpuPoseAuditEditor.
It reads selected character animation-library assets, visits their skeletal parts
and all referenced outfits, and recursively duplicates saved materials and constant
instances into a fresh generated namespace. One source material is copied once
within a generation; parent copies are reused. Texture/function references and
parameter overrides are retained. No original mesh or material is saved.

The only allowed serialized state difference is the InstancedSkinnedMesh usage
bit inherited from a prepared parent. Scalar/vector/texture/font/static parameters,
all other base overrides, blend mode, two-sided and shading-model state are checked.
The final material interface must support the usage; checking only the base parent
would miss an instance that disables it. Shader resources must compile successfully.
The receipt contains source package SHA1, source/prepared paths and every mesh slot.
An existing generation is rejected instead of overwritten. Failed generations
have no consumer receipt and must not be adopted.

`InstancedMaterials-OriginalMaterialsR4.json` passed for two selected real cards:
10 low mesh parts, 157 slots, 249 prepared materials/parent copies. Generated source
packages total 4,244,220 bytes. These assets were compiled in Editor; this is not
proof of appearance or frame-time improvement.

## Cook and runtime

`Tools/cook_crowd_instanced_material_patch.ps1` checks source material/mesh package
hashes before cooking the generated directory through a verified isolated Content
mount. It refreshes an out-of-date isolated Editor BuildId, checks the source shader
snapshot, and requires exactly the expected generated package count. The patch
contains the generated packages and a separately named GpuPoseAudit shader library.
It does not replace the game's shader library with a two-card subset.

`InstancedMaterialPatch-OriginalMaterialsR3.json` passed: 249 prepared packages,
1,405 unique material shaders, patch 16,943,826 bytes. The shader cache is compiled
and cooked; a pre-recorded stable PSO cache has not been generated yet.

Runtime opts in with `-CrowdIndependentGpuMaterials=<receipt>` and a trusted
`-CrowdIndependentGpuMaterialsIdentity=<SHA1>`, together with the existing provider
probe switches. The receipt is bounded to 16 MiB before reading, hashed once and
decoded from those same bytes. Only a matching low mesh's materials are loaded at
first probe creation. Every slot must match the native source identity and prepared
blend/two-sided/shading state; missing library/assets or invalid data retains the
original CPU character. Runtime dynamic overrides are rejected by exact source
identity rather than silently lost.

This remains a one-component diagnostic with all original CPU evaluators retained.
Near faces, hair LOD, section visibility, outlines and shadow ownership are not yet
transferred to GPU-only components. The diagnostic can therefore verify material
and matrix integration, but cannot serve as final visual/performance acceptance.

## Formal Development scene verification

`GpuAnimationControl-GpuOnlyOriginalMaterialsR1.json` passed OFF/ON/OFF with
100 distinct native characters animated at each endpoint and 45 near-face/shadow
characters retained. The GPU-only component applied 59 original prepared material
slots, had no CPU skeletal pose evaluator, and passed 32,718 matrix comparisons
including 14,022 previous matrices. Maximum error was 0.0000152587890625, with
zero mismatches. The log confirms the isolated shader library loaded; no missing
shader-map, default-material fallback or shader compilation errors were found.

This is a 60 FPS capped functional run with CPU references and diagnostic readback.
The ON mean was **20.233 ms**, P95 **34.365 ms**. It is not uncapped throughput
evidence and does not meet the 8.333 ms target. See raw phase reports for complete
Game/Render/RHI/GPU times; full adoption and final profiling remain outstanding.

| Phase | Frame mean ms | P95 ms | Game ms | Render ms | RHI ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
| OFF before | 18.853 | 29.314 | 10.738 | 15.145 | 3.867 | 1.974 |
| ON diagnostic | 20.233 | 34.365 | 13.166 | 20.218 | 4.735 | 2.102 |
| OFF after | 25.153 | 43.939 | 16.434 | 24.706 | 5.455 | 1.923 |

The two CPU controls differ by 33.42%, so even apart from the diagnostic/cap,
this run cannot establish a speedup. No performance benefit is claimed.

## Required remaining work

Complete near-part private palettes and runtime property/section propagation;
preserve outline and shadow semantics and conservative clip bounds; handle source
replacement and consumer-driven CPU fallback; switch supported original component
ownership and remove their CPU evaluators. Export production per-card clip catalogs
with source freshness, bounded clip streaming and independent clocks. Then profile
100 randomly selected cards in the formal scene with readback disabled and uncapped
CPU/GPU/CPU controls, reporting frame mean/P95 and Game/Render/RHI/GPU means. The
120 FPS target is 8.333 ms. No 120 FPS or sub-minute random cold-load claim is made.
