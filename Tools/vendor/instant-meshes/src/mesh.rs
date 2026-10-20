//! Indexed polygon soup. Faces are stored column-major, matching the original
//! Eigen layout: component `r` of face `c` lives at `idx[c * degree + r]`.

use crate::math::{Vec3, INVALID};

#[derive(Clone, Debug)]
pub struct Faces {
    pub degree: usize,
    pub idx: Vec<u32>,
}

impl Faces {
    pub fn empty() -> Self {
        Self {
            degree: 3,
            idx: Vec::new(),
        }
    }

    pub fn new(degree: usize, cols: usize) -> Self {
        Self {
            degree,
            idx: vec![INVALID; degree * cols],
        }
    }

    pub fn cols(&self) -> usize {
        if self.degree == 0 {
            0
        } else {
            self.idx.len() / self.degree
        }
    }

    pub fn get(&self, r: usize, c: usize) -> u32 {
        self.idx[c * self.degree + r]
    }

    pub fn set(&mut self, r: usize, c: usize, v: u32) {
        self.idx[c * self.degree + r] = v;
    }

    pub fn face(&self, c: usize) -> &[u32] {
        let s = c * self.degree;
        &self.idx[s..s + self.degree]
    }

    pub fn resize_cols(&mut self, cols: usize) {
        self.idx.resize(cols * self.degree, INVALID);
    }

    pub fn ensure_cols(&mut self, cols: usize) {
        if self.cols() < cols {
            let grow = cols.max(self.cols().saturating_mul(2)).max(1);
            self.resize_cols(grow);
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct Surface {
    pub faces: Faces,
    pub vertices: Vec<Vec3>,
    pub normals: Vec<Vec3>,
}

impl Default for Faces {
    fn default() -> Self {
        Self::empty()
    }
}

#[derive(Clone, Debug)]
pub struct OutputMesh {
    pub faces: Faces,
    pub vertices: Vec<Vec3>,
    pub normals: Vec<Vec3>,
    pub face_normals: Vec<Vec3>,
}
