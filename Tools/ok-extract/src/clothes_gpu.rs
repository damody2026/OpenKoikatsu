//! Native create_topN execution. Resource data comes from authored bundles,
//! including compressed blocks and mip chains, never renderer dumps.
use std::path::Path;
use std::sync::OnceLock;

#[derive(Clone,serde::Serialize,serde::Deserialize)]
pub(crate) struct Texture {
    #[serde(with="serde_bytes")]
    pub data:Vec<u8>,pub width:u32,pub height:u32,pub format:u32,pub mips:u32,
    pub sampling:crate::unity::TextureSampling,pub aniso:u32,pub bias:f32,
}
impl Texture {
    pub fn white()->Self {Self{data:vec![255;4],width:1,height:1,format:28,mips:1,sampling:Default::default(),aniso:0,bias:0.0}}
}
#[repr(C)]
struct Input {data:*const u8,size:usize,width:u32,height:u32,format:u32,mips:u32,filter:u32,wrap_u:u32,wrap_v:u32,aniso:u32,bias:f32}
#[cfg(windows)]
extern "C" {
    fn ok_clothes_gpu_bake(shader:*const u8,shader_size:usize,input:*const Input,constants:*const f32,width:u32,height:u32,quality:u32,output:*mut u8,output_size:usize,error:*mut u8,error_size:usize)->i32;
    fn ok_clothes_gpu_identity(output:*mut u8,size:usize)->i32;
    fn ok_texture_gpu_readback(input:*const Input,width:u32,height:u32,output:*mut u8,output_size:usize,error:*mut u8,error_size:usize)->i32;
}

/// Time spent in GPU calls and their number (diagnostics: OK_PROFILE per converted card).
pub(crate) static GPU_NANOS:std::sync::atomic::AtomicU64=std::sync::atomic::AtomicU64::new(0);
pub(crate) static GPU_CALLS:std::sync::atomic::AtomicU64=std::sync::atomic::AtomicU64::new(0);

/// GPU results kept by their inputs' content, in a process converting cards (`keep_results`). A card's opens read the
/// same texture back again and again (every outfit and garment state extracts its meshes anew): 378 calls for two
/// cards were 129 different inputs, and each call waits a round trip on the GPU (~10–50 ms, shared by every worker of
/// a batch). A result depends on its inputs and the process's one device only; errors are not kept. The game (one
/// open per card) does not keep them.
static KEEP_RESULTS:std::sync::atomic::AtomicBool=std::sync::atomic::AtomicBool::new(false);
pub(crate) fn keep_results() {KEEP_RESULTS.store(true,std::sync::atomic::Ordering::Relaxed);}
fn gpu_memo(key:[u64;2],make:impl FnOnce()->Result<Vec<u8>,String>)->Result<Vec<u8>,String> {
    if !KEEP_RESULTS.load(std::sync::atomic::Ordering::Relaxed) {return make();}
    const KEPT_BYTES:usize=512<<20;
    static KEPT:std::sync::LazyLock<std::sync::Mutex<(std::collections::HashMap<[u64;2],Vec<u8>>,usize)>>=std::sync::LazyLock::new(Default::default);
    if let Some(hit)=KEPT.lock().unwrap_or_else(|e|e.into_inner()).0.get(&key).cloned() {return Ok(hit);}
    let made=make()?;
    let mut kept=KEPT.lock().unwrap_or_else(|e|e.into_inner());
    if kept.1+made.len()>KEPT_BYTES {kept.0.clear();kept.1=0;}
    kept.1+=made.len();kept.0.insert(key,made.clone());
    Ok(made)
}
fn gpu_key(kind:&str,inputs:&[&[u8]])->[u64;2] {
    use std::io::Write;
    let mut h=crate::push::StreamHash::default();
    let _=h.write_all(kind.as_bytes());
    for part in inputs {let _=h.write_all(&(part.len() as u64).to_le_bytes());let _=h.write_all(part);}
    h.finish()
}
fn texture_key(t:&Texture)->Vec<u8> {bincode::serialize(&(t.width,t.height,t.format,t.mips,&t.sampling,t.aniso,t.bias.to_bits())).unwrap_or_default()}

