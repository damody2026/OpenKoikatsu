//! Batch remesher. This is the command-line pipeline of Instant Meshes.

use std::collections::HashSet;
use std::path::Path;
use std::time::Instant;

use crate::adjacency::{generate_adjacency_cotan, generate_adjacency_uniform, Adjacency, Link};
use crate::bvh::Bvh;
use crate::dedge::build_dedge;
use crate::dset::DisjointSets;
use crate::extract::{extract_faces, extract_graph};
use crate::field::{
    compute_orientation_singularities, optimize_orientations_hierarchical,
    optimize_positions_hierarchical,
};
use crate::hierarchy::{Hierarchy, Level};
use crate::math::{time_string, Vec3};
use crate::mesh::{Faces, OutputMesh, Surface};
use crate::meshio::{load_mesh, write_mesh};
use crate::meshstats::{compute_dual_vertex_areas, compute_mesh_stats};
use crate::normal::{generate_crease_normals, generate_smooth_normals};
use crate::subdivide::subdivide;

#[derive(Clone, Debug)]
pub struct Options {
    pub rosy: i32,
    pub posy: i32,
    pub scale: f32,
    pub face_count: i32,
    pub vertex_count: i32,
    pub crease_angle: f32,
    pub extrinsic: bool,
    pub align_to_boundaries: bool,
    pub smooth_iter: i32,
    pub knn_points: u32,
    pub pure_quad: bool,
    pub deterministic: bool,
    pub cotangent: bool,
    /// Fill interior holes the extractor leaves (it only fills fewer than 7
    /// edges); loops along the input boundary stay open. Not in the original.
    pub fill_holes: bool,
    /// Pull output boundary vertices onto the input boundary so parts meeting
    /// at a seam close up. Not in the original.
    pub snap_boundary: bool,
}

impl Default for Options {
    fn default() -> Self {
        Self {
            rosy: 4,
            posy: 4,
            scale: -1.0,
            face_count: -1,
            vertex_count: -1,
            crease_angle: -1.0,
            extrinsic: true,
            align_to_boundaries: false,
            smooth_iter: 2,
            knn_points: 10,
            pure_quad: true,
            deterministic: false,
            cotangent: false,
            fill_holes: true,
            snap_boundary: true,
        }
    }
}

pub fn remesh_file(input: &Path, output: &Path, options: &Options) -> Result<(), String> {
    let surface = load_mesh(input)?;
    let mesh = remesh(&surface, options)?;
    write_mesh(output, &mesh)
}

