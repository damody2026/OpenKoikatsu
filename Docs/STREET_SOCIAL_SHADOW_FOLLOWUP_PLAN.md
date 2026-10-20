# Runtime shadow follow-up plan
1. Reproduce/attribute remaining Non-Nanite VSM marking overflow from existing SubmitR4 diagnostics and UE source.
2. Identify a semantics-preserving runtime fix. Preserve arbitrary APNG topology/material silhouette, private independent animation, near-only shadows, CPU/GPU toggle, face and fade. No per-card exceptions, bound clamp, quality/queue CVars, engine modifications or hardware tuning.
3. Implement narrowly scoped helper/runtime change with meaningful containment/lifecycle regression tests if justified. Otherwise report exact blocker/evidence; do not claim solved.
4. Root reviews all edits, builds Development/Editor, runs pure and actual five-NPC tests with shadow gate, profiles frame mean/P95 and updates public receipt only after success. Record failures in STREET_SOCIAL_ERRORS.md. Full visual/content acceptance remains later.
