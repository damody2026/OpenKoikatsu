//! Bounding volume hierarchy for reprojection and point-cloud neighborhoods.

use crate::math::{Aabb, Ray, Vec3, RCPOVERFLOW};
use crate::mesh::Faces;

struct Node {
    aabb: Aabb,
    start: u32,
    count: u32,
    right: u32,
    leaf: bool,
}

pub struct Bvh {
    nodes: Vec<Node>,
    indices: Vec<u32>,
    vertices: Vec<Vec3>,
    normals: Vec<Vec3>,
    faces: Vec<[u32; 3]>,
    pub disk_radius: f32,
}

impl Bvh {
    pub fn build_mesh(faces: &Faces, vertices: &[Vec3], aabb: Aabb) -> Self {
        let n = faces.cols();
        let indices: Vec<u32> = (0..n as u32).collect();
        let tris: Vec<[u32; 3]> = (0..n)
            .map(|f| [faces.get(0, f), faces.get(1, f), faces.get(2, f)])
            .collect();
        let mut bvh = Self {
            nodes: Vec::new(),
            indices,
            vertices: vertices.to_vec(),
            normals: Vec::new(),
            faces: tris,
            disk_radius: 0.0,
        };
        if n == 0 {
            return bvh;
        }
        bvh.nodes.push(Node {
            aabb,
            start: 0,
            count: n as u32,
            right: 0,
            leaf: true,
        });
        bvh.split(0);
        crate::say!("Built BVH ({} triangles).", n);
        bvh
    }

    pub fn build_points(vertices: &[Vec3], normals: &[Vec3], aabb: Aabb) -> Self {
        let n = vertices.len();
        let mut bvh = Self {
            nodes: Vec::new(),
            indices: (0..n as u32).collect(),
            vertices: vertices.to_vec(),
            normals: normals.to_vec(),
            faces: Vec::new(),
            disk_radius: 0.0,
        };
        if n == 0 {
            return bvh;
        }
        bvh.nodes.push(Node {
            aabb,
            start: 0,
            count: n as u32,
            right: 0,
            leaf: true,
        });
        bvh.split(0);
        let mut sum = 0.0f64;
        let mut hits = 0u32;
        for i in 0..n {
            let mut radius = f32::INFINITY;
            if let Some((_, d)) = bvh.find_nearest(vertices[i], &mut radius, false) {
                if d.is_finite() {
                    sum += d as f64;
                    hits += 1;
                }
            }
        }
        if hits > 0 {
            bvh.disk_radius = (sum / hits as f64) as f32 * 3.0;
        }
        bvh
    }

    fn centroid(&self, prim: u32) -> Vec3 {
        if self.faces.is_empty() {
            self.vertices[prim as usize]
        } else {
            let f = self.faces[prim as usize];
            (self.vertices[f[0] as usize]
                + self.vertices[f[1] as usize]
                + self.vertices[f[2] as usize])
                * (1.0 / 3.0)
        }
    }

    fn prim_aabb(&self, prim: u32) -> Aabb {
        let mut box_ = Aabb::empty();
        if self.faces.is_empty() {
            let p = self.vertices[prim as usize];
            let r = self.disk_radius;
            box_.expand(p);
            if r > 0.0 {
                box_.expand(p + Vec3::new(r, r, r));
                box_.expand(p - Vec3::new(r, r, r));
            }
        } else {
            let f = self.faces[prim as usize];
            box_.expand(self.vertices[f[0] as usize]);
            box_.expand(self.vertices[f[1] as usize]);
            box_.expand(self.vertices[f[2] as usize]);
        }
        box_
    }

    fn split(&mut self, node_idx: usize) {
        let start = self.nodes[node_idx].start as usize;
        let count = self.nodes[node_idx].count as usize;
        if count <= 8 {
            return;
        }
        let axis = self.nodes[node_idx].aabb.largest_axis();
        let mut order: Vec<(u32, f32)> = self.indices[start..start + count]
            .iter()
            .copied()
            .map(|prim| (prim, self.centroid(prim).comp(axis)))
            .collect();
        order.sort_by(|a, b| a.1.partial_cmp(&b.1).unwrap_or(std::cmp::Ordering::Equal));
        for (dst, (prim, _)) in self.indices[start..start + count].iter_mut().zip(order) {
            *dst = prim;
        }
        let mid = count / 2;
        let left_start = start;
        let right_start = start + mid;
        let mut left_box = Aabb::empty();
        let mut right_box = Aabb::empty();
        for &prim in &self.indices[left_start..right_start] {
            left_box = left_box.merge(self.prim_aabb(prim));
        }
        for &prim in &self.indices[right_start..start + count] {
            right_box = right_box.merge(self.prim_aabb(prim));
        }
        let left_idx = self.nodes.len() as u32;
        self.nodes.push(Node {
            aabb: left_box,
            start: left_start as u32,
            count: mid as u32,
            right: 0,
            leaf: true,
        });
        let right_idx = self.nodes.len() as u32;
        self.nodes.push(Node {
            aabb: right_box,
            start: right_start as u32,
            count: (count - mid) as u32,
            right: 0,
            leaf: true,
        });
        self.nodes[node_idx].leaf = false;
        self.nodes[node_idx].right = right_idx;
        self.nodes[node_idx].start = left_idx;
        self.split(left_idx as usize);
        self.split(right_idx as usize);
    }

