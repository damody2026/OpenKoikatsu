//! Texture-aware remesh: bake each renderer's albedo into vertex colours.
//!
//! Follows JulianKnodt/texture_to_vert_colors. The surface is first refined
//! until every edge spans at most about one texel, every vertex takes the
//! colour under it, and a colour-aware quadric simplification (Garland &
//! Heckbert 1998, position + RGBA) then removes vertices where the colour
//! field is flat. Simplification only collapses half edges, so every output
//! vertex is a refined vertex and keeps its barycentric position on one
//! source triangle: skin weights, morphs and the other UV streams are
//! interpolated exactly, never re-guessed.
//!
//! The colour source is not `_MainTex`. Hair, items and clothes compose their
//! colour in the shader (colour masks, ramps, overlays), so Unreal bakes each
//! part's real material into its UV layout first (`ok.BakeCardAlbedo`), and
//! this module samples those images.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;

use crate::session::{OkSession, SessionPart};
use std::collections::{BinaryHeap, HashMap};
use std::path::Path;

/// Shader kind Unreal draws from COLOR0 (KKPreviewActor MakeVertexColorMaterial).
pub const VERTEX_COLOR_SHADER_KIND: u8 = 20;

#[derive(Clone, Debug)]
pub struct Options {
    /// Output triangles per source triangle.
    pub ratio: f32,
    /// Refine until each edge spans at most this many bake texels.
    pub edge_texels: f32,
    /// Refinement grows the threshold so one part stays below this.
    pub max_dense_triangles: usize,
    /// Never simplify a part below this many triangles.
    pub min_triangles: usize,
    /// Weight of RGBA against unit-diagonal geometry in the quadric.
    pub color_weight: f64,
    /// Keep at least this fraction of the refined triangles, so a small quad
    /// carrying a detailed alpha texture (a halo ring) keeps its outline.
    pub detail_fraction: f32,
}

impl Default for Options {
    fn default() -> Self {
        Self { ratio: 2.0, edge_texels: 1.0, max_dense_triangles: 400_000, min_triangles: 64, color_weight: 0.25, detail_fraction: 0.01 }
    }
}

// ---------------------------------------------------------------- bake image

/// One part's baked colour, in Unreal UV space (u, 1 - v) over `area`.
pub struct Bake {
    width: usize,
    height: usize,
    /// sRGB-encoded RGB and coverage, 0..1, already dilated.
    pixels: Vec<[f32; 4]>,
    area: [f32; 4],
}

impl Bake {
    fn pixel(&self, uv: [f32; 2]) -> [f32; 2] {
        let y = 1.0 - uv[1];
        [(uv[0] - self.area[0]) / (self.area[2] - self.area[0]) * self.width as f32,
         (y - self.area[1]) / (self.area[3] - self.area[1]) * self.height as f32]
    }

    fn texel_distance(&self, a: [f32; 2], b: [f32; 2]) -> f32 {
        let (p, q) = (self.pixel(a), self.pixel(b));
        ((p[0] - q[0]).powi(2) + (p[1] - q[1]).powi(2)).sqrt()
    }

    fn sample(&self, uv: [f32; 2]) -> [f32; 4] {
        let p = self.pixel(uv);
        let x = (p[0] - 0.5).clamp(0.0, (self.width - 1) as f32);
        let y = (p[1] - 0.5).clamp(0.0, (self.height - 1) as f32);
        let (x0, y0) = (x.floor() as usize, y.floor() as usize);
        let (x1, y1) = ((x0 + 1).min(self.width - 1), (y0 + 1).min(self.height - 1));
        let (fx, fy) = (x - x0 as f32, y - y0 as f32);
        let at = |x: usize, y: usize| self.pixels[y * self.width + x];
        let mut out = [0.0; 4];
        for c in 0..4 {
            let top = at(x0, y0)[c] * (1.0 - fx) + at(x1, y0)[c] * fx;
            let bottom = at(x0, y1)[c] * (1.0 - fx) + at(x1, y1)[c] * fx;
            out[c] = top * (1.0 - fy) + bottom * fy;
        }
        out
    }

    /// Coverage-only texture (white, alpha = coverage) and the `_MainTex_ST`
    /// that maps UV0 onto it. The outline shell clips with `_MainTex` alpha,
    /// so cut-out hair and items keep their outline shape. None if opaque.
    fn coverage_mask(&self) -> Option<(Vec<u8>, [f32; 4])> {
        if self.pixels.iter().all(|p| p[3] >= 0.999) { return None; }
        let (w, h) = (self.width.min(512), self.height.min(512));
        let mut img = image::RgbaImage::new(w as u32, h as u32);
        for (x, y, px) in img.enumerate_pixels_mut() {
            let (x0, x1) = (x as usize * self.width / w, ((x as usize + 1) * self.width / w).max(x as usize * self.width / w + 1));
            let (y0, y1) = (y as usize * self.height / h, ((y as usize + 1) * self.height / h).max(y as usize * self.height / h + 1));
            let mut sum = 0.0;
            for yy in y0..y1 { for xx in x0..x1 { sum += self.pixels[yy * self.width + xx][3]; } }
            *px = image::Rgba([255, 255, 255, (sum / ((x1 - x0) * (y1 - y0)) as f32 * 255.0).round() as u8]);
        }
        let mut png = Vec::new();
        image::DynamicImage::ImageRgba8(img).write_to(&mut std::io::Cursor::new(&mut png), image::ImageFormat::Png).ok()?;
        // Unity uv' = uv * st.xy + st.zw with image row 0 at uv'.y = 1, which
        // is the bake's row 0 at UE y = 1 - v = area.y0.
        let [x0, y0, x1, y1] = self.area;
        Some((png, [1.0 / (x1 - x0), 1.0 / (y1 - y0), -x0 / (x1 - x0), -(1.0 - y1) / (y1 - y0)]))
    }

