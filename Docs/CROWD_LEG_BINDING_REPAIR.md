# Crowd low-model leg binding repair — 2026-10-04

## Result and scope

KK_372839/C014's skin bridge was fixed by UV-constrained authored binding transfer in R7. Continued investigation found six additional cards whose reduced surface fails the stretch test even though their original geometry passes under the same gameplay animation. R8 restores or rebuilds only their affected garment surfaces; all other R7 coverage/material/animation changes remain intact.

The test detects an edge initially between 0.1 and 8 cm becoming longer than 15 cm and three times its initial length, over 13 walking phases. Counts are diagnostic suspects, not visual proof that every clothing problem is resolved. Requested visual evidence uses a side camera (yaw90, target85cm, distance400cm), phase3/6 with legs apart.

| Card index | Affected part | R7 suspects | Final suspects | Treatment |
|---|---|---:|---:|---|
| C007 | Stockings | 12 | 0 | Two locally decimated source surfaces, topology-island binding hints |
| C039 | Bottom clothing | 2 | 0 | Original surface restored within whole-person budget |
| C087 | Bottom clothing | 2 | 0 | Original surface restored within whole-person budget |
| C090 | Bottom clothing | 2 | 0 | Original surface restored within whole-person budget |
| C094 | Shoes | 6 | 0 | Local decimation, island-preserving weld and binding hints |
| C098 | Bottom clothing | 2 | 0 | Original surface restored within whole-person budget |

C007 final whole person is29,897 triangles, C09429,900. Initial low materials remain256². High body assets are not added to initial gameplay loading. C014's R7 body is reused unchanged.

## Why simple re-binding was insufficient

Position-only matching can select a vertex on the other leg during the walking reference pose. UV constraints fix many cases, but stockings can have mirrored/overlapping UVs. Reduced topology and interpolated weights can still connect independently moving limbs.

Blender now carries the authored connected-island identity through decimation and returns an explicit original-corner binding index for each output corner. Weight transfer uses that index, retains full affine column groups and bounds the source distance to10cm. Welding is restricted to vertices of the same authored island. The unrelated part of the mesh is protected from re-binding. These are offline operations; no additional per-frame vertex work is introduced.

This path is enabled by default for Blender surface recovery. `-CrowdLegacyBindingTransfer` disables its hints only for diagnostic comparisons. The pre-existing no-hint exchange remains readable.

## Rejected attempts and safeguards

- C007's first Blender candidate worsened6 suspects to46 despite staying below30k. It was rejected without changing the Character asset.
- C094's first candidate requested a source binding24.154213cm away and failed the10cm guard. Restricting welds by authored island eliminated that problem; the guard was not relaxed.
- C007 regression after the weld change remains0 suspects (`ResidualBlenderC007NoWeldR10.json`), without another asset write.
- New body packages are `GameLowSurfaceSafeR8/BodyTopologySafeR1`. Before changing each derived Character reference, its original file was copied to Saved/Crowd. Original KKS/APNG, imported high meshes and R7 caches were not overwritten.
- Failed root builds encountered concurrent PaintSprayer edits; no paint code was modified for this task. Later full root builds succeeded after that independent source was completed. Details are in CROWD_GPU_POSE_ERRORS.md.

## Remaining classification

C022, C025, C030, C049 and C096 retain218 suspects in total. Their original imported high geometry also triggers suspects with the same gameplay clip, in the corresponding long clothing/skirt material slots. Reduction is therefore not the sole cause. This finding does not prove the original KKS game has the same visual defect or that these shapes are acceptable; animation/imported garment deformation needs a separate investigation. Stocking/skin intersections were explicitly postponed and are not counted as repaired by this change.

## Evidence

- `Saved/Crowd/ResidualBindingEvidenceR3.json`: high/low geometry × original/gameplay animation comparison for the eleven residual cards, material slots and native part roles.
- `Saved/Crowd/ResidualSurfaceRestore100R5.json`: full100 R8 derived cache preparation and bounded original-surface restoration.
- `Saved/Crowd/ResidualBlenderC007R7.json`, `ResidualBlenderC094R10.json`: accepted local topology repairs.
- `Saved/Crowd/LegSideOpenComparisonC014R3.png`, `LegSideOpenComparisonC007R9.png`: same-phase side/open-leg comparisons.
- `Saved/Crowd/AuditSurfaceNear100R10.json`: final100 uniqueness, low triangle/texture budget and near-detail hierarchy validation.

Animation bounds must match the final mesh paths; the C007 and C094 rows are regenerated from501 walk samples. Functional Development tests and appearance checks do not constitute120FPS acceptance.

Final Development verification: SurfaceTopologyMovement100R10 exits0, loaded/animated100, failed0, load6.822303s. Initial low-only, WASD and near-face round-trip pass. AuditSurfaceNear100R10 verifies maximum29,900 rendered triangles and256² source textures for100 unique cards. Final bounds file: Saved/Crowd/SectionBoundsSurface100R10.json. C007/C094 side/open-leg pilot runs both exit0. FPS target remains unaccepted. Saved logical size175.407GB, junctions skipped.
Interactive SurfaceTopologyPlay100R11 reopened and verified100 loaded/failed0, initial low-only and WASD player control. Idle player is excluded from animated count until moving.
