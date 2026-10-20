# Selected-character GPU material preparation (R38–R39)

This implements generic original-material preparation for a selected character
list. It does not merge sections or substitute a simplified shader. Production
native CPU animation remains enabled; whole-character GPU ownership and 120 FPS
acceptance remain unfinished.

## Implementation

- `prepare_crowd_instanced_materials.ps1` accepts AnimationProfile, explicit
  gpu_animation_library + gpu_character selection identities, DetailProfiles
  (defaults FaceDetailR2 and NearDetailR6), and per-row gpu_material_characters.
  Missing requested assets and duplicate selection identities reject before
  launching the commandlet. Explicit libraries require explicit identities.
- The commandlet validates each library's actual Character against the expected
  runtime identity. It collects the explicitly requested extra character assets,
  then processes every outfit and every skeletal mesh material slot. Shared mesh
  references and material parents are deduplicated across the selected inputs.
- Generated copies retain parameter overrides, original texture references,
  static switches, blend mode, two-sided state and shading models. Only the
  InstancedSkinnedMesh usage flag is added. Original assets are not saved or
  overwritten. A fresh namespace is required; the consumer manifest is published
  only after shader compilation, validation and package saves complete.
- The preparation receipt reports covered character paths and mesh count. The
  wrapper rejects incomplete requested character coverage. Cook verifies source
  material/mesh package hashes and uses its own GpuPoseAudit shader archive.
- The visual harness now accepts GpuShardIndex, MaterialManifest and MaterialPatch,
  allowing cards outside the previous fixed first-ten fixture to be compared.

## Verified evidence

Selection: C015 and C000 from GpuShardArbitraryR30/selection-index.json.

- Editor module build passed.
- InstancedMaterials-SelectedNearR38.json: 6 character variant assets, 32 unique
  skeletal meshes, 175 material slots, 221 compiled material/parent copies;
  overrides preserved, source assets unchanged, sections_merged=false.
- InstancedMaterialPatch-SelectedNearR38.json: Development Cook/pak passed with
  221 prepared assets and independent GpuPoseAudit shader library.
- InstancedMaterials-IdentityRejectR39.rejection.json: intentionally wrong
  Library.Character identity rejected, nonzero exit, no generated asset directory
  and no published success manifest.
- GpuVisual-SelectedNearR39: formal CrowdRoom, selected pilot C015 (selection
  position 10), phase 0.37. Same-frame front/side/back CPU vs GPU capture passed.
  The GPU-only probe uses HairShellSkin0 with all 57 original material slots.
  Native GPU override components=0, GPU probe components=1. Visual inspection of
  the front comparison also completed. Mean absolute RGB error (0–255 scale)
  front=0.479590, side=0.484585, back=0.482844; P99 channel error=2 for all views.

The capture is **not a whole-character GPU comparison**: the other character
parts remain native. Near face/hair materials are prepared/cooked but their
GPU-only rendering has not been individually accepted. Static-only parts,
production clocks, bounds, morph handling, native history acceptance and native
CPU removal still require implementation/validation.

The visual run is a single-character, fixed-phase diagnostic with a 60 FPS cap,
not a throughput profile. It reports mean frame 17.670 ms, GT 2.087 ms, RT 2.120
ms, RHI 1.278 ms, GPU 1.620 ms. These values cannot represent the required 100
moving characters, cannot prove speedup, and do not meet/verify 120 FPS.

Source package hashes currently cover .uasset headers, not a complete recursive
texture/function/export-data dependency graph. Prepared parents and meshes are
validated as recorded; do not describe this as full dependency integrity proof.

## Reproduction

```powershell
Tools/prepare_crowd_instanced_materials.ps1 -Tag FreshMaterials -Cards 2 -SelectionFile Saved/Crowd/GpuShardArbitraryR30/selection-index.json
Tools/cook_crowd_instanced_material_patch.ps1 -Tag FreshMaterials -MaterialManifest Saved/Crowd/InstancedMaterials-FreshMaterials.json
Tools/test_crowd_gpu_visual_compare.ps1 -Tag FreshVisual -PilotIndex 10 -GpuShardIndex Saved/Crowd/GpuShardArbitraryR30/selection-index.json -MaterialManifest Saved/Crowd/InstancedMaterials-FreshMaterials.json -MaterialPatch Saved/Crowd/InstancedMaterialPatch-FreshMaterials_P.pak
```

Fresh tags are mandatory. Use Cards 100 with the actual complete player selection
and matching generated profile assets to prepare all selected cards. This is an
offline preparation/Cook path, not a requirement to compile shaders on game entry.
The next ownership step is validating every near-detail GPU part using these
original materials, then implementing independent clocks/bounds/morph behavior
before removing native per-frame work and repeating the hundred-character profile.