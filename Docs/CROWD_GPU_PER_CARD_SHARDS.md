# Per-card GPU animation cache export (2026-10-05)

`Tools/export_crowd_gpu_shards.ps1` accepts a selection of 1–100 arbitrary
imported cards, exports each independently and publishes a selection index
only after all requested shards pass validation. It is an offline export
tool, not a runtime loader or full GPU ownership implementation.

Each shard preserves its private skeleton, reference identities, tracks,
inverse binds and palettes. Source card/animation asset data (.uasset, .uexp,
.ubulk) and GPU exporter/plugin/shader source hashes define the reuse key.
Receipts also hash the generated cache, manifest and audit report. Changed or
missing outputs are rejected; old evidence is preserved. Repeated valid input
reuses the artifacts instead of launching another Unreal commandlet.

The default profile is the existing `GameLowHairSafeR9/Walk` asset layout;
`-AnimationProfile` makes the profile configurable. For a different layout,
provide `gpu_animation_library` and `gpu_character` explicitly per selection
row. Original selected `character` and actual low-model `gpu_character` are
different identities and are both retained in the index. The commandlet
verifies the animation library's Character against the requested runtime
identity before export. No card-name whitelist is used.

Example from the repository root:

```powershell
Tools/export_crowd_gpu_shards.ps1 `
  -SelectionFile Saved/Crowd/GameLowRandomSelectionR1.json `
  -OutputDirectory Saved/Crowd/GpuCardLibrary
```

Initial build is performed once per invocation unless `-SkipBuild` is
explicitly selected. It requires a current compiled audit host. Normal reuse
of already validated entries does not invoke the compiler. The index uses
absolute artifact paths under Saved/Crowd, which must be retained together.

## R30/R31 validation

Test selection uses entries 10 and 99 of the formal 100-card selection:
C015 and C000, both outside the old first-ten fixture. Both single-card
exports pass six-phase GPU pose/history, skin palette and editable low-vertex
comparison, cache roundtrip/async load and manifest validation. Single-card
canonical selection is tested separately; the multi-actor corruption tests
are not falsely reported as executed for a one-card shard.

| Card | Cache bytes | Pose max error | Vertex max error cm | Palette max error |
|---|---:|---:|---:|---:|
| C015 | 11,404,848 | 0.0000762939453 | 0.0000682810132 | 0.0000610351563 |
| C000 | 13,312,544 | 0.0000915527344 | 0.000137554918 | 0.0000610351563 |

Re-execution reused both shards. A deliberately wrong output hash in an
isolated receipt was rejected and no selection index was published; original
artifacts were unchanged. Evidence:
`Saved/Crowd/GpuShardArbitraryR30/selection-index.json`,
`Saved/Crowd/GpuShardArbitraryR30-final-reuse.log`, and
`Saved/Crowd/GpuShardIntegrityRejectR31/rejection.json`.

R29 failed at the newly added strict identity check because the selection's
original card path was incorrectly supplied as the low-profile identity.
R30 preserves and validates both identities. Error details remain in
Docs/CROWD_GPU_POSE_ERRORS.md.

## Limits and next work

This milestone validates two real cards, not all 100. The exporter currently
extracts the current walk library's first sequence; exporting multiple
available walks per card remains required for future 20-walk gameplay. The
runtime data representation supports independently owned multiple clips, but
this tool does not claim to export every clip yet.

The source fingerprint covers the imported card and selected library
directories. It is not an Unreal dependency-graph hash of arbitrary shared
assets outside those directories; importing a custom dependency layout needs
that extension before cache reuse is relied on for it.

Runtime loading/composition of the selected shards, GPU byte budgets,
resident memory, cooked material permutations and production clocks/bounds
are still required. `runtime_adopted=false` in the index is intentional.
Offline commandlet work may take minutes; it must remain outside the scene
entry path. No 60-second scene-load or Development 120 FPS result is claimed,
and no FPS benchmark was run for this offline change.