    /// Decode a bake and fill the texels its UV triangles do not cover, so
    /// bilinear samples near chart borders and alpha holes keep the colour of
    /// the surface instead of the black capture background.
    pub fn load(png: &[u8], area: [f32; 4], uvs: &[[f32; 2]], indices: &[u32]) -> Result<Self, String> {
        let image = crate::teximg::load_from_memory(png).map_err(|e| e.to_string())?.to_rgba8();
        let (width, height) = (image.width() as usize, image.height() as usize);
        if width == 0 || height == 0 { return Err("empty bake".into()); }
        if !(area[2] > area[0] && area[3] > area[1]) { return Err("invalid bake area".into()); }
        let mut bake = Bake { width, height, area,
            pixels: image.pixels().map(|p| [p[0] as f32 / 255.0, p[1] as f32 / 255.0, p[2] as f32 / 255.0, p[3] as f32 / 255.0]).collect() };
        // Texels whose centre lies within half a texel of a UV triangle.
        let mut chart = vec![false; width * height];
        for t in indices.chunks_exact(3) {
            let Some(p) = t.iter().map(|&i| uvs.get(i as usize).map(|uv| bake.pixel(*uv))).collect::<Option<Vec<_>>>() else { continue };
            let area2 = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]);
            let edges: Vec<f32> = (0..3).map(|k| { let (a, b) = (p[k], p[(k + 1) % 3]); ((b[0] - a[0]).powi(2) + (b[1] - a[1]).powi(2)).sqrt().max(1e-6) }).collect();
            let x0 = (p.iter().map(|q| q[0]).fold(f32::MAX, f32::min) - 1.0).floor().max(0.0) as usize;
            let x1 = (p.iter().map(|q| q[0]).fold(f32::MIN, f32::max) + 1.0).ceil().min(width as f32 - 1.0).max(0.0) as usize;
            let y0 = (p.iter().map(|q| q[1]).fold(f32::MAX, f32::min) - 1.0).floor().max(0.0) as usize;
            let y1 = (p.iter().map(|q| q[1]).fold(f32::MIN, f32::max) + 1.0).ceil().min(height as f32 - 1.0).max(0.0) as usize;
            if x1.saturating_sub(x0) > 8192 || y1.saturating_sub(y0) > 8192 { continue; }
            let sign = if area2 < 0.0 { -1.0 } else { 1.0 };
            for y in y0..=y1 {
                for x in x0..=x1 {
                    let c = [x as f32 + 0.5, y as f32 + 0.5];
                    // Signed distance to each edge, positive inside.
                    let inside = (0..3).all(|k| {
                        let (a, b) = (p[k], p[(k + 1) % 3]);
                        sign * ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / edges[k] >= -0.5
                    });
                    if inside { chart[y * width + x] = true; }
                }
            }
        }
        // Multi-source BFS from covered chart texels copies the nearest colour.
        let mut owner: Vec<u32> = vec![u32::MAX; width * height];
        let mut queue = std::collections::VecDeque::new();
        for i in 0..width * height {
            if chart[i] && bake.pixels[i][3] > 0.0 { owner[i] = i as u32; queue.push_back(i); }
        }
        while let Some(i) = queue.pop_front() {
            let (x, y) = (i % width, i / width);
            let neighbours = [(x > 0).then(|| i - 1), (x + 1 < width).then(|| i + 1), (y > 0).then(|| i - width), (y + 1 < height).then(|| i + width)];
            for n in neighbours.into_iter().flatten() {
                if owner[n] == u32::MAX { owner[n] = owner[i]; queue.push_back(n); }
            }
        }
        let source = bake.pixels.clone();
        for i in 0..width * height {
            let o = owner[i];
            if o == u32::MAX || o as usize == i { continue; }
            let from = source[o as usize];
            let p = &mut bake.pixels[i];
            // Inside a chart the captured coverage is the material's own
            // opacity; outside it only the nearest surface matters.
            let alpha = if chart[i] { p[3] } else { from[3] };
            *p = [from[0], from[1], from[2], alpha];
        }
        Ok(bake)
    }
}

// ------------------------------------------------------------- refinement

/// Up to three source vertices and their weights; unused slots weigh 0.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Bary { pub v: [u32; 3], pub w: [f32; 3] }

impl Bary {
    fn vertex(i: u32) -> Self { Bary { v: [i, i, i], w: [1.0, 0.0, 0.0] } }
    fn mid(a: &Bary, b: &Bary) -> Bary {
        let mut items: Vec<(u32, f32)> = Vec::with_capacity(6);
        for (v, w) in a.v.iter().zip(a.w).chain(b.v.iter().zip(b.w)) {
            if w <= 0.0 { continue; }
            match items.iter_mut().find(|(x, _)| x == v) { Some(it) => it.1 += w * 0.5, None => items.push((*v, w * 0.5)) }
        }
        // Both ends lie on one source triangle; more than three is rounding.
        items.sort_by(|x, y| y.1.total_cmp(&x.1));
        items.truncate(3);
        let sum: f32 = items.iter().map(|x| x.1).sum();
        let mut out = Bary { v: [items[0].0; 3], w: [0.0; 3] };
        for (k, (v, w)) in items.into_iter().enumerate() { out.v[k] = v; out.w[k] = w / sum; }
        out
    }
}

/// Refined surface. Corner vertices carry an attribute identity (`av`); all
/// corners at one position share a geometric id (`gid`) so splits never open
/// cracks across UV seams.
struct Dense {
    bary: Vec<Bary>,
    gid: Vec<u32>,
    uv: Vec<[f32; 2]>,
    tris: Vec<[u32; 3]>,
    gids: u32,
}

fn key(a: u32, b: u32) -> (u32, u32) { if a < b { (a, b) } else { (b, a) } }

fn weld(pos: &[[f32; 3]]) -> (Vec<u32>, u32) {
    let mut map: HashMap<[u32; 3], u32> = HashMap::with_capacity(pos.len());
    let ids = pos.iter().map(|p| {
        let n = map.len() as u32;
        *map.entry([p[0].to_bits(), p[1].to_bits(), p[2].to_bits()]).or_insert(n)
    }).collect();
    let count = map.len() as u32;
    (ids, count)
}

fn refine(pos: &[[f32; 3]], uvs: &[[f32; 2]], indices: &[u32], bake: &Bake, opt: &Options) -> (Dense, f32) {
    let (gid, gids) = weld(pos);
    let mut d = Dense { bary: (0..pos.len() as u32).map(Bary::vertex).collect(), gid, uv: uvs.to_vec(), tris: Vec::new(), gids };
    for t in indices.chunks_exact(3) {
        let t = [t[0], t[1], t[2]];
        if t.iter().any(|&i| i as usize >= pos.len()) { continue; }
        let g = t.map(|i| d.gid[i as usize]);
        if g[0] == g[1] || g[1] == g[2] || g[0] == g[2] { continue; }
        d.tris.push(t);
    }
    // Each split adds two triangles per incident face; estimate the
    // refined count from texel area and coarsen the threshold if needed.
    let texel_area: f64 = d.tris.iter().map(|t| {
        let p = t.map(|i| bake.pixel(d.uv[i as usize]));
        (((p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1])).abs() * 0.5) as f64
    }).sum();
    let mut threshold = opt.edge_texels.max(0.25);
    let estimate = |t: f32| d.tris.len() as f64 + texel_area * 2.0 / (t as f64 * t as f64 * 0.5);
    while estimate(threshold) > opt.max_dense_triangles as f64 { threshold *= 1.25; }

    let mut edges: HashMap<(u32, u32), Vec<u32>> = HashMap::with_capacity(d.tris.len() * 2);
    for (ti, t) in d.tris.iter().enumerate() {
        for k in 0..3 { edges.entry(key(d.gid[t[k] as usize], d.gid[t[(k + 1) % 3] as usize])).or_default().push(ti as u32); }
    }
    let length = |d: &Dense, edges: &HashMap<(u32, u32), Vec<u32>>, e: (u32, u32)| -> f32 {
        let mut best = 0.0f32;
        for &t in edges.get(&e).map(Vec::as_slice).unwrap_or(&[]) {
            let tri = d.tris[t as usize];
            let corner = |g: u32| tri.iter().copied().find(|&v| d.gid[v as usize] == g);
            if let (Some(a), Some(b)) = (corner(e.0), corner(e.1)) {
                best = best.max(bake.texel_distance(d.uv[a as usize], d.uv[b as usize]));
            }
        }
        best
    };
    let mut heap: BinaryHeap<(u32, u32, u32)> = BinaryHeap::new();
    for &e in edges.keys() {
        let l = length(&d, &edges, e);
        if l > threshold { heap.push((l.to_bits(), e.0, e.1)); }
    }
    let cap = opt.max_dense_triangles * 2;
    while let Some((_, a, b)) = heap.pop() {
        if d.tris.len() >= cap { break; }
        let Some(list) = edges.remove(&(a, b)) else { continue };
        let gm = d.gids; d.gids += 1;
        let mut mids: Vec<((u32, u32), u32)> = Vec::new();
        let mut touched = vec![key(a, gm), key(gm, b)];
        for t in list {
            let tri = d.tris[t as usize];
            let Some(k) = (0..3).find(|&k| key(d.gid[tri[k] as usize], d.gid[tri[(k + 1) % 3] as usize]) == (a, b)) else { continue };
            let (vi, vj, vo) = (tri[k], tri[(k + 1) % 3], tri[(k + 2) % 3]);
            let pair = key(vi, vj);
            let vm = match mids.iter().find(|m| m.0 == pair) {
                Some(m) => m.1,
                None => {
                    let id = d.bary.len() as u32;
                    d.bary.push(Bary::mid(&d.bary[vi as usize], &d.bary[vj as usize]));
                    d.gid.push(gm);
                    let (p, q) = (d.uv[vi as usize], d.uv[vj as usize]);
                    d.uv.push([(p[0] + q[0]) * 0.5, (p[1] + q[1]) * 0.5]);
                    mids.push((pair, id));
                    id
                }
            };
            let (gi, gj, go) = (d.gid[vi as usize], d.gid[vj as usize], d.gid[vo as usize]);
            d.tris[t as usize] = [vi, vm, vo];
            let t2 = d.tris.len() as u32;
            d.tris.push([vm, vj, vo]);
            if let Some(list) = edges.get_mut(&key(gj, go)) { if let Some(s) = list.iter_mut().find(|x| **x == t) { *s = t2; } }
            edges.entry(key(gi, gm)).or_default().push(t);
            edges.entry(key(gm, gj)).or_default().push(t2);
            let side = edges.entry(key(gm, go)).or_default();
            side.push(t); side.push(t2);
            touched.push(key(gm, go));
        }
        for e in touched {
            let l = length(&d, &edges, e);
            if l > threshold { heap.push((l.to_bits(), e.0, e.1)); }
        }
    }
    (d, threshold)
}