pub fn remesh(surface: &Surface, options: &Options) -> Result<OutputMesh, String> {
    if !(options.rosy == 2 || options.rosy == 4 || options.rosy == 6) {
        return Err("Rotation symmetry must be 2, 4, or 6".into());
    }
    if !(options.posy == 3 || options.posy == 4) {
        return Err("Position symmetry must be 4 or 6".into());
    }
    let posy_name = if options.posy == 3 { 6 } else { options.posy };
    crate::say!();
    crate::say!("Running in batch mode:");
    crate::say!("   Rotation symmetry type = {}", options.rosy);
    crate::say!("   Position symmetry type = {posy_name}");
    crate::say!(
        "   Crease angle threshold = {}",
        if options.crease_angle > 0.0 {
            options.crease_angle.to_string()
        } else {
            "disabled".into()
        }
    );
    crate::say!(
        "   Extrinsic mode         = {}",
        if options.extrinsic { "enabled" } else { "disabled" }
    );
    crate::say!(
        "   Align to boundaries    = {}",
        if options.align_to_boundaries { "yes" } else { "no" }
    );
    crate::say!(
        "   Fully deterministic    = {}",
        if options.deterministic { "yes" } else { "no" }
    );
    if options.posy == 4 {
        crate::say!(
            "   Output mode            = {}",
            if options.pure_quad {
                "pure quad mesh"
            } else {
                "quad-dominant mesh"
            }
        );
    }
    crate::say!();

    // Input boundary before subdivision, for hole filling and snapping.
    let input_boundary = if options.fill_holes || options.snap_boundary {
        crate::postprocess::input_boundary(&surface.faces, &surface.vertices)
    } else {
        Vec::new()
    };
    let mut faces = surface.faces.clone();
    let mut vertices = surface.vertices.clone();
    let mut normals = surface.normals.clone();
    let pointcloud = faces.cols() == 0;
    if pointcloud && normals.len() != vertices.len() {
        return Err("Point clouds need a normal per vertex (nx ny nz). Provide a triangle mesh, or an ASCII PLY is loaded as a mesh.".into());
    }

    let timer = Instant::now();
    let mut stats = compute_mesh_stats(&faces, &vertices);
    let mut scale = options.scale;
    let mut face_count = options.face_count;
    let mut vertex_count = options.vertex_count;

    let (adj, areas, e2e, crease_in, bvh_points) = if pointcloud {
        let bvh = Bvh::build_points(&vertices, &normals, stats.aabb);
        stats.average_edge_length = bvh.disk_radius as f64;
        let adj = adjacency_pointcloud(&mut vertices, &normals, &bvh, options.knn_points as usize);
        stats.surface_area =
            std::f64::consts::PI * stats.average_edge_length * stats.average_edge_length * 0.5 * vertices.len() as f64;
        let areas = vec![1.0; vertices.len()];
        (adj, areas, Vec::new(), HashSet::new(), Some(bvh))
    } else {
        if scale < 0.0 && vertex_count < 0 && face_count < 0 {
            crate::say!(
                "No target vertex count/face count/scale argument provided. Setting to the default of 1/16 * input vertex count."
            );
            vertex_count = (vertices.len() as i32 / 16).max(4);
        }
        resolve_scale(
            &mut scale,
            &mut face_count,
            &mut vertex_count,
            options.posy,
            stats.surface_area,
        )?;
        if stats.maximum_edge_length * 2.0 > scale as f64
            || stats.maximum_edge_length > stats.average_edge_length * 2.0
        {
            crate::say!(
                "Input mesh is too coarse for the desired output edge length (max input mesh edge length={}), subdividing ..",
                stats.maximum_edge_length
            );
            let mut dedge = build_dedge(&faces, vertices.len(), false)?;
            // Floor at a quarter of the target edge: finely detailed inputs
            // with a few long edges otherwise split each long triangle into
            // hundreds of thousands of pieces the field never needs.
            let max_length = (scale / 2.0).min((stats.average_edge_length as f32) * 2.0).max(scale / 4.0);
            subdivide(&mut faces, &mut vertices, &mut dedge, max_length);
        }
        let dedge = build_dedge(&faces, vertices.len(), false)?;
        let adj = if options.cotangent {
            generate_adjacency_cotan(&faces, &vertices, &dedge.v2e, &dedge.e2e, &dedge.non_manifold)
        } else {
            generate_adjacency_uniform(&faces, &dedge.v2e, &dedge.e2e, &dedge.non_manifold)
        };
        let (normals_out, crease) = if options.crease_angle >= 0.0 {
            generate_crease_normals(
                &faces,
                &vertices,
                &dedge.v2e,
                &dedge.e2e,
                &dedge.boundary,
                &dedge.non_manifold,
                options.crease_angle,
            )
        } else {
            (
                generate_smooth_normals(&faces, &vertices, &dedge.v2e, &dedge.e2e, &dedge.non_manifold),
                HashSet::new(),
            )
        };
        normals = normals_out;
        let areas = compute_dual_vertex_areas(&faces, &vertices, &dedge.v2e, &dedge.e2e, &dedge.non_manifold);
        (adj, areas, dedge.e2e, crease, None)
    };

    if !pointcloud && scale < 0.0 {
        resolve_scale(
            &mut scale,
            &mut face_count,
            &mut vertex_count,
            options.posy,
            stats.surface_area,
        )?;
    }
    if pointcloud && scale < 0.0 && vertex_count < 0 && face_count < 0 {
        vertex_count = (vertices.len() as i32 / 16).max(4);
        resolve_scale(
            &mut scale,
            &mut face_count,
            &mut vertex_count,
            options.posy,
            stats.surface_area,
        )?;
    } else if pointcloud {
        resolve_scale(
            &mut scale,
            &mut face_count,
            &mut vertex_count,
            options.posy,
            stats.surface_area,
        )?;
    }

    crate::say!("Output mesh goals (approximate)");
    crate::say!("   Vertex count           = {vertex_count}");
    crate::say!("   Face count             = {face_count}");
    crate::say!("   Edge length            = {scale}");
    if scale <= 0.0 || !scale.is_finite() {
        return Err("Could not derive a positive target edge length. Pass --scale, --faces, or --vertices.".into());
    }

    let mut hierarchy = Hierarchy::new();
    hierarchy.faces = if pointcloud { Faces::empty() } else { faces };
    hierarchy.e2e = e2e;
    hierarchy.scale = scale;
    hierarchy.levels.push(Level {
        v: vertices,
        n: normals,
        a: areas,
        q: Vec::new(),
        o: Vec::new(),
        cq: Vec::new(),
        co: Vec::new(),
        cqw: Vec::new(),
        cow: Vec::new(),
        adj,
        phases: Vec::new(),
    });
    hierarchy.build(options.deterministic);
    hierarchy.reset_solution();

    if options.align_to_boundaries && !pointcloud {
        hierarchy.clear_constraints();
        let faces = &hierarchy.faces;
        let e2e = &hierarchy.e2e;
        let n_edges = faces.cols() * 3;
        for i in 0..n_edges {
            if e2e.get(i).copied().unwrap_or(crate::math::INVALID) != crate::math::INVALID {
                continue;
            }
            let i0 = faces.get(i % 3, i / 3);
            let i1 = faces.get((i + 1) % 3, i / 3);
            let p0 = hierarchy.levels[0].v[i0 as usize];
            let p1 = hierarchy.levels[0].v[i1 as usize];
            let mut edge = p1 - p0;
            if edge.squared_norm() > 0.0 {
                edge = edge.normalized();
                hierarchy.levels[0].co[i0 as usize] = p0;
                hierarchy.levels[0].co[i1 as usize] = p1;
                hierarchy.levels[0].cq[i0 as usize] = edge;
                hierarchy.levels[0].cq[i1 as usize] = edge;
                hierarchy.levels[0].cqw[i0 as usize] = 1.0;
                hierarchy.levels[0].cqw[i1 as usize] = 1.0;
                hierarchy.levels[0].cow[i0 as usize] = 1.0;
                hierarchy.levels[0].cow[i1 as usize] = 1.0;
            }
        }
        hierarchy.propagate_constraints(options.rosy, options.posy);
    }

    let bvh_mesh = if pointcloud {
        None
    } else if options.smooth_iter > 0 {
        Some(Bvh::build_mesh(
            &hierarchy.faces,
            &hierarchy.levels[0].v,
            stats.aabb,
        ))
    } else {
        None
    };
    let _ = bvh_points;

    crate::say!(
        "Preprocessing is done. (total time excluding file I/O: {})",
        time_string(timer.elapsed().as_secs_f64() * 1000.0)
    );

    let parallel = !options.deterministic;
    let t0 = Instant::now();
    crate::say_raw!("Optimizing orientation field .. ");
    optimize_orientations_hierarchical(&mut hierarchy, options.extrinsic, options.rosy, parallel);
    crate::say!("done. (took {})", time_string(t0.elapsed().as_secs_f64() * 1000.0));
    let sing = compute_orientation_singularities(&hierarchy, options.extrinsic, options.rosy);
    crate::say!("Orientation field has {} singularities.", sing.len());

    let t1 = Instant::now();
    crate::say_raw!("Optimizing position field .. ");
    optimize_positions_hierarchical(&mut hierarchy, options.extrinsic, options.posy, parallel);
    crate::say!("done. (took {})", time_string(t1.elapsed().as_secs_f64() * 1000.0));

    let t2 = Instant::now();
    let (adj_extr, o_extr, n_extr, crease_out) = extract_graph(
        &hierarchy,
        options.extrinsic,
        options.rosy,
        options.posy,
        &crease_in,
        options.deterministic,
    );
    let extracted = extract_faces(
        adj_extr,
        o_extr,
        n_extr,
        options.posy,
        hierarchy.scale,
        crease_out,
        true,
        options.pure_quad,
        bvh_mesh.as_ref(),
        options.smooth_iter,
    );
    crate::say!(
        "Extraction is done. (total time: {})",
        time_string(t2.elapsed().as_secs_f64() * 1000.0)
    );

    let mut extracted = extracted;
    if !pointcloud && options.snap_boundary {
        let moved = crate::postprocess::snap_boundary(&extracted.faces, &mut extracted.positions, &input_boundary);
        crate::say!("Snapped {moved} boundary vertices onto the input boundary.");
    }
    if !pointcloud && options.fill_holes {
        let before = extracted.faces.cols();
        let (holes, added) = crate::postprocess::fill_holes(&mut extracted.faces, &extracted.positions, &input_boundary, 128);
        for f in before..extracted.faces.cols() {
            let face = extracted.faces.face(f);
            let (a, b, c) = (extracted.positions[face[0] as usize], extracted.positions[face[1] as usize], extracted.positions[face[2] as usize]);
            extracted.face_normals.push((b - a).cross(c - a).normalized());
        }
        crate::say!("Filled {holes} interior holes with {added} faces.");
    }

    Ok(OutputMesh {
        faces: extracted.faces,
        vertices: extracted.positions,
        normals: extracted.normals,
        face_normals: extracted.face_normals,
    })
}

