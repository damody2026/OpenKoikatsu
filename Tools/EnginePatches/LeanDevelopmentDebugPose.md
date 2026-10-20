# Development GPU-skin debug pose copy

The patch applies to local UE 5.8 Engine/Private/SkeletalRenderGPUSkin.cpp.
Check/apply from the engine root with `git apply --check <absolute patch path>`
and `git apply <absolute patch path>`. On an already patched engine,
`git apply --reverse --check <absolute patch path>` verifies the patch is present.
Rebuild the target consuming Engine and stage that exact binary before testing.
No engine header, ABI or shader change is required. Re-audit on engine upgrades.

`r.SkeletalMesh.CopyDebugPose` defaults to 1. Setting it to 0 skips only the
Development GPU-skin component-space transform copy used for debug bone/physics
visualizations. GIsEditor always retains the original path. Normal current and
previous skinning matrices, private bone maps, inverse binds, morphs, cloth,
material sections and shadows retain their original engine paths. The optional
leader-mapped debug output is null only when debug copying is disabled.

When no debug data was copied, GetComponentSpaceTransforms returns nullptr so
debug-drawing consumers do not index an empty array. In game, restore the CVar
to 1 before using bone/constraint/collision debug overlays; data is repopulated
on the next normal mesh update. This option is not an animation/visual LOD.
Static, CPU-skin and Nanite render paths are unchanged.

After exact-scale-cache integration and two matching forward/reverse 100-card
comparisons (baseline drift 1.01% and 2.44%), the non-Editor crowd game scene
now defaults to CopyDebugPose=0. Render Thread means were approximately
10.36–10.62 ms without the copy, versus 15.37–15.78 ms with it. Current
near faces/shadows remained valid; this is not a 120 FPS result.
Use -CrowdKeepDebugPose in the game or -KeepDebugPose in the Development
launcher before using debug overlays. The Engine cvar default remains 1,
and authoring Editor keeps the original path. Comparison tests without an
explicit debug-copy case list keep the old copy behavior to isolate experiments.

The patch is independent of card IDs and character selection. Existing local
engine modifications are retained; it does not replace an engine source file.
The scene control uses `copy_debug_pose` (-1 unchanged, 0 skip, 1 original).
Reports verify requested and effective settings; missing Engine support is
rejected. Controlled A/B: `Tools\lua\lua.exe Tools\test_crowd_bone_packing.lua --modes 0,0,0
--copy-debug-pose-cases 1,0,1 --tag <fresh> --seconds 30`.