// ------------------------------------------------------------ simplification

/// Symmetric 7x7 quadric over (x, y, z, r, g, b, a).
#[derive(Clone, Copy)]
struct Quadric { a: [f64; 28], b: [f64; 7], c: f64 }

impl Quadric {
    const ZERO: Quadric = Quadric { a: [0.0; 28], b: [0.0; 7], c: 0.0 };
    fn at(i: usize, j: usize) -> usize { let (i, j) = if i <= j { (i, j) } else { (j, i) }; i * 7 - i * (i + 1) / 2 + j }
    /// Squared distance to the plane through three 7D points, times weight.
    fn triangle(p: [[f64; 7]; 3], weight: f64) -> Option<Quadric> {
        let sub = |a: &[f64; 7], b: &[f64; 7]| { let mut o = [0.0; 7]; for k in 0..7 { o[k] = a[k] - b[k]; } o };
        let dot = |a: &[f64; 7], b: &[f64; 7]| (0..7).map(|k| a[k] * b[k]).sum::<f64>();
        let mut e1 = sub(&p[1], &p[0]);
        let l1 = dot(&e1, &e1).sqrt();
        if l1 < 1e-15 { return None; }
        for v in &mut e1 { *v /= l1; }
        let mut e2 = sub(&p[2], &p[0]);
        let proj = dot(&e1, &e2);
        for k in 0..7 { e2[k] -= proj * e1[k]; }
        let l2 = dot(&e2, &e2).sqrt();
        if l2 < 1e-15 { return None; }
        for v in &mut e2 { *v /= l2; }
        let mut q = Quadric::ZERO;
        for i in 0..7 { for j in i..7 {
            q.a[Self::at(i, j)] = weight * (if i == j { 1.0 } else { 0.0 } - e1[i] * e1[j] - e2[i] * e2[j]);
        } }
        let (pe1, pe2) = (dot(&p[0], &e1), dot(&p[0], &e2));
        for k in 0..7 { q.b[k] = weight * (pe1 * e1[k] + pe2 * e2[k] - p[0][k]); }
        q.c = weight * (dot(&p[0], &p[0]) - pe1 * pe1 - pe2 * pe2);
        Some(q)
    }
    fn add(&mut self, o: &Quadric) {
        for k in 0..28 { self.a[k] += o.a[k]; }
        for k in 0..7 { self.b[k] += o.b[k]; }
        self.c += o.c;
    }
    fn eval(&self, v: &[f64; 7]) -> f64 {
        let mut s = self.c;
        for i in 0..7 {
            s += 2.0 * self.b[i] * v[i] + self.a[Self::at(i, i)] * v[i] * v[i];
            for j in i + 1..7 { s += 2.0 * self.a[Self::at(i, j)] * v[i] * v[j]; }
        }
        s.max(0.0)
    }
}

struct Candidate { cost: f64, u: u32, v: u32 }
impl PartialEq for Candidate { fn eq(&self, o: &Self) -> bool { self.cost == o.cost } }
impl Eq for Candidate {}
impl PartialOrd for Candidate { fn partial_cmp(&self, o: &Self) -> Option<std::cmp::Ordering> { Some(self.cmp(o)) } }
impl Ord for Candidate { fn cmp(&self, o: &Self) -> std::cmp::Ordering { o.cost.total_cmp(&self.cost) } }

struct Simplifier<'a> {
    d: &'a Dense,
    pos: Vec<[f64; 3]>,
    color: &'a [[f32; 4]],
    weight: f64,
    tris: Vec<[u32; 3]>,
    alive: Vec<bool>,
    around: Vec<Vec<u32>>,
    quadric: Vec<Quadric>,
    live_tris: usize,
}

