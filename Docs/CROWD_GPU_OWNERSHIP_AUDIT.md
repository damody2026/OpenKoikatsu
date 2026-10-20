# Whole-character GPU ownership preflight (2026-10-05)

The native matrix bridge and GPU-only part probe do not constitute whole
character ownership. A general preflight now scans every registered visible
or hidden-shadow skeletal part, independent of character names or card IDs.
It checks current clip identity/duration, resident character coverage, private
bone/reference/inverse-bind compatibility, render data/LOD, original material
InstancedSkinnedMesh usage, active morph weights/curves and external morph sets,
cloth and mesh deformers. Visible non-skeletal mesh parts are also blockers
until their ownership is explicitly integrated.

Enable `-CrowdIndependentGpuOwnershipAudit` for diagnostics. The formal test
helper exposes `-OwnershipAudit`; it rejects use with Benchmark or Profile
because the once-per-second palette validation is diagnostic work. Normal
game launch leaves the audit disabled. Each scene report includes structured
`independent_gpu_ownership_audit` data with per-character blockers and part
counts. The scan refreshes after runtime near-detail changes and OFF/ON.

The output explicitly lists unresolved production capabilities: independent
production clocks, GPU animated bounds and whole-actor GPU rendering. Material
usage flags are necessary, not proof of cooked shader availability. Clearing
asset blockers alone therefore cannot authorize CPU shutdown. No source asset,
material, animation frequency, quality setting or CPU evaluator is changed.

## R27 evidence

Standalone Development build succeeded. Formal first phase reports all 100
selected actors, 714 relevant skeletal parts, 48 private-palette-compatible
parts and zero parts whose original material slots all carry the required
InstancedSkinnedMesh usage. 90 actors lack resident character records in the
ten-card fixture. All 100 have at least one material usage blocker. These are
snapshot counts, not total distinct assets; near-detail changes alter counts.

After correcting per-LOD empty morph container detection, this snapshot has
no active morph-state blockers. This does not imply future expressions or
different cards never require GPU morph transfer. Preflight remains fail
closed for nonzero/nonfinite morph weights and nonempty external sets.

R26's initial morph count was an audit bug, not evidence that all 100 actors
have active morphs. The correction and other errors are recorded in
Docs/CROWD_GPU_POSE_ERRORS.md.

## Remaining integration order

1. Expand the offline export/cache pipeline from the ten-card fixture to
   arbitrary selected actors, retaining each actor's private data. Preserve
   per-card/on-demand loading rather than requiring a fixed monolithic 100-card
   cache. Measure resident memory and cold scene startup separately.
2. Prepare/cook original material shader permutations for GPU-only components,
   preserving every material slot and override. Runtime must verify the actual
   shader resources; default-material substitution is not acceptance.
3. Adopt independent production clocks, conservative animated bounds and
   original expression/section visibility state. GPU-only renderer must cover
   all relevant parts/shadow passes, with coherent detail/outfit/F8 handoff.
4. Only then disable native evaluator/dynamic updates for a fully owned actor;
   use separate native reference actors for correctness tests. Unsupported
   actors retain their complete native path.
5. Compare images and matrix history, then run clean uncapped timing without
   audit/readback/trace. This preflight is not an FPS optimization and does not
   satisfy Development 120 FPS or complete GPU ownership.

R27 OFF/ON/OFF functional harness completed successfully. Structured reports
in all three phases contain exactly 100 unique characters, valid bounded part
counts, zero CPU-owned shutdowns and explicit blockers. GPU ON compared
1323555 matrices including 3972 native previous matrices, zero mismatches,
max error 0.000198364258. GPU-only probe compared 14022 previous matrices with
zero mismatches. This verifies audit integration preserves existing matrix
behavior; it is not complete visual or throughput acceptance. Launcher
ValidateOnly also passed. Binary SHA256:
75770364CFFF2EE997D2CD73C357A5A88B392D3A310B9F8AA6713EA3BBEC9F2F.