/// Decode compressed authored texels with their actual GPU sRGB view. This
/// avoids CPU BC interpolation errors being amplified by a second transfer.
pub(crate) fn texture_readback(texture:Texture)->Result<Vec<u8>,String> {
    let described=texture_key(&texture);
    gpu_memo(gpu_key("readback",&[&described,&texture.data]),||texture_readback_now(texture))
}
fn texture_readback_now(texture:Texture)->Result<Vec<u8>,String> {
    #[cfg(not(windows))] {let _=texture;return Err("D3D11 unavailable".into());}
    #[cfg(windows)] {
        let mut textures=std::array::from_fn::<_,5,_>(|_|Texture::white());
        let (width,height)=(texture.width,texture.height);textures[4]=texture;
        let inputs=textures.each_ref().map(|t|Input {data:t.data.as_ptr(),size:t.data.len(),width:t.width,height:t.height,
            format:t.format,mips:t.mips,filter:t.sampling.filter as u32,wrap_u:t.sampling.wrap_u as u32,
            wrap_v:t.sampling.wrap_v as u32,aniso:t.aniso,bias:t.bias});
        let mut out=vec![0u8;width as usize*height as usize*4];let mut error=vec![0u8;512];
        let t=std::time::Instant::now();
        let result=unsafe{ok_texture_gpu_readback(inputs.as_ptr(),width,height,out.as_mut_ptr(),out.len(),error.as_mut_ptr(),error.len())};
        GPU_NANOS.fetch_add(t.elapsed().as_nanos() as u64,std::sync::atomic::Ordering::Relaxed);GPU_CALLS.fetch_add(1,std::sync::atomic::Ordering::Relaxed);
        if result!=0 {return Err(String::from_utf8_lossy(&error[..error.iter().position(|&b|b==0).unwrap_or(error.len())]).into_owned());}
        let mut image=image::RgbaImage::from_raw(width,height,out).ok_or("invalid GPU texture readback")?;
        image::imageops::flip_vertical_in_place(&mut image);
        crate::encode_png(&image).ok_or("PNG encoding failed".into())
    }
}
pub(crate) fn identity()->&'static str {
    static ID:OnceLock<String>=OnceLock::new();
    ID.get_or_init(||{
        #[cfg(windows)] {let mut s=vec![0u8;256];unsafe{ok_clothes_gpu_identity(s.as_mut_ptr(),s.len());}return String::from_utf8_lossy(&s[..s.iter().position(|&x|x==0).unwrap_or(s.len())]).into_owned();}
        #[cfg(not(windows))] {"unavailable".into()}
    })
}

/// Only accept an unambiguous installed profile. Mixed quality overrides or
/// reduced texture resolution need runtime evidence and stay outside this gate.
fn quality_2019(raw:&[u8])->Option<u32> {
    // Unity 2019.4 QualitySettings native type tree: name, 13 four-byte
    // fields, textureQuality, anisotropicTextures, then 72 aligned bytes.
    let mut at=0usize;
    let read=|at:&mut usize|->Option<i32>{let n=i32::from_le_bytes(raw.get(*at..*at+4)?.try_into().ok()?);*at+=4;Some(n)};
    let current=read(&mut at)?;let count=read(&mut at)?;
    if !(1..=32).contains(&count)||current<0||current>=count{return None;}
    let mut selected=None;
    for _ in 0..count {
        let n=read(&mut at)?;if !(1..=1024).contains(&n){return None;}
        let name=raw.get(at..at+n as usize)?;std::str::from_utf8(name).ok()?;
        at=(at+n as usize+3)&!3;at=at.checked_add(13*4)?;
        if read(&mut at)?!=0{return None;}
        let mode=read(&mut at)?;if !(0..=2).contains(&mode)||selected.is_some_and(|p|p!=mode){return None;}
        selected=Some(mode);at=at.checked_add(72)?;
    }
    read(&mut at)?;if at!=raw.len(){return None;}
    selected.map(|n|n as u32)
}

pub(crate) fn quality(game:&Path)->Option<u32> {
    let bytes=crate::gamefs::read(game.join("CharaStudio_Data/globalgamemanagers")).ok()?;
    crate::oklib::memo_bytes(&bytes.clone(),"quality_settings",&(),||quality_unity(bytes))
}
fn quality_unity(bytes:Vec<u8>)->Option<u32> {
    let file=unity_asset_binary::asset::parse_serialized_file(bytes).ok()?;
    // globalgamemanagers strips its type tree; do not use the general object
    // converter's partial/unknown QualitySettings properties as a valid mode.
    if !file.unity_version.starts_with("2019.4."){return None;}
    let info=file.objects_of_type(47).into_iter().next()?;
    quality_2019(&file.object_bytes(info).ok()?)
}