impl<'a> Simplifier<'a> {
    fn point(&self, g: u32, av: u32) -> [f64; 7] {
        let p = self.pos[g as usize];
        let c = self.color[av as usize];
        [p[0], p[1], p[2], c[0] as f64 * self.weight, c[1] as f64 * self.weight, c[2] as f64 * self.weight, c[3] as f64 * self.weight]
    }
    fn gid(&self, av: u32) -> u32 { self.d.gid[av as usize] }
    fn faces(&self, g: u32) -> Vec<u32> {
        let mut out: Vec<u32> = self.around[g as usize].iter().copied()
            .filter(|&t| self.alive[t as usize] && self.tris[t as usize].iter().any(|&v| self.gid(v) == g)).collect();
        out.sort_unstable(); out.dedup(); out
    }
    fn normal(&self, p: [[f64; 3]; 3]) -> [f64; 3] {
        let (a, b) = ([p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]], [p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]]);
        [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    }
    fn on_boundary(&self, g: u32, faces: &[u32]) -> bool {
        let mut count: HashMap<u32, u32> = HashMap::new();
        for &t in faces { for &v in &self.tris[t as usize] { let w = self.gid(v); if w != g { *count.entry(w).or_default() += 1; } } }
        count.values().any(|&c| c == 1)
    }

    /// Cost of moving `u` onto `v` and the attribute map, or None if illegal.
    fn evaluate(&self, u: u32, v: u32) -> Option<(f64, Vec<(u32, u32)>)> {
        let fu = self.faces(u);
        let fv = self.faces(v);
        let shared: Vec<u32> = fu.iter().copied().filter(|t| fv.binary_search(t).is_ok()).collect();
        if shared.is_empty() || shared.len() > 2 { return None; }
        // Attribute map: the corner of u becomes the corner of v it shares a face with.
        let mut map: Vec<(u32, u32)> = Vec::new();
        for &t in &shared {
            let tri = self.tris[t as usize];
            let au = tri.iter().copied().find(|&x| self.gid(x) == u)?;
            let av = tri.iter().copied().find(|&x| self.gid(x) == v)?;
            match map.iter().find(|m| m.0 == au) { Some(m) if m.1 != av => return None, Some(_) => {}, None => map.push((au, av)) }
        }
        for &t in &fu { for &x in &self.tris[t as usize] { if self.gid(x) == u && !map.iter().any(|m| m.0 == x) { return None; } } }
        // Link condition: common neighbours are exactly the shared faces' apexes.
        let ring = |faces: &[u32], g: u32| { let mut r: Vec<u32> = faces.iter().flat_map(|&t| self.tris[t as usize].iter().map(|&x| self.gid(x))).filter(|&w| w != g).collect(); r.sort_unstable(); r.dedup(); r };
        let (ru, rv) = (ring(&fu, u), ring(&fv, v));
        let common = ru.iter().filter(|w| **w != v && rv.binary_search(w).is_ok()).count();
        if common != shared.len() { return None; }
        let bu = self.on_boundary(u, &fu);
        if bu && (shared.len() != 1 || !self.on_boundary(v, &fv)) { return None; }
        if !bu && shared.len() != 2 { return None; }
        // Faces that survive must not flip or collapse.
        let pv = self.pos[v as usize];
        for &t in &fu {
            if shared.binary_search(&t).is_ok() { continue; }
            let tri = self.tris[t as usize];
            let before = tri.map(|x| self.pos[self.gid(x) as usize]);
            let after = tri.map(|x| if self.gid(x) == u { pv } else { self.pos[self.gid(x) as usize] });
            let (n0, n1) = (self.normal(before), self.normal(after));
            let (l0, l1) = ((n0[0] * n0[0] + n0[1] * n0[1] + n0[2] * n0[2]).sqrt(), (n1[0] * n1[0] + n1[1] * n1[1] + n1[2] * n1[2]).sqrt());
            if l1 < 1e-14 || (n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2]) < 0.2 * l0 * l1 { return None; }
        }
        let mut cost = 0.0;
        for &(au, av) in &map { cost += self.quadric[au as usize].eval(&self.point(v, av)); }
        let pu = self.pos[u as usize];
        cost += 1e-12 * ((pu[0] - pv[0]).powi(2) + (pu[1] - pv[1]).powi(2) + (pu[2] - pv[2]).powi(2));
        Some((cost, map))
    }

    fn collapse(&mut self, u: u32, v: u32, map: &[(u32, u32)]) {
        for t in self.faces(u) {
            let gids = &self.d.gid;
            let tri = &mut self.tris[t as usize];
            if tri.iter().any(|&x| gids[x as usize] == v) { self.alive[t as usize] = false; self.live_tris -= 1; continue; }
            for x in tri.iter_mut() { if let Some(m) = map.iter().find(|m| m.0 == *x) { *x = m.1; } }
            self.around[v as usize].push(t);
        }
        for &(au, av) in map { let q = self.quadric[au as usize]; self.quadric[av as usize].add(&q); }
        self.around[u as usize].clear();
    }

    fn neighbours(&self, g: u32) -> Vec<u32> {
        let mut r: Vec<u32> = self.faces(g).iter().flat_map(|&t| self.tris[t as usize].iter().map(|&x| self.gid(x))).filter(|&w| w != g).collect();
        r.sort_unstable(); r.dedup(); r
    }
}

struct Simplified { bary: Vec<Bary>, colors: Vec<[f32; 4]>, indices: Vec<u32> }

fn simplify(d: &Dense, pos_src: &[[f32; 3]], color: &[[f32; 4]], target: usize, opt: &Options) -> Simplified {
    // Geometry of every gid, normalised to a unit bounding-box diagonal.
    let mut pos = vec![[0.0f64; 3]; d.gids as usize];
    for (av, b) in d.bary.iter().enumerate() {
        let mut p = [0.0f64; 3];
        for k in 0..3 { let s = pos_src[b.v[k] as usize]; for c in 0..3 { p[c] += s[c] as f64 * b.w[k] as f64; } }
        pos[d.gid[av] as usize] = p;
    }
    let (mut lo, mut hi) = ([f64::MAX; 3], [f64::MIN; 3]);
    for p in &pos { for c in 0..3 { lo[c] = lo[c].min(p[c]); hi[c] = hi[c].max(p[c]); } }
    let diag = ((hi[0] - lo[0]).powi(2) + (hi[1] - lo[1]).powi(2) + (hi[2] - lo[2]).powi(2)).sqrt().max(1e-9);
    for p in &mut pos { for c in 0..3 { p[c] = (p[c] - lo[c]) / diag; } }
    let mut s = Simplifier { d, pos, color, weight: opt.color_weight, tris: d.tris.clone(), alive: vec![true; d.tris.len()],
        around: vec![Vec::new(); d.gids as usize], quadric: vec![Quadric::ZERO; d.bary.len()], live_tris: d.tris.len() };
    for (t, tri) in d.tris.iter().enumerate() {
        for &x in tri { s.around[d.gid[x as usize] as usize].push(t as u32); }
        let pts = tri.map(|x| s.point(d.gid[x as usize], x));
        let g = tri.map(|x| s.pos[d.gid[x as usize] as usize]);
        let n = s.normal(g);
        let area = 0.5 * (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]).sqrt();
        if let Some(q) = Quadric::triangle(pts, area.max(1e-18)) { for &x in tri { s.quadric[x as usize].add(&q); } }
    }
    // Open borders (hems, strand edges) get a stiff plane through the edge,
    // perpendicular to its face, so collapses slide along them, never inward.
    let mut faces_on: HashMap<(u32, u32), (u32, usize)> = HashMap::new();
    for (t, tri) in d.tris.iter().enumerate() {
        for k in 0..3 {
            let e = key(d.gid[tri[k] as usize], d.gid[tri[(k + 1) % 3] as usize]);
            faces_on.entry(e).and_modify(|c| c.0 += 1).or_insert((1, t * 3 + k));
        }
    }
    for (_, (count, corner)) in faces_on {
        if count != 1 { continue; }
        let tri = d.tris[corner / 3];
        let (a, b) = (tri[corner % 3], tri[(corner % 3 + 1) % 3]);
        let g = tri.map(|x| s.pos[d.gid[x as usize] as usize]);
        let n = s.normal(g);
        let (p, q) = (s.pos[d.gid[a as usize] as usize], s.pos[d.gid[b as usize] as usize]);
        let e = [q[0] - p[0], q[1] - p[1], q[2] - p[2]];
        let mut m = [e[1] * n[2] - e[2] * n[1], e[2] * n[0] - e[0] * n[2], e[0] * n[1] - e[1] * n[0]];
        let l = (m[0] * m[0] + m[1] * m[1] + m[2] * m[2]).sqrt();
        if l < 1e-18 { continue; }
        for v in &mut m { *v /= l; }
        let w = 10.0 * (e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        let off = m[0] * p[0] + m[1] * p[1] + m[2] * p[2];
        let mut plane = Quadric::ZERO;
        for i in 0..3 { for j in i..3 { plane.a[Quadric::at(i, j)] = w * m[i] * m[j]; } plane.b[i] = -w * off * m[i]; }
        plane.c = w * off * off;
        s.quadric[a as usize].add(&plane);
        s.quadric[b as usize].add(&plane);
    }
    let mut heap = BinaryHeap::new();
    for t in &d.tris { for k in 0..3 {
        let (a, b) = (d.gid[t[k] as usize], d.gid[t[(k + 1) % 3] as usize]);
        for (u, v) in [(a, b), (b, a)] { if let Some((cost, _)) = s.evaluate(u, v) { heap.push(Candidate { cost, u, v }); } }
    } }
    let mut dead = vec![false; d.gids as usize];
    while s.live_tris > target {
        let Some(c) = heap.pop() else { break };
        if dead[c.u as usize] || dead[c.v as usize] { continue; }
        let Some((cost, map)) = s.evaluate(c.u, c.v) else { continue };
        // Costs only grow as quadrics merge; re-queue stale entries.
        if cost > c.cost * (1.0 + 1e-9) + 1e-18 { heap.push(Candidate { cost, u: c.u, v: c.v }); continue; }
        s.collapse(c.u, c.v, &map);
        dead[c.u as usize] = true;
        for w in s.neighbours(c.v) {
            for (u, v) in [(c.v, w), (w, c.v)] { if let Some((cost, _)) = s.evaluate(u, v) { heap.push(Candidate { cost, u, v }); } }
        }
    }
    let mut remap = vec![u32::MAX; d.bary.len()];
    let mut out = Simplified { bary: Vec::new(), colors: Vec::new(), indices: Vec::new() };
    for (t, tri) in s.tris.iter().enumerate() {
        if !s.alive[t] { continue; }
        for &x in tri {
            if remap[x as usize] == u32::MAX {
                remap[x as usize] = out.bary.len() as u32;
                out.bary.push(d.bary[x as usize]);
                out.colors.push(color[x as usize]);
            }
            out.indices.push(remap[x as usize]);
        }
    }
    out
}

