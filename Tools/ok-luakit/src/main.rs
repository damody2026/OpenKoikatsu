//! ok-luakit: heavy helpers for the repository's Lua scripts.
//! Every subcommand prints exactly one JSON value on stdout; errors go to
//! stderr with a non-zero exit code. See README.md for the command list.
mod codec;
mod hashmany;
mod image_ops;
mod market;
mod misc;
mod ocr;
mod probe;
mod procs;
mod shader;
mod text;
mod unity;

use serde_json::Value;
use std::io::Read;

pub type Res<T> = Result<T, String>;

/// Read a file argument; "-" means stdin.
pub fn read_input(arg: &str) -> Res<Vec<u8>> {
    if arg == "-" {
        let mut buf = Vec::new();
        std::io::stdin().read_to_end(&mut buf).map_err(|e| format!("stdin: {e}"))?;
        Ok(buf)
    } else {
        std::fs::read(arg).map_err(|e| format!("{arg}: {e}"))
    }
}

pub fn write_output(path: &str, data: &[u8]) -> Res<()> {
    if let Some(parent) = std::path::Path::new(path).parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent).map_err(|e| format!("{}: {e}", parent.display()))?;
        }
    }
    std::fs::write(path, data).map_err(|e| format!("{path}: {e}"))
}

pub fn arg<'a>(args: &'a [String], i: usize, name: &str) -> Res<&'a str> {
    args.get(i).map(|s| s.as_str()).ok_or_else(|| format!("missing argument <{name}>"))
}

/// Value of "--name v" in args, if present.
pub fn opt<'a>(args: &'a [String], name: &str) -> Option<&'a str> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1)).map(|s| s.as_str())
}

pub fn flag(args: &[String], name: &str) -> bool {
    args.iter().any(|a| a == name)
}

fn run(args: &[String]) -> Res<Value> {
    let cmd = arg(args, 0, "command")?;
    let rest = &args[1..];
    match cmd {
        "hash" => codec::hash(rest),
        "hash-many" => hashmany::hash_many(rest),
        "zstd-d" | "zstd-c" | "lz4-block-d" | "lz4-block-c" | "zlib-d" | "zlib-c" | "gzip-d" | "gzip-c" => codec::transform(cmd, rest),
        "msgpack-json" => codec::msgpack_json(rest),
        "json-msgpack" => codec::json_msgpack(rest),
        "image-info" => image_ops::info(rest),
        "image-decode" => image_ops::decode(rest),
        "image-encode" => image_ops::encode(rest),
        "image-convert" => image_ops::convert(rest),
        "image-stats" => image_ops::stats(rest),
        "image-diff" => image_ops::diff(rest),
        "image-resize" => image_ops::resize(rest),
        "image-compose" => image_ops::compose(rest),
        "text-render" => text::render(rest),
        "unity-dump" => unity::dump(rest),
        "hardlink" => misc::hardlink(rest),
        "window-noactivate" => misc::window_noactivate(rest),
        "ocr" => ocr::ocr(rest),
        "file-id" => misc::file_id(rest),
        "disk-free" => misc::disk_free(rest),
        "zip-read" => misc::zip_read(rest),
        "zip-read-many" => misc::zip_read_many(rest),
        "font-metrics" => misc::font_metrics(rest),
        "proc-list" => procs::list(rest),
        "proc-kill" => procs::kill(rest),
        "proc-times" => procs::times(rest),
        "proc-wait" => procs::wait(rest),
        "proc-priority" => procs::priority(rest),
        "proc-affinity" => procs::affinity(rest),
        "cpu-cache" => procs::cpu_cache(rest),
        "u8-stats" => misc::u8_stats(rest),
        "zip-member" | "zip-names" | "unity-fields" | "unity-raw" | "unity-stream" | "unity-version" => shader::run(cmd, rest),
        "probe" => probe::run(rest),
        "sqlite" => market::sqlite(rest),
        "okbar-dump" => market::okbar_dump(rest),
        "zstd-c-sized" => market::zstd_sized(rest),
        "zstd-batch" => market::zstd_batch(rest),
        "zstd-c-batch" => market::zstd_c_batch(rest),
        "zhconv" => market::zhconv(rest),
        "pdf-text" => market::pdf_text(rest),
        "baostock" => market::baostock(rest),
        "text-decode" => market::text_decode(rest),
        "version" => Ok(serde_json::json!({"name": "ok-luakit", "version": env!("CARGO_PKG_VERSION")})),
        other => Err(format!("unknown command: {other}")),
    }
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match run(&args) {
        Ok(v) => {
            if !v.is_null() {
                println!("{}", serde_json::to_string(&v).unwrap());
            }
        }
        Err(e) => {
            eprintln!("ok-luakit: {e}");
            std::process::exit(1);
        }
    }
}
