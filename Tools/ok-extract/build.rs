fn main(){
 println!("cargo:rerun-if-changed=src/clothes_gpu.cpp");
 if std::env::var("CARGO_CFG_TARGET_OS").as_deref()==Ok("windows") {
  cc::Build::new().cpp(true).file("src/clothes_gpu.cpp").flag_if_supported("/EHsc").compile("ok_clothes_gpu");
  println!("cargo:rustc-link-lib=d3d11");println!("cargo:rustc-link-lib=d3dcompiler");println!("cargo:rustc-link-lib=dxgi");
 }
 source_hash();
}

/// OK_EXTRACT_SOURCE_HASH: every source file under src/ (relative path and bytes, in path order). The cached opens
/// (oklib::session_cache_key) carry it, so a build never reuses what another version of the extractor stored: a
/// change that forgets to bump SESSION_CACHE_REVISION can no longer leave stale characters in the shared cache.
fn source_hash(){
 println!("cargo:rerun-if-changed=src");
 fn walk(dir:&std::path::Path,root:&std::path::Path,out:&mut Vec<(String,std::path::PathBuf)>){
  let Ok(entries)=std::fs::read_dir(dir) else { return };
  for e in entries.flatten() {
   let p=e.path();
   if p.is_dir() { walk(&p,root,out); }
   else if matches!(p.extension().and_then(|x| x.to_str()),Some("rs"|"cpp"|"h"|"hlsl"|"json")) {
    out.push((p.strip_prefix(root).unwrap().to_string_lossy().replace('\\',"/"),p));
   }
  }
 }
 let root=std::path::Path::new("src");
 let mut files=Vec::new();
 walk(root,root,&mut files);
 files.sort();
 // FNV-1a 64 over (name, length, bytes) with line endings normalised: a checkout's CRLF/LF choice is not a change.
 let mut h:u64=0xcbf29ce484222325;
 let mut feed=|bytes:&[u8]|{ for b in bytes { h^=*b as u64; h=h.wrapping_mul(0x100000001b3); } };
 for (name,path) in &files {
  let mut bytes=std::fs::read(path).unwrap_or_default();
  bytes.retain(|&b| b!=b'\r');
  feed(name.as_bytes());
  feed(&(bytes.len() as u64).to_le_bytes());
  feed(&bytes);
 }
 println!("cargo:rustc-env=OK_EXTRACT_SOURCE_HASH={h:016x}");
}
