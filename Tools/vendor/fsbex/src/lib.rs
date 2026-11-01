//! # fsbex
//!
//! `fsbex` is a library for extracting audio from FMOD sound banks. Only FSB version 5 is supported for now.
//!
//! ## Example
//!
//! Parsing a sound bank, then writing streams to files:
//!
//! ```no_run
//! use fsbex::{Bank, AudioFormat};
//! use std::{
//!     error::Error,
//!     io::{BufReader, BufWriter},
//!     fs::File,
//! };
//!
//! fn main() -> Result<(), Box<dyn Error>> {
//!     // open file for reading sound bank
//!     let file = BufReader::new(File::open("example.fsb")?);
//!     let bank = Bank::new(file)?;
//!
//!     // report number of streams contained within the sound bank
//!     println!("{} streams within this sound bank", bank.num_streams());
//!
//!     // check stream audio format
//!     if bank.format() != AudioFormat::Vorbis {
//!         return Err("expected Vorbis format".into());
//!     }
//!
//!     // iterate over streams
//!     for (index, stream) in bank.into_iter().enumerate() {
//!         // check stream name
//!         let file_name = if let Some(name) = stream.name() {
//!             format!("{name}.ogg")
//!         } else {
//!             format!("stream_{index}.ogg")
//!         };
//!
//!         // write stream data to file
//!         let output_file = BufWriter::new(File::create(file_name)?);
//!         stream.write(output_file)?;
//!     }
//!
//!     Ok(())
//! }
//! ```
//!
//! ## Supported formats
//!
//! `fsbex` supports encoding stream data for the following formats:
//! - PCM (8, 16, 24, 32-bit integer)
//! - PCM (32-bit float)
//! - Vorbis

#![warn(clippy::pedantic, future_incompatible, unused)]
#![deny(
    let_underscore_drop,
    macro_use_extern_crate,
    meta_variable_misuse,
    missing_abi,
    missing_debug_implementations,
    missing_docs,
    non_ascii_idents,
    nonstandard_style,
    noop_method_call,
    rust_2018_idioms,
    trivial_casts,
    trivial_numeric_casts,
    unreachable_pub,
    unsafe_op_in_unsafe_fn,
    unused_crate_dependencies,
    unused_import_braces,
    unused_lifetimes,
    unused_macro_rules,
    unused_qualifications,
    unused_results,
    unused_tuple_struct_fields
)]
#![allow(clippy::module_name_repetitions)]

mod bank;
pub mod encode;
mod header;
mod read;
mod stream;

pub use bank::{Bank, DecodeError, LazyStreamError};
pub use header::{AudioFormat, Loop};
pub use stream::{LazyStream, Stream, StreamIntoIter};