fn resolve_scale(
    scale: &mut f32,
    face_count: &mut i32,
    vertex_count: &mut i32,
    posy: i32,
    surface_area: f64,
) -> Result<(), String> {
    if *scale > 0.0 {
        let face_area = if posy == 4 {
            (*scale as f64) * (*scale as f64)
        } else {
            3.0_f64.sqrt() / 4.0 * (*scale as f64) * (*scale as f64)
        };
        *face_count = if face_area > 0.0 {
            (surface_area / face_area) as i32
        } else {
            0
        };
        *vertex_count = if posy == 4 { *face_count } else { *face_count / 2 };
    } else if *face_count > 0 {
        let face_area = surface_area / *face_count as f64;
        *vertex_count = if posy == 4 { *face_count } else { *face_count / 2 };
        *scale = if posy == 4 {
            face_area.sqrt() as f32
        } else {
            (2.0 * (face_area * (1.0_f64 / 3.0).sqrt()).sqrt()) as f32
        };
    } else if *vertex_count > 0 {
        *face_count = if posy == 4 { *vertex_count } else { *vertex_count * 2 };
        let face_area = surface_area / *face_count as f64;
        *scale = if posy == 4 {
            face_area.sqrt() as f32
        } else {
            (2.0 * (face_area * (1.0_f64 / 3.0).sqrt()).sqrt()) as f32
        };
    }
    Ok(())
}