// ------------------------------------------------------- attribute transfer

fn lerp<const N: usize>(src: &[[f32; N]], b: &[Bary]) -> Vec<[f32; N]> {
    b.iter().map(|b| { let mut o = [0.0; N]; for k in 0..3 { let s = src[b.v[k] as usize]; for c in 0..N { o[c] += s[c] * b.w[k]; } } o }).collect()
}

fn lerp_dirs(src: &[[f32; 3]], b: &[Bary]) -> Vec<[f32; 3]> {
    lerp(src, b).into_iter().map(|d| { let l = (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]).sqrt(); if l > 1e-12 { [d[0] / l, d[1] / l, d[2] / l] } else { [0.0, 1.0, 0.0] } }).collect()
}

fn lerp_tangents(src: &[[f32; 4]], b: &[Bary]) -> Vec<[f32; 4]> {
    b.iter().map(|b| {
        let mut o = [0.0f32; 3];
        for k in 0..3 { let s = src[b.v[k] as usize]; for c in 0..3 { o[c] += s[c] * b.w[k]; } }
        let l = (o[0] * o[0] + o[1] * o[1] + o[2] * o[2]).sqrt().max(1e-12);
        let dominant = (0..3).max_by(|x, y| b.w[*x].total_cmp(&b.w[*y])).unwrap();
        [o[0] / l, o[1] / l, o[2] / l, if src[b.v[dominant] as usize][3] < 0.0 { -1.0 } else { 1.0 }]
    }).collect()
}

fn lerp_skin(weights: &[[f32; 4]], bones: &[[u16; 4]], b: &[Bary]) -> (Vec<[f32; 4]>, Vec<[u16; 4]>) {
    b.iter().map(|b| {
        let mut acc: Vec<(u16, f32)> = Vec::with_capacity(12);
        for k in 0..3 {
            let (w, j) = (weights[b.v[k] as usize], bones[b.v[k] as usize]);
            for s in 0..4 {
                if w[s] <= 0.0 || b.w[k] <= 0.0 { continue; }
                match acc.iter_mut().find(|x| x.0 == j[s]) { Some(x) => x.1 += w[s] * b.w[k], None => acc.push((j[s], w[s] * b.w[k])) }
            }
        }
        acc.sort_by(|x, y| y.1.total_cmp(&x.1));
        acc.truncate(4);
        let sum: f32 = acc.iter().map(|x| x.1).sum();
        let (mut w, mut j) = ([0.0f32; 4], [0u16; 4]);
        if sum > 1e-8 { for (k, (bone, weight)) in acc.into_iter().enumerate() { j[k] = bone; w[k] = weight / sum; } }
        else { let s = b.v[0] as usize; w = weights[s]; j = bones[s]; }
        (w, j)
    }).unzip()
}

fn lerp_morph(deltas: &[(u32, [f32; 3])], sources: usize, b: &[Bary]) -> Vec<(u32, [f32; 3])> {
    let mut dense = vec![[0.0f32; 3]; sources];
    for (i, d) in deltas { if let Some(slot) = dense.get_mut(*i as usize) { *slot = *d; } }
    lerp(&dense, b).into_iter().enumerate()
        .filter(|(_, d)| d.iter().any(|v| v.abs() > 1e-7))
        .map(|(i, d)| (i as u32, d)).collect()
}

/// Interpolate a per-vertex stream, keep an absent one absent.
fn stream<const N: usize>(src: &[[f32; N]], sources: usize, b: &[Bary]) -> Result<Vec<[f32; N]>, String> {
    if src.is_empty() { Ok(Vec::new()) } else if src.len() == sources { Ok(lerp(src, b)) } else { Err(format!("stream has {} of {sources} vertices", src.len())) }
}

fn colors_u8(colors: &[[f32; 4]]) -> Vec<[u8; 4]> {
    colors.iter().map(|c| c.map(|v| (v.clamp(0.0, 1.0) * 255.0).round() as u8)).collect()
}

fn rewrite_part(part: &mut SessionPart, b: &[Bary], indices: Vec<u32>, colors: &[[f32; 4]]) -> Result<(), String> {
    let n = part.rest_positions.len();
    part.positions = if part.positions.len() == n { lerp(&part.positions, b) } else { lerp(&part.rest_positions, b) };
    part.rest_positions = lerp(&part.rest_positions, b);
    part.rest_normals = if part.rest_normals.len() == n { lerp_dirs(&part.rest_normals, b) } else { Vec::new() };
    part.normals = if part.normals.len() == n { lerp_dirs(&part.normals, b) } else { part.rest_normals.clone() };
    part.rest_tangents = if part.rest_tangents.len() == n { lerp_tangents(&part.rest_tangents, b) } else { Vec::new() };
    part.uvs = stream(&part.uvs, n, b)?;
    part.uv2 = stream(&part.uv2, n, b)?;
    part.uv3 = stream(&part.uv3, n, b)?;
    part.uv4 = stream(&part.uv4, n, b)?;
    if part.weights.len() == n && part.bone_idx.len() == n {
        (part.weights, part.bone_idx) = lerp_skin(&part.weights, &part.bone_idx, b);
    } else if !part.weights.is_empty() || !part.bone_idx.is_empty() {
        return Err("skin weights do not match vertices".into());
    }
    for (_, deltas) in &mut part.blend_shapes { *deltas = lerp_morph(deltas, n, b); }
    part.morph_idx = part.blend_shapes.iter().map(|(_, d)| d.iter().map(|x| x.0).collect()).collect();
    part.morph_dxyz = part.blend_shapes.iter().map(|(_, d)| d.iter().flat_map(|x| x.1).collect()).collect();
    part.vertex_colors = colors_u8(colors);
    part.indices = indices;
    Ok(())
}

