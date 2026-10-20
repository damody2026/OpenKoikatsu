use anyhow::{Context, Result, bail, ensure};
use base64::{Engine, engine::general_purpose::STANDARD};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet, VecDeque},
    io::Cursor,
    path::Path,
};

pub const VIEWS: [&str; 3] = ["front", "side", "back"];
#[derive(Clone, Deserialize, Serialize)]
pub struct Mesh {
    pub role: String,
    pub vertices: Vec<[f64; 3]>,
    pub triangles: Vec<[usize; 3]>,
}
#[derive(Clone, Deserialize, Serialize)]
pub struct Geometry {
    pub meshes: Vec<Mesh>,
}
impl Geometry {
    pub fn parse(v: Value) -> Result<Self> {
        let g: Self = serde_json::from_value(v)?;
        ensure!(!g.meshes.is_empty(), "No body geometry");
        for m in &g.meshes {
            ensure!(
                !m.vertices.is_empty() && !m.triangles.is_empty(),
                "Empty posed mesh"
            );
            ensure!(
                m.vertices.iter().flatten().all(|x| x.is_finite()),
                "Invalid posed vertices"
            );
            ensure!(
                m.triangles.iter().flatten().all(|&i| i < m.vertices.len()),
                "Invalid posed triangles"
            );
        }
        Ok(g)
    }
    pub fn bounds(&self) -> ([f64; 3], [f64; 3]) {
        let mut low = [f64::INFINITY; 3];
        let mut high = [f64::NEG_INFINITY; 3];
        for p in self.meshes.iter().flat_map(|m| &m.vertices) {
            for a in 0..3 {
                low[a] = low[a].min(p[a]);
                high[a] = high[a].max(p[a]);
            }
        }
        (low, high)
    }
}
#[derive(Clone, Serialize, Deserialize)]
pub struct Frame {
    pub width: u32,
    pub height: u32,
    pub scale: f64,
    pub center: [f64; 3],
    pub floor: f64,
    pub physical_height: f64,
    pub projection: String,
    #[serde(default = "right")]
    pub side: String,
    #[serde(default)]
    pub padding: f64,
}
fn right() -> String {
    "right".into()
}
impl Frame {
    pub fn new(g: &Geometry, width: u32, height: u32) -> Result<Self> {
        let (l, h) = g.bounds();
        let size: [f64; 3] = std::array::from_fn(|i| h[i] - l[i]);
        ensure!(size[2] > 0., "Geometry has zero height");
        Ok(Self {
            width,
            height,
            scale: (height as f64 * 0.9 / size[2])
                .min(width as f64 * 0.9 / size[0].max(size[1]).max(1e-9)),
            center: std::array::from_fn(|i| (l[i] + h[i]) / 2.),
            floor: l[2],
            physical_height: size[2],
            projection: "orthographic".into(),
            side: right(),
            padding: 0.05,
        })
    }
    pub fn validate(&self) -> Result<()> {
        ensure!(
            (64..=1024).contains(&self.width)
                && (64..=1024).contains(&self.height)
                && self.scale.is_finite()
                && self.scale > 0.
                && self.physical_height.is_finite()
                && self.physical_height > 0.
                && self.floor.is_finite()
                && self.center.iter().all(|x| x.is_finite())
                && self.projection == "orthographic"
                && matches!(self.side.as_str(), "left" | "right"),
            "Invalid projection frame"
        );
        Ok(())
    }
}
#[derive(Clone, Debug, PartialEq)]
pub struct Mask {
    pub width: u32,
    pub height: u32,
    pub pixels: Vec<u8>,
}
impl Mask {
    pub fn new(w: u32, h: u32) -> Self {
        Self {
            width: w,
            height: h,
            pixels: vec![0; (w * h) as usize],
        }
    }
    pub fn png(&self) -> Result<Vec<u8>> {
        let img = image::GrayImage::from_raw(
            self.width,
            self.height,
            self.pixels.iter().map(|v| v * 255).collect(),
        )
        .context("Invalid mask dimensions")?;
        let mut bytes = Cursor::new(Vec::new());
        image::DynamicImage::ImageLuma8(img).write_to(&mut bytes, image::ImageFormat::Png)?;
        Ok(bytes.into_inner())
    }
    pub fn content(&self) -> Result<Value> {
        Ok(json!({"type":"image","mimeType":"image/png","data":STANDARD.encode(self.png()?)}))
    }
    fn set(&mut self, x: i32, y: i32) {
        if x >= 0 && y >= 0 && x < self.width as i32 && y < self.height as i32 {
            self.pixels[y as usize * self.width as usize + x as usize] = 1;
        }
    }
}
fn triangle(mask: &mut Mask, points: [[f64; 2]; 3]) {
    // Integer endpoint scanline fill. One deterministic rule is shared by
    // reference generation, comparison and optimization; includes both ends.
    let p = points.map(|p| [p[0].floor() as i32, p[1].floor() as i32]);
    let y0 = p.iter().map(|p| p[1]).min().unwrap().max(0);
    let y1 = p
        .iter()
        .map(|p| p[1])
        .max()
        .unwrap()
        .min(mask.height as i32 - 1);
    for y in y0..=y1 {
        let mut xs = Vec::with_capacity(6);
        for (a, b) in [(p[0], p[1]), (p[1], p[2]), (p[2], p[0])] {
            if y < a[1].min(b[1]) || y > a[1].max(b[1]) {
                continue;
            }
            if a[1] == b[1] {
                xs.push(a[0] as f64);
                xs.push(b[0] as f64);
            } else {
                xs.push(
                    a[0] as f64 + (b[0] - a[0]) as f64 * (y - a[1]) as f64 / (b[1] - a[1]) as f64,
                );
            }
        }
        if !xs.is_empty() {
            let min = xs.iter().copied().fold(f64::INFINITY, f64::min).ceil() as i32;
            let max = xs.iter().copied().fold(f64::NEG_INFINITY, f64::max).floor() as i32;
            for x in min.max(0)..=max.min(mask.width as i32 - 1) {
                mask.set(x, y);
            }
        }
    }
}
pub fn rasterize(g: &Geometry, f: &Frame) -> Result<Vec<Mask>> {
    f.validate()?;
    let mut result = Vec::new();
    for view in 0..3 {
        let mut mask = Mask::new(f.width, f.height);
        for mesh in &g.meshes {
            let points: Vec<[f64; 2]> = mesh
                .vertices
                .iter()
                .map(|p| {
                    let x = match view {
                        0 => p[0] - f.center[0],
                        2 => f.center[0] - p[0],
                        _ => (p[1] - f.center[1]) * if f.side == "left" { -1. } else { 1. },
                    };
                    [
                        f.width as f64 / 2. + x * f.scale,
                        f.height as f64 * 0.95 - (p[2] - f.floor) * f.scale,
                    ]
                })
                .collect();
            ensure!(
                points.iter().all(|p| p[0] >= 0.
                    && p[0] < f.width as f64
                    && p[1] >= 0.
                    && p[1] < f.height as f64),
                "Geometry exceeds fixed projection frame; prepare a wider frame before fitting"
            );
            for t in &mesh.triangles {
                triangle(&mut mask, [points[t[0]], points[t[1]], points[t[2]]]);
            }
        }
        result.push(mask);
    }
    Ok(result)
}
pub fn read_mask(
    path: &Path,
    mode: &str,
    background: Option<[u8; 3]>,
    tolerance: f64,
) -> Result<Mask> {
    ensure!(
        path.is_absolute() && path.is_file(),
        "Reference must be an existing absolute local image path"
    );
    let (w, h) = image::image_dimensions(path)?;
    ensure!(
        w as u64 * h as u64 <= 16_777_216,
        "Reference image too large"
    );
    let rgba = image::ImageReader::open(path)?
        .with_guessed_format()?
        .decode()?
        .to_rgba8();
    let mut mask = Mask::new(w, h);
    match mode {
        "mask" => {
            for (i, p) in rgba.pixels().enumerate() {
                ensure!(
                    p.0[..3].iter().all(|&c| c == 0 || c == 255) && p[0] == p[1] && p[1] == p[2],
                    "Mask must be binary black/white; use alpha/background or provide a segmentation mask"
                );
                mask.pixels[i] = u8::from(p[0] > 127);
            }
        }
        "alpha" => {
            ensure!(
                rgba.pixels().any(|p| p[3] < 255),
                "Alpha input has no transparent background; supply a mask"
            );
            for (i, p) in rgba.pixels().enumerate() {
                mask.pixels[i] = u8::from(p[3] > 127);
            }
        }
        "background" => {
            let bg = background.context("background mode requires RGB background")?;
            let candidates: Vec<bool> = rgba
                .pixels()
                .map(|p| (0..3).all(|a| (p[a] as f64 - bg[a] as f64).abs() <= tolerance))
                .collect();
            let mut seen = vec![false; candidates.len()];
            let mut queue = VecDeque::new();
            for y in 0..h {
                for x in 0..w {
                    if x == 0 || y == 0 || x == w - 1 || y == h - 1 {
                        let i = (y * w + x) as usize;
                        if candidates[i] && !seen[i] {
                            seen[i] = true;
                            queue.push_back((x, y));
                        }
                    }
                }
            }
            while let Some((x, y)) = queue.pop_front() {
                for (xx, yy) in [
                    (x as i64 - 1, y as i64),
                    (x as i64 + 1, y as i64),
                    (x as i64, y as i64 - 1),
                    (x as i64, y as i64 + 1),
                ] {
                    if xx < 0 || yy < 0 || xx >= w as i64 || yy >= h as i64 {
                        continue;
                    }
                    let i = (yy * w as i64 + xx) as usize;
                    if candidates[i] && !seen[i] {
                        seen[i] = true;
                        queue.push_back((xx as u32, yy as u32));
                    }
                }
            }
            for (i, p) in rgba.pixels().enumerate() {
                mask.pixels[i] = u8::from(!seen[i] && p[3] > 127);
            }
        }
        _ => bail!("Unknown segmentation mode"),
    }
    let n = mask.pixels.iter().filter(|&&p| p != 0).count();
    ensure!(
        n > 0 && n as f64 / (w as f64 * h as f64) <= 0.9,
        "Reference segmentation is empty or fills almost the whole image"
    );
    Ok(mask)
}
pub fn align_masks(masks: &[Mask], f: &Frame, height: Option<f64>) -> Result<(Vec<Mask>, Value)> {
    let desired = height.unwrap_or(f.physical_height) * f.scale;
    let bottom = (f.height as f64 * 0.95).floor() as i32;
    let top = (f.height as f64 * 0.95 - desired).floor() as i32;
    let h = (bottom - top + 1).max(1);
    let mut result = Vec::new();
    let mut transforms = Vec::new();
    for m in masks {
        let mut bx = [m.width as i32, m.height as i32, -1, -1];
        for y in 0..m.height {
            for x in 0..m.width {
                if m.pixels[(y * m.width + x) as usize] != 0 {
                    bx[0] = bx[0].min(x as i32);
                    bx[1] = bx[1].min(y as i32);
                    bx[2] = bx[2].max(x as i32);
                    bx[3] = bx[3].max(y as i32);
                }
            }
        }
        ensure!(bx[2] >= bx[0], "Empty reference mask");
        let sw = bx[2] - bx[0] + 1;
        let sh = bx[3] - bx[1] + 1;
        let factor = h as f64 / sh as f64;
        let w = (sw as f64 * factor).round_ties_even().max(1.) as i32;
        let left = ((f.width as i32 - w + 1) as f64 / 2.).floor() as i32;
        ensure!(
            left >= 0 && top >= 0 && top + h <= f.height as i32 && left + w <= f.width as i32,
            "Reference exceeds frame; provide larger projection padding"
        );
        let mut out = Mask::new(f.width, f.height);
        for y in 0..h {
            for x in 0..w {
                let sx =
                    bx[0] + (((x as f64 + 0.5) * sw as f64 / w as f64).floor() as i32).min(sw - 1);
                let sy =
                    bx[1] + (((y as f64 + 0.5) * sh as f64 / h as f64).floor() as i32).min(sh - 1);
                out.pixels[((top + y) * f.width as i32 + left + x) as usize] =
                    m.pixels[(sy * m.width as i32 + sx) as usize];
            }
        }
        result.push(out);
        transforms.push(json!({"source_box":[bx[0],bx[1],bx[2]+1,bx[3]+1],"scale":factor,"translation":[left,top]}));
    }
    Ok((result, json!(transforms)))
}
pub fn metrics(actual: &[Mask], reference: &[Mask]) -> Result<Value> {
    ensure!(
        actual.len() == 3 && reference.len() == 3,
        "Three masks required"
    );
    let mut views = Vec::new();
    for (view, (a, b)) in VIEWS.iter().zip(actual.iter().zip(reference)) {
        ensure!(
            a.width == b.width && a.height == b.height,
            "Projection dimensions differ"
        );
        let mut inter = 0usize;
        let mut union = 0usize;
        for (&a, &b) in a.pixels.iter().zip(&b.pixels) {
            inter += usize::from(a != 0 && b != 0);
            union += usize::from(a != 0 || b != 0);
        }
        let iou = if union == 0 {
            1.
        } else {
            inter as f64 / union as f64
        };
        let mut errors = 0.;
        let mut count = 0.;
        for y in 0..a.height as usize {
            let offset = y * a.width as usize;
            let aa = &a.pixels[offset..offset + a.width as usize];
            let bb = &b.pixels[offset..offset + b.width as usize];
            match (
                aa.iter().position(|&p| p != 0),
                bb.iter().position(|&p| p != 0),
            ) {
                (Some(x), Some(z)) => {
                    let xx = aa.iter().rposition(|&p| p != 0).unwrap();
                    let zz = bb.iter().rposition(|&p| p != 0).unwrap();
                    errors += (x.abs_diff(z) + xx.abs_diff(zz)) as f64;
                    count += 2.;
                }
                (None, None) => {}
                _ => {
                    errors += 2. * a.width as f64;
                    count += 2.;
                }
            }
        }
        let boundary = if count == 0. {
            0.
        } else {
            errors / count / a.height as f64
        };
        views.push(json!({"view":view,"iou":iou,"mean_boundary_error_over_image_height":boundary,"loss":0.7*(1.-iou)+0.3*boundary}));
    }
    Ok(
        json!({"loss":views.iter().map(|v|v["loss"].as_f64().unwrap()).sum::<f64>()/3.,"views":views}),
    )
}
pub fn save_masks(folder: &Path, masks: &[Mask]) -> Result<()> {
    std::fs::create_dir_all(folder)?;
    for (view, mask) in VIEWS.iter().zip(masks) {
        std::fs::write(folder.join(format!("{view}.png")), mask.png()?)?;
    }
    Ok(())
}
pub fn sections(g: &Geometry, heights: &[f64]) -> Value {
    type Key = (i64, i64);
    let tolerance = 0.001;
    let mut results = Vec::new();
    for &height in heights {
        let mut nodes = BTreeMap::<Key, [f64; 2]>::new();
        let mut edges = BTreeSet::new();
        for m in g.meshes.iter().filter(|m| m.role == "body") {
            for face in &m.triangles {
                let t = face.map(|i| m.vertices[i]);
                if t.iter().map(|p| p[2]).fold(f64::INFINITY, f64::min) >= height
                    || t.iter().map(|p| p[2]).fold(f64::NEG_INFINITY, f64::max) <= height
                {
                    continue;
                }
                let mut points = Vec::new();
                for (a, b) in [(t[0], t[1]), (t[1], t[2]), (t[2], t[0])] {
                    if (a[2] <= height && height < b[2]) || (b[2] <= height && height < a[2]) {
                        let f = (height - a[2]) / (b[2] - a[2]);
                        points.push([a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f]);
                    }
                }
                if points.len() != 2 {
                    continue;
                }
                let keys: Vec<Key> = points
                    .iter()
                    .map(|p| {
                        (
                            (p[0] / tolerance).round_ties_even() as i64,
                            (p[1] / tolerance).round_ties_even() as i64,
                        )
                    })
                    .collect();
                if keys[0] == keys[1] {
                    continue;
                }
                nodes.insert(keys[0], points[0]);
                nodes.insert(keys[1], points[1]);
                edges.insert((keys[0].min(keys[1]), keys[0].max(keys[1])));
            }
        }
        let mut graph = BTreeMap::<Key, BTreeSet<Key>>::new();
        for (a, b) in edges {
            graph.entry(a).or_default().insert(b);
            graph.entry(b).or_default().insert(a);
        }
        let mut unvisited: BTreeSet<Key> = graph.keys().copied().collect();
        let mut loops = Vec::new();
        let mut invalid = 0;
        while let Some(&start) = unvisited.first() {
            let mut component = BTreeSet::new();
            let mut queue = vec![start];
            while let Some(n) = queue.pop() {
                if component.insert(n) {
                    queue.extend(graph[&n].iter().filter(|n| !component.contains(n)).copied());
                }
            }
            for n in &component {
                unvisited.remove(n);
            }
            if component.iter().any(|n| graph[n].len() != 2) {
                invalid += 1;
                continue;
            }
            let mut prev = None;
            let mut current = start;
            let mut ordered = Vec::new();
            let mut visited = BTreeSet::new();
            while visited.insert(current) {
                ordered.push(nodes[&current]);
                let next = *graph[&current].iter().find(|&&n| Some(n) != prev).unwrap();
                prev = Some(current);
                current = next;
            }
            if current != start {
                invalid += 1;
                continue;
            }
            let mut perimeter = 0.;
            let mut area = 0.;
            let mut low = [f64::INFINITY; 2];
            let mut high = [f64::NEG_INFINITY; 2];
            let mut center = [0.; 2];
            for i in 0..ordered.len() {
                let a = ordered[i];
                let b = ordered[(i + 1) % ordered.len()];
                perimeter += (a[0] - b[0]).hypot(a[1] - b[1]);
                area += a[0] * b[1] - a[1] * b[0];
                for k in 0..2 {
                    low[k] = low[k].min(a[k]);
                    high[k] = high[k].max(a[k]);
                    center[k] += a[k] / ordered.len() as f64;
                }
            }
            loops.push(json!({"perimeter":perimeter,"width":high[0]-low[0],"depth":high[1]-low[1],"area":area.abs()/2.,"center":center,"points":ordered}));
        }
        loops.sort_by(|a, b| {
            b["area"]
                .as_f64()
                .unwrap()
                .total_cmp(&a["area"].as_f64().unwrap())
        });
        results.push(json!({"height":height,"valid":!loops.is_empty()&&invalid==0,"closed_loops":loops,"invalid_components":invalid}));
    }
    json!({"unit":"centimeters","method":"posed_triangle_plane_intersection","weld_tolerance":tolerance,"sections":results})
}
