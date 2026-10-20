# instant-meshes (vendored)

Copied on 2026-10-10 from `E:\Paper\instant-meshes-rs` (Rust port of Instant Field-Aligned Meshes, BSD-3-Clause, see `LICENSE.txt`).

Local changes:

- Progress output goes through `say!`, which `set_verbose(false)` silences. `ok-extract` turns it off.
- FBX reading/writing sits behind the default `fbx` feature. `ok-extract` builds without it.
- `print!` progress fragments go through `say_raw!` for the same reason.
- `extract_faces`: a cycle walk stops after 64 steps. Without it a hole walk on some non-manifold input (crowd card C019) looped until a 32–64 GiB allocation failed. Holes of 7+ edges are never filled, so no filled face is lost.
- `subdivide`: stops after 16 × input vertices, clamped to 256K–1M. Long slivers against a short target edge otherwise requested a 16 GiB buffer (crowd card C033).
- `remesh` pre-subdivision length is floored at target edge / 4. Upstream `min(target / 2, average × 2)` split long triangles of finely detailed parts into millions of vertices (C033 took 105 s).
- `extract_graph` step 5: the snap / edge-removal loops stop after 64 inner and 16 outer rounds. They oscillated forever on the C064 jacket.
- `postprocess.rs` (new, on by default; `Options::fill_holes` / `Options::snap_boundary`, CLI `--no-fill-holes` / `--no-snap-boundary` restore the original):
  - Interior holes up to 128 corners are ear-clipped after extraction (the original fills only fewer than 7). Loops that follow the input boundary (sleeves, collars, hems) stay open; non-flat loops are skipped.
  - Output boundary vertices within 1.5 output edges of the input boundary snap onto it. Extracted boundaries otherwise sit up to several centimetres inside, which opens seams between parts.
  - Crowd card C000, remesher output: body interior holes 2 → 0, boundary retreat 16.65 mm → 0; skirt 35.56 mm → 0; swimsuit holes 19 → 2.