fn rewrite_source(mesh: &mut crate::unity::ExtractedMesh, sources: usize, b: &[Bary], indices: &[u32], colors: &[[f32; 4]]) -> Result<(), String> {
    let base = if mesh.rest_positions.len() == sources { mesh.rest_positions.clone() } else { mesh.positions.clone() };
    if base.len() != sources { return Err("runtime source does not match the drawn vertices".into()); }
    mesh.positions = if mesh.positions.len() == sources { lerp(&mesh.positions, b) } else { lerp(&base, b) };
    mesh.rest_positions = if mesh.rest_positions.len() == sources { lerp(&mesh.rest_positions, b) } else { Vec::new() };
    mesh.normals = if mesh.normals.len() == sources { lerp_dirs(&mesh.normals, b) } else { Vec::new() };
    mesh.rest_normals = if mesh.rest_normals.len() == sources { lerp_dirs(&mesh.rest_normals, b) } else { Vec::new() };
    mesh.tangents = if mesh.tangents.len() == sources { lerp_tangents(&mesh.tangents, b) } else { Vec::new() };
    mesh.uvs = stream(&mesh.uvs, sources, b)?;
    mesh.uv2 = stream(&mesh.uv2, sources, b)?;
    mesh.uv3 = stream(&mesh.uv3, sources, b)?;
    mesh.uv4 = stream(&mesh.uv4, sources, b)?;
    if mesh.weights.len() == sources && mesh.bone_idx.len() == sources {
        (mesh.weights, mesh.bone_idx) = lerp_skin(&mesh.weights, &mesh.bone_idx, b);
    } else if !mesh.weights.is_empty() || !mesh.bone_idx.is_empty() {
        return Err("runtime skin weights do not match vertices".into());
    }
    for (_, deltas) in &mut mesh.blend_shapes { *deltas = lerp_morph(deltas, sources, b); }
    mesh.vertex_colors = colors_u8(colors);
    mesh.indices = indices.to_vec();
    if !mesh.submesh_ranges.is_empty() { mesh.submesh_ranges = vec![(0, indices.len(), 0)]; mesh.submesh_index = 0; }
    mesh.texture_png = None;
    mesh.color_mask_png = None;
    mesh.alpha_mask_png = None;
    mesh.detail_mask_png = None;
    mesh.line_mask_png = None;
    mesh.normal_map_png = None;
    mesh.normal_mask_png = None;
    mesh.hair_gloss_png = None;
    mesh.shader_extra_textures.clear();
    mesh.runtime_textures.clear();
    Ok(())
}

/// Draw a converted part from COLOR0 only; every texture input is dropped.
fn rewrite_material(part: &mut SessionPart) {
    part.shader_kind = VERTEX_COLOR_SHADER_KIND;
    part.png = crate::unity::solid_png([1.0; 4], 4, 4);
    part.main_tex_st = [1.0, 1.0, 0.0, 0.0];
    part.alpha_mask = None;
    part.hair_gloss = None;
    part.detail_mask = None;
    part.line_mask = None;
    part.normal_map = None;
    part.normal_detail = None;
    part.normal_mask = None;
    part.overtex_png = None;
    part.overtex2_png = None;
    part.item_draw = None;
    // The bake already holds the lit colour; the vertex-colour material only
    // adds a camera-light step, so keep its shadow tint half as strong.
    for c in 0..3 { part.shadow_color[c] = 0.5 * (part.shadow_color[c] + 1.0); }
    part.shader_textures.clear();
    part.runtime_textures.clear();
    if let Some(m) = part.source_material.as_mut() {
        m.shader = "OpenKoikatsu/VertexColor".into();
        m.tex_slots.clear();
        m.tex_wraps.clear();
        m.keywords.clear();
    }
}

/// Remesh one part in place from its bake.
fn convert_part(part: &mut SessionPart, bake: &Bake, opt: &Options) -> Result<serde_json::Value, String> {
    let started = std::time::Instant::now();
    let n = part.rest_positions.len();
    let runtime_source = part.runtime_accessory.as_ref().map(|r| r.source_mesh().clone());
    let geometry: Vec<[f32; 3]> = match &runtime_source {
        Some(s) if s.rest_positions.len() == n => s.rest_positions.clone(),
        Some(s) if s.positions.len() == n => s.positions.clone(),
        Some(_) => return Err("runtime source vertex count differs".into()),
        None => part.rest_positions.clone(),
    };
    if part.uvs.len() != n || n == 0 || part.indices.len() < 3 { return Err("part has no UV triangles".into()); }
    let source_tris = part.indices.len() / 3;
    let (dense, threshold) = refine(&geometry, &part.uvs, &part.indices, bake, opt);
    let colors: Vec<[f32; 4]> = dense.uv.iter().map(|uv| bake.sample(*uv)).collect();
    let target = ((source_tris as f32 * opt.ratio) as usize).max(opt.min_triangles)
        .max((dense.tris.len() as f32 * opt.detail_fraction) as usize).min(dense.tris.len());
    let out = simplify(&dense, &geometry, &colors, target, opt);
    if out.indices.is_empty() { return Err("simplification removed every triangle".into()); }
    if let Some(mut source) = runtime_source {
        rewrite_source(&mut source, n, &out.bary, &out.indices, &out.colors)?;
        part.runtime_accessory.as_mut().unwrap().replace_source(source);
    }
    rewrite_part(part, &out.bary, out.indices.clone(), &out.colors)?;
    rewrite_material(part);
    if let Some((mask, st)) = bake.coverage_mask() { part.png = Some(mask); part.main_tex_st = st; }
    Ok(serde_json::json!({
        "name": part.name, "role": part.role, "source_triangles": source_tris, "source_vertices": n,
        "dense_triangles": dense.tris.len(), "edge_texels": threshold, "triangles": out.indices.len() / 3,
        "vertices": out.bary.len(), "runtime_accessory": part.runtime_accessory.is_some(),
        "seconds": started.elapsed().as_secs_f64(),
    }))
}

// ------------------------------------------------------------- card level