    pub fn ray_intersect(&self, mut ray: Ray) -> Option<(u32, f32)> {
        if self.nodes.is_empty() {
            return None;
        }
        let mut stack = vec![0u32];
        let mut best: Option<(u32, f32)> = None;
        while let Some(node_idx) = stack.pop() {
            let node = &self.nodes[node_idx as usize];
            if !node.aabb.ray_intersect(&ray) {
                continue;
            }
            if !node.leaf {
                stack.push(node.start);
                stack.push(node.right);
                continue;
            }
            for k in 0..node.count {
                let prim = self.indices[node.start as usize + k as usize];
                if let Some(t) = self.intersect_prim(&ray, prim) {
                    if t < ray.maxt {
                        ray.maxt = t;
                        best = Some((prim, t));
                    }
                }
            }
        }
        best
    }

    fn intersect_prim(&self, ray: &Ray, prim: u32) -> Option<f32> {
        if self.faces.is_empty() {
            self.intersect_disk(ray, prim)
        } else {
            self.intersect_tri(ray, prim)
        }
    }

    fn intersect_tri(&self, ray: &Ray, prim: u32) -> Option<f32> {
        let f = self.faces[prim as usize];
        let p0 = self.vertices[f[0] as usize];
        let p1 = self.vertices[f[1] as usize];
        let p2 = self.vertices[f[2] as usize];
        let edge1 = p1 - p0;
        let edge2 = p2 - p0;
        let pvec = ray.d.cross(edge2);
        let det = edge1.dot(pvec);
        if det == 0.0 {
            return None;
        }
        let inv_det = 1.0 / det;
        let tvec = ray.o - p0;
        let u = tvec.dot(pvec) * inv_det;
        if !(0.0..=1.0).contains(&u) {
            return None;
        }
        let qvec = tvec.cross(edge1);
        let v = ray.d.dot(qvec) * inv_det;
        if v < 0.0 || u + v > 1.0 {
            return None;
        }
        let t = edge2.dot(qvec) * inv_det;
        if t < ray.mint || t > ray.maxt {
            None
        } else {
            Some(t)
        }
    }

    fn intersect_disk(&self, ray: &Ray, prim: u32) -> Option<f32> {
        let v = self.vertices[prim as usize];
        let n = self.normals[prim as usize];
        let dp = ray.d.dot(n);
        if dp.abs() < RCPOVERFLOW {
            return None;
        }
        let t = (n.dot(v) - n.dot(ray.o)) / dp;
        if t < ray.mint || t > ray.maxt {
            return None;
        }
        if (ray.at(t) - v).squared_norm() < self.disk_radius * self.disk_radius {
            Some(t)
        } else {
            None
        }
    }

    pub fn find_nearest(&self, p: Vec3, radius: &mut f32, include_self: bool) -> Option<(u32, f32)> {
        if self.nodes.is_empty() {
            return None;
        }
        let mut best: Option<(u32, f32)> = None;
        let mut stack = vec![0u32];
        while let Some(node_idx) = stack.pop() {
            let node = &self.nodes[node_idx as usize];
            if node.aabb.squared_distance(p) > *radius * *radius {
                continue;
            }
            if !node.leaf {
                stack.push(node.start);
                stack.push(node.right);
                continue;
            }
            for k in 0..node.count {
                let prim = self.indices[node.start as usize + k as usize];
                let c = self.centroid(prim);
                let d = (c - p).norm();
                if d < *radius && (include_self || d > 1e-8) {
                    *radius = d;
                    best = Some((prim, d));
                }
            }
        }
        best
    }

    pub fn find_k_nearest(
        &self,
        p: Vec3,
        normal: Option<Vec3>,
        k: usize,
        angle_deg: f32,
    ) -> Vec<(f32, u32)> {
        if self.nodes.is_empty() || k == 0 {
            return Vec::new();
        }
        let cos_thresh = (angle_deg * std::f32::consts::PI / 180.0).cos();
        let mut best: Vec<(f32, u32)> = Vec::new();
        let mut radius2 = f32::INFINITY;
        let mut stack = vec![0u32];
        while let Some(node_idx) = stack.pop() {
            let node = &self.nodes[node_idx as usize];
            if node.aabb.squared_distance(p) > radius2 {
                continue;
            }
            if !node.leaf {
                stack.push(node.start);
                stack.push(node.right);
                continue;
            }
            for i in 0..node.count {
                let prim = self.indices[node.start as usize + i as usize];
                let c = self.centroid(prim);
                let d2 = (c - p).squared_norm();
                if d2 < 1e-20 {
                    continue;
                }
                if let Some(n) = normal {
                    if !self.normals.is_empty() && n.dot(self.normals[prim as usize]) < cos_thresh {
                        continue;
                    }
                }
                if best.len() < k || d2 < radius2 {
                    best.push((d2, prim));
                    best.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap_or(std::cmp::Ordering::Equal));
                    if best.len() > k {
                        best.pop();
                    }
                    if best.len() == k {
                        radius2 = best[0].0;
                    }
                }
            }
        }
        best.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap_or(std::cmp::Ordering::Equal));
        best.into_iter().map(|(d2, id)| (d2.sqrt(), id)).collect()
    }
}
