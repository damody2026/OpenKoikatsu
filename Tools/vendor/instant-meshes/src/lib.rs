//! Rust port of Instant Field-Aligned Meshes
//! (Jakob, Tarini, Panozzo, Sorkine-Hornung, SIGGRAPH Asia 2015).
//!
//! The original C++ implementation is BSD-licensed; see `LICENSE.txt`.
//! This crate covers the batch remesher. The interactive OpenGL viewer from
//! the original program is not included.

use std::sync::atomic::{AtomicBool, Ordering};

static VERBOSE: AtomicBool = AtomicBool::new(true);

/// Progress output is on for the CLI. Embedding hosts can turn it off.
pub fn set_verbose(on: bool) {
    VERBOSE.store(on, Ordering::Relaxed);
}

#[doc(hidden)]
pub fn verbose() -> bool {
    VERBOSE.load(Ordering::Relaxed)
}

#[doc(hidden)]
#[macro_export]
macro_rules! say {
    ($($arg:tt)*) => {
        if $crate::verbose() {
            println!($($arg)*);
        }
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! say_raw {
    ($($arg:tt)*) => {
        if $crate::verbose() {
            print!($($arg)*);
        }
    };
}

pub mod adjacency;
pub mod batch;
pub mod bvh;
pub mod dedge;
pub mod dset;
pub mod extract;
#[cfg(feature = "fbx")]
mod fbxio;
pub mod field;
pub mod hierarchy;
pub mod math;
pub mod mesh;
pub mod meshio;
pub mod meshstats;
pub mod normal;
pub mod postprocess;
pub mod reorder;
pub mod subdivide;

pub use batch::{remesh, remesh_file, Options};
pub use mesh::{OutputMesh, Surface};