#[derive(serde::Deserialize)]
struct BakeRow { index: usize, file: String, #[serde(rename = "box")] area: [f32; 4] }
#[derive(serde::Deserialize)]
struct BakeManifest { parts: Vec<BakeRow> }

/// Bake area that makes `Bake::pixel` read a texture through `_MainTex_ST`
/// (uv' = uv * st.xy + st.zw, image row 0 at uv'.y = 1).
fn main_tex_area(st: [f32; 4]) -> [f32; 4] {
    let (sx, sy) = (if st[0].abs() > 1e-6 { st[0] } else { 1.0 }, if st[1].abs() > 1e-6 { st[1] } else { 1.0 });
    let (x0, y0) = (-st[2] / sx, -(1.0 - sy - st[3]) / sy);
    [x0, y0, x0 + 1.0 / sx, y0 + 1.0 / sy]
}

/// Remesh every baked part of one loaded outfit.
pub fn convert_session(session: &mut OkSession, bakes: &Path, opt: &Options) -> Result<serde_json::Value, String> {
    let manifest: BakeManifest = serde_json::from_slice(&crate::gamefs::read(bakes.join("bake.json")).map_err(|e| format!("{}: {e}", bakes.display()))?)
        .map_err(|e| e.to_string())?;
    let mut rows = Vec::new();
    let mut kept = Vec::new();
    let mut jobs: HashMap<usize, Bake> = HashMap::new();
    let mut fallbacks = Vec::new();
    let mut hidden = Vec::new();
    for row in &manifest.parts {
        let Some(part) = session.parts.get(row.index) else { continue };
        let loaded = crate::gamefs::read(bakes.join(&row.file)).map_err(|e| e.to_string())
            .and_then(|png| Bake::load(&png, row.area, &part.uvs, &part.indices))
            .and_then(|b| if b.pixels.iter().any(|p| p[3] > 0.0) { Ok(b) } else {
                // A material that draws nothing on the flat sheet (native skin,
                // IBL) falls back to its own _MainTex, the extractor's albedo.
                fallbacks.push(serde_json::json!({"index": row.index, "name": part.name}));
                let png = part.png.as_ref().ok_or("empty bake and no _MainTex")?;
                // Skin under worn clothes is cut by the clothes-over mask.
                let masked = crate::lowpoly::clothes_over_mask(part).and_then(|m| crate::lowpoly::bake_alpha_mask(png, &m));
                Bake::load(masked.as_deref().unwrap_or(png), main_tex_area(part.main_tex_st), &part.uvs, &part.indices)
            });
        match loaded {
            // Neither the material nor its texture covers anything: the
            // renderer draws nothing, so the converted card hides it.
            Ok(b) if !b.pixels.iter().any(|p| p[3] > 0.0) => hidden.push(row.index),
            Ok(b) => { jobs.insert(row.index, b); }
            Err(e) => kept.push(serde_json::json!({"index": row.index, "name": part.name, "reason": e})),
        }
    }
    for (i, p) in session.parts.iter().enumerate() {
        if !jobs.contains_key(&i) && !manifest.parts.iter().any(|r| r.index == i) {
            kept.push(serde_json::json!({"index": i, "name": p.name, "reason": "no bake (kept textured)"}));
        }
    }
    let hidden: Vec<serde_json::Value> = hidden.into_iter().map(|i| {
        session.parts[i].render_visible = false;
        serde_json::json!({"index": i, "name": session.parts[i].name})
    }).collect();
    use rayon::prelude::*;
    let results: Vec<(usize, Result<serde_json::Value, String>)> = session.parts.par_iter_mut().enumerate()
        .filter_map(|(i, part)| jobs.get(&i).map(|bake| (i, convert_part(part, bake, opt)))).collect();
    for (i, r) in results {
        match r {
            Ok(mut row) => { row["index"] = serde_json::json!(i); rows.push(row); }
            Err(e) => kept.push(serde_json::json!({"index": i, "name": session.parts[i].name, "reason": e})),
        }
    }
    rows.sort_by_key(|r| r["index"].as_u64());
    session.skel_export = None;
    session.skin_all();
    let (src, out) = rows.iter().fold((0u64, 0u64), |a, r| (a.0 + r["source_triangles"].as_u64().unwrap_or(0), a.1 + r["triangles"].as_u64().unwrap_or(0)));
    Ok(serde_json::json!({"converted": rows, "main_tex_fallback": fallbacks, "hidden_transparent": hidden, "kept": kept, "source_triangles": src, "triangles": out}))
}

/// Write `output` from `input`: same card blocks, every outfit's renderers
/// replaced by their vertex-coloured remesh. `bakes(outfit)` names the bake
/// directory of each outfit; outfits without one keep their original parts.
pub fn convert_card(input: &Path, output: &Path, bakes: &dyn Fn(i32) -> Option<std::path::PathBuf>, opt: &Options)
    -> Result<serde_json::Value, String> {
    use std::io::Write;
    if output.ok_exists() { return Err("output already exists".into()); }
    let original = crate::gamefs::read(input).map_err(|e| e.to_string())?;
    let (_, range) = crate::portable::png_snapshot(&original)?;
    let range = range.ok_or("card has no portable chunk")?;
    let root = crate::portable_resources::root(input)?;
    let count = crate::portable::load(input)?.coordinate_count;
    let mut snapshots = Vec::new();
    let mut outfits = Vec::new();
    for index in 0..count {
        let mut session = crate::portable::load_outfit(input, index as i32)?;
        let report = match bakes(index as i32).filter(|d| d.join("bake.json").ok_is_file()) {
            Some(dir) => convert_session(&mut session, &dir, opt)?,
            None => serde_json::json!({"converted": [], "note": "no bake; outfit kept textured"}),
        };
        let info = crate::card::parse_card_bytes_mode(&original, index as i32, false)?;
        let mut references = Vec::new();
        let mut unique = std::collections::BTreeMap::new();
        let residue = session.split_preview_resources(&info, |part, field, kind, raw, _| {
            let id = crate::portable_resources::id(kind, &raw);
            if !root.join("assets").join(format!("{id}.okasset")).ok_exists() { unique.entry(id.clone()).or_insert(raw); }
            references.push(crate::portable_resources::ResourceRef { part, field: field.into(), kind: kind.into(), id });
            Ok(())
        })?;
        OkSession::audit_preview_residue(&residue)?;
        snapshots.push(crate::portable_resources::SplitSnapshot { schema: 1, residue, references, unique });
        eprintln!("outfit {index}: {} -> {} triangles", report["source_triangles"], report["triangles"]);
        outfits.push(report);
    }
    let (payload, pack) = crate::portable_outfits::pack(snapshots, &root)?;
    let mut chunk = b"okPv".to_vec();
    chunk.extend(&payload);
    let mut bytes = original[..range.start - 8].to_vec();
    bytes.extend((payload.len() as u32).to_be_bytes());
    bytes.extend(&chunk);
    bytes.extend(crate::portable::crc(&chunk).to_be_bytes());
    bytes.extend(&original[range.end + 4..]);
    if bytes.len() as u64 > crate::portable::MAX_BYTES { return Err("converted card exceeds the size limit".into()); }
    let mut file = std::fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e| e.to_string())?;
    file.write_all(&bytes).and_then(|_| file.sync_all()).map_err(|e| e.to_string())?;
    // Reload every outfit from the written card.
    let mut verified = Vec::new();
    for index in 0..count {
        let session = crate::portable::load_outfit(output, index as i32)?;
        let converted: Vec<&SessionPart> = session.parts.iter().filter(|p| p.shader_kind == VERTEX_COLOR_SHADER_KIND).collect();
        let bad = converted.iter()
            .filter(|p| p.vertex_colors.len() != p.rest_positions.len() || p.indices.iter().any(|&i| i as usize >= p.rest_positions.len())
                || p.positions.len() != p.rest_positions.len() || p.positions.iter().flatten().any(|v| !v.is_finite()))
            .map(|p| p.name.clone()).collect::<Vec<_>>();
        if !bad.is_empty() { return Err(format!("outfit {index} reloads with broken parts: {bad:?}")); }
        verified.push(serde_json::json!({"outfit": index, "vertex_color_parts": converted.len(), "parts": session.parts.len()}));
    }
    Ok(serde_json::json!({"input": input, "output": output, "bytes": bytes.len(), "pack": pack, "outfits": outfits, "reload": verified}))
}

