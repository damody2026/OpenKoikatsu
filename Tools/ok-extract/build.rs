fn main(){
 println!("cargo:rerun-if-changed=src/clothes_gpu.cpp");
 if std::env::var("CARGO_CFG_TARGET_OS").as_deref()==Ok("windows") {
  cc::Build::new().cpp(true).file("src/clothes_gpu.cpp").flag_if_supported("/EHsc").compile("ok_clothes_gpu");
  println!("cargo:rustc-link-lib=d3d11");println!("cargo:rustc-link-lib=d3dcompiler");println!("cargo:rustc-link-lib=dxgi");
 }
}