pub(crate) fn bake(shader:&[u8],textures:&[Texture;5],constants:&[[f32;4];16],quality:u32)->Result<Vec<u8>,String> {
    let described:Vec<Vec<u8>>=textures.iter().map(texture_key).collect();
    let numbers=bincode::serialize(&(constants.map(|c|c.map(f32::to_bits)),quality)).unwrap_or_default();
    let mut inputs:Vec<&[u8]>=vec![shader,&numbers];
    for (d,t) in described.iter().zip(textures) {inputs.push(d);inputs.push(&t.data);}
    gpu_memo(gpu_key("bake",&inputs),||bake_now(shader,textures,constants,quality))
}
fn bake_now(shader:&[u8],textures:&[Texture;5],constants:&[[f32;4];16],quality:u32)->Result<Vec<u8>,String> {
    #[cfg(not(windows))] {let _=(shader,textures,constants,quality);return Err("native clothes GPU unavailable on this platform".into());}
    #[cfg(windows)] {
        let width=textures[4].width;let height=textures[4].height;
        let size=(width as usize).checked_mul(height as usize).and_then(|n|n.checked_mul(4)).ok_or("image overflow")?;
        let input:[Input;5]=std::array::from_fn(|i|{let t=&textures[i];Input{data:t.data.as_ptr(),size:t.data.len(),width:t.width,height:t.height,format:t.format,mips:t.mips,filter:t.sampling.filter as u32,wrap_u:t.sampling.wrap_u as u32,wrap_v:t.sampling.wrap_v as u32,aniso:t.aniso,bias:t.bias}});
        let mut output=vec![0;size];let mut error=[0u8;512];
        let t=std::time::Instant::now();
        let result=unsafe{ok_clothes_gpu_bake(shader.as_ptr(),shader.len(),input.as_ptr(),constants.as_ptr().cast(),width,height,quality,output.as_mut_ptr(),output.len(),error.as_mut_ptr(),error.len())};
        GPU_NANOS.fetch_add(t.elapsed().as_nanos() as u64,std::sync::atomic::Ordering::Relaxed);GPU_CALLS.fetch_add(1,std::sync::atomic::Ordering::Relaxed);
        if result!=0 {return Err(String::from_utf8_lossy(&error[..error.iter().position(|&x|x==0).unwrap_or(error.len())]).into_owned());}
        let mut image=image::RgbaImage::from_raw(width,height,output).ok_or("invalid GPU readback")?;
        // The public bake returns source row order; the shared runtime Blit
        // conversion performs the final flip once when assigning the RT.
        image::imageops::flip_vertical_in_place(&mut image);
        crate::encode_png(&image).ok_or("PNG encoding failed".into())
    }
}

#[cfg(test)]
mod tests {
 use super::*;
 #[test]
 #[cfg(windows)]
 fn texture_gpu_readback_preserves_srgb_source_rows_and_alpha() {
  let data=vec![32,188,188,73,80,128,255,191];
  let texture=Texture {data:data.clone(),width:1,height:2,format:29,mips:1,sampling:Default::default(),aniso:0,bias:0.};
  let png=texture_readback(texture.clone()).unwrap();
  assert_eq!(crate::teximg::load_from_memory(&png).unwrap().to_rgba8().into_raw(),data);
  let mut linear=texture;linear.format=28;linear.sampling.srgb=false;
  let png=texture_readback(linear).unwrap();
  let pixels=crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
  assert_eq!(pixels.into_raw(),data);
  let pixels=crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
  assert_eq!(pixels.get_pixel(0,0)[3],73);
 }
 fn fixture(modes:&[(i32,i32)])->Vec<u8>{
  let mut b=Vec::new();b.extend_from_slice(&0i32.to_le_bytes());b.extend_from_slice(&(modes.len() as i32).to_le_bytes());
  for &(texture,aniso) in modes {b.extend_from_slice(&4i32.to_le_bytes());b.extend_from_slice(b"Test");b.extend_from_slice(&[0;52]);b.extend_from_slice(&texture.to_le_bytes());b.extend_from_slice(&aniso.to_le_bytes());b.extend_from_slice(&[0;72]);}
  b.extend_from_slice(&0i32.to_le_bytes());b
 }
 #[test]fn quality_layout_rejects_ambiguous_or_truncated_profiles(){
  assert_eq!(quality_2019(&fixture(&[(0,2),(0,2)])),Some(2));
  assert_eq!(quality_2019(&fixture(&[(0,0)])),Some(0));assert_eq!(quality_2019(&fixture(&[(0,1)])),Some(1));
  for b in [fixture(&[(0,2),(0,1)]),fixture(&[(1,2)]),fixture(&[(0,3)]),fixture(&[])]{assert_eq!(quality_2019(&b),None);}
  let mut b=fixture(&[(0,2)]);b.pop();assert_eq!(quality_2019(&b),None);
  let mut b=fixture(&[(0,2)]);b.push(0);assert_eq!(quality_2019(&b),None);
 }
}