pub fn probe(card: &Path, outfit: i32) -> Result<serde_json::Value, String> {
    let session = crate::portable::load_outfit(card, outfit)?;
    let rows: Vec<_> = session.parts.iter().enumerate().map(|(i, p)| serde_json::json!({
        "index": i, "name": p.name, "role": p.role, "visible": p.render_visible, "skel": p.skel_kind, "shader": p.shader_kind,
        "blend": p.blend, "verts": p.rest_positions.len(), "tris": p.indices.len() / 3, "vc": p.vertex_colors.len(),
        "runtime": p.runtime_accessory.is_some(), "blend_shapes": p.blend_shapes.len(),
    })).collect();
    Ok(serde_json::json!({"coordinate_count": session.coordinate_count, "parts": rows}))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn grid_bake(size: usize, f: impl Fn(f32, f32) -> [f32; 4]) -> Bake {
        let pixels = (0..size * size).map(|i| f(((i % size) as f32 + 0.5) / size as f32, ((i / size) as f32 + 0.5) / size as f32)).collect();
        Bake { width: size, height: size, pixels, area: [0.0, 0.0, 1.0, 1.0] }
    }

    fn quad() -> (Vec<[f32; 3]>, Vec<[f32; 2]>, Vec<u32>) {
        (vec![[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0]],
         vec![[0.0, 1.0], [1.0, 1.0], [1.0, 0.0], [0.0, 0.0]], vec![0, 1, 2, 0, 2, 3])
    }

    fn at(out: &Simplified, pos: &[[f32; 3]], x: f32, y: f32) -> Option<[f32; 4]> {
        for t in out.indices.chunks_exact(3) {
            let p: Vec<[f32; 3]> = t.iter().map(|&i| { let b = out.bary[i as usize]; let mut o = [0.0; 3]; for s in 0..3 { for c in 0..3 { o[c] += pos[b.v[s] as usize][c] * b.w[s]; } } o }).collect();
            let det = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]);
            if det.abs() < 1e-12 { continue; }
            let l1 = ((x - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (y - p[0][1])) / det;
            let l2 = ((p[1][0] - p[0][0]) * (y - p[0][1]) - (x - p[0][0]) * (p[1][1] - p[0][1])) / det;
            if l1 < -1e-5 || l2 < -1e-5 || l1 + l2 > 1.0 + 1e-5 { continue; }
            let mut c = [0.0; 4];
            for ch in 0..4 { c[ch] = out.colors[t[0] as usize][ch] * (1.0 - l1 - l2) + out.colors[t[1] as usize][ch] * l1 + out.colors[t[2] as usize][ch] * l2; }
            return Some(c);
        }
        None
    }

    #[test]
    fn refinement_reaches_texel_size_without_cracks() {
        let (pos, uv, idx) = quad();
        let bake = grid_bake(16, |_, _| [1.0; 4]);
        let (d, threshold) = refine(&pos, &uv, &idx, &bake, &Options::default());
        assert_eq!(threshold, 1.0);
        for t in &d.tris { for k in 0..3 {
            assert!(bake.texel_distance(d.uv[t[k] as usize], d.uv[t[(k + 1) % 3] as usize]) <= 1.0 + 1e-4);
        } }
        // Closed square: interior edges have two faces, border edges one.
        let mut count: HashMap<(u32, u32), u32> = HashMap::new();
        for t in &d.tris { for k in 0..3 { *count.entry(key(d.gid[t[k] as usize], d.gid[t[(k + 1) % 3] as usize])).or_default() += 1; } }
        assert!(count.values().all(|&c| c == 1 || c == 2));
        assert_eq!(count.values().filter(|&&c| c == 1).count(), 4 * 16);
    }

    #[test]
    fn flat_colour_collapses_and_a_colour_step_stays_sharp() {
        let (pos, uv, idx) = quad();
        let flat = grid_bake(32, |_, _| [0.5, 0.5, 0.5, 1.0]);
        let (d, _) = refine(&pos, &uv, &idx, &flat, &Options::default());
        let colors: Vec<_> = d.uv.iter().map(|uv| flat.sample(*uv)).collect();
        let out = simplify(&d, &pos, &colors, 2, &Options::default());
        assert!(out.indices.len() / 3 <= 8, "flat field kept {} triangles", out.indices.len() / 3);
        let step = grid_bake(32, |x, _| if x < 0.5 { [1.0, 0.0, 0.0, 1.0] } else { [0.0, 0.0, 1.0, 1.0] });
        let (d, _) = refine(&pos, &uv, &idx, &step, &Options::default());
        let colors: Vec<_> = d.uv.iter().map(|uv| step.sample(*uv)).collect();
        let out = simplify(&d, &pos, &colors, 60, &Options::default());
        assert!(out.indices.len() / 3 <= 60);
        let area: f32 = out.indices.chunks_exact(3).map(|t| {
            let p: Vec<[f32; 3]> = t.iter().map(|&i| { let b = out.bary[i as usize]; let mut o = [0.0; 3]; for s in 0..3 { for c in 0..3 { o[c] += pos[b.v[s] as usize][c] * b.w[s]; } } o }).collect();
            0.5 * ((p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1])).abs()
        }).sum();
        assert!((area - 1.0).abs() < 1e-3, "simplified square covers {area} with {} triangles", out.indices.len() / 3);
        let mut worst = 0.0f32;
        for k in 0..200 {
            let x = (k as f32 + 0.5) / 200.0;
            if (x - 0.5).abs() < 0.08 { continue; }
            let c = at(&out, &pos, x, 0.37).expect("point covered");
            let want = if x < 0.5 { [1.0, 0.0, 0.0] } else { [0.0, 0.0, 1.0] };
            worst = worst.max((0..3).map(|ch| (c[ch] - want[ch]).abs()).fold(0.0, f32::max));
        }
        assert!(worst < 0.1, "colour step blurred by {worst}");
    }

    #[test]
    fn skin_and_morphs_interpolate_and_normalise() {
        let b = [Bary { v: [0, 1, 2], w: [0.5, 0.25, 0.25] }];
        let (w, j) = lerp_skin(&[[1.0, 0.0, 0.0, 0.0], [0.5, 0.5, 0.0, 0.0], [1.0, 0.0, 0.0, 0.0]], &[[3, 0, 0, 0], [3, 7, 0, 0], [9, 0, 0, 0]], &b);
        assert!((w[0].iter().sum::<f32>() - 1.0).abs() < 1e-6);
        assert_eq!(j[0][0], 3);
        assert!((w[0][0] - 0.625).abs() < 1e-6);
        let m = lerp_morph(&[(1, [4.0, 0.0, 0.0])], 3, &b);
        assert_eq!(m, vec![(0, [1.0, 0.0, 0.0])]);
    }

    #[test]
    fn bake_dilation_fills_outside_charts() {
        let mut png = Vec::new();
        let mut img = image::RgbaImage::new(8, 8);
        for (x, _, p) in img.enumerate_pixels_mut() { *p = if x < 4 { image::Rgba([255, 0, 0, 255]) } else { image::Rgba([0, 0, 0, 0]) }; }
        image::DynamicImage::ImageRgba8(img).write_to(&mut std::io::Cursor::new(&mut png), image::ImageFormat::Png).unwrap();
        // One UV triangle covering only the left half.
        let uvs = [[0.0, 1.0], [0.5, 1.0], [0.0, 0.0]];
        let bake = Bake::load(&png, [0.0, 0.0, 1.0, 1.0], &uvs, &[0, 1, 2]).unwrap();
        let right = bake.pixels[7 * 8 + 7];
        assert_eq!(right, [1.0, 0.0, 0.0, 1.0]);
    }
}