fn adjacency_pointcloud(
    vertices: &mut [Vec3],
    normals: &[Vec3],
    bvh: &Bvh,
    knn: usize,
) -> Adjacency {
    crate::say!("Generating adjacency matrix ..");
    let n = vertices.len();
    let mut lists: Vec<Vec<u32>> = vec![Vec::new(); n];
    let dset = DisjointSets::new(n);
    for i in 0..n {
        let found = bvh.find_k_nearest(vertices[i], Some(normals[i]), knn, 30.0);
        for (_, id) in found {
            if id as usize == i {
                continue;
            }
            lists[i].push(id);
            dset.unite(i as u32, id);
        }
    }
    for i in 0..n {
        for &k in lists[i].clone().iter() {
            if !lists[k as usize].contains(&(i as u32)) && lists[k as usize].len() < knn * 2 {
                lists[k as usize].push(i as u32);
            }
        }
    }
    let mut comp = HashMapCount::default();
    for i in 0..n {
        *comp.entry(dset.find(i as u32)) += 1;
    }
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let size = comp.get(&dset.find(i as u32)).copied().unwrap_or(0);
        if (size as f32) < n as f32 * 0.01 {
            vertices[i] = Vec3::new(1.0e6, 1.0e6, 1.0e6);
            out.push(Vec::new());
        } else {
            out.push(lists[i].iter().copied().map(Link::new).collect());
        }
    }
    crate::say!("done.");
    Adjacency::from_lists(out)
}

#[derive(Default)]
struct HashMapCount(std::collections::HashMap<u32, u32>);
impl HashMapCount {
    fn entry(&mut self, k: u32) -> &mut u32 {
        self.0.entry(k).or_insert(0)
    }
    fn get(&self, k: &u32) -> Option<&u32> {
        self.0.get(k)
    }
}
