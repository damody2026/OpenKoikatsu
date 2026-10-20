# StreetSocial VSM investigation and acceptance plan

2026-10-07. Remaining functional defect: Runtime-BlendR2 emitted non-Nanite VSM marking queue overflow. Preserve visual fidelity, runtime APNG private characters, independent animations, near-character shadow policy and public hundred-person launcher.

1. Grok 4.7 fast traces scene lights, character shadow admission and GPU diagnostic bounds against local UE5.8 renderer source. Identify the specific cause with file/line evidence; distinguish hypotheses from proof.
2. Implement only a demonstrably necessary general correction; never increase queue capacity, disable shadows globally, or add per-card exceptions. If no safe correction follows from evidence, provide a precise reproduction/measurement recommendation instead of speculative changes.
3. Root reviews changed files and runs affected tests/builds. Record any failed assumptions in STREET_SOCIAL_ERRORS.md.
4. Repeat actual street runtime and report whether overflow persists, load time and average/P95 frame time. Full manual visual/content acceptance remains distinct from automated functional evidence.

## Measured follow-up
Root enabled only test-time UE page-area diagnostics and added runtime component/bounds reporting. ShadowBoundsR1 shows selection-001 halfextent497.125cm expanding to909.913cm after a gesture; male256.096cm expanding to427.255cm. Actual high-page casters are UInstancedSkinnedMeshComponent belonging to StreetSocial. This supports replacing the streamed per-bone maximum envelope with the existing weighted per-vertex/per-axis conservative formula. Grok follow-up retains owned bind geometry for background computation, with containment tests; no heuristic shrinking or quality change. Bounds reduction alone does not yet prove the VSM warning disappears.
