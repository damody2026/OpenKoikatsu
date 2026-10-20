//! Execute the installed create_skinface DXBC with authored mip chains.
use crate::{clothes_gpu::Texture, face_create::Inputs};

/// Sideloader.GetPng creates RGBA32 with mipmap=true. Unity's byte mip
/// generator truncates each horizontal average before the vertical average;
/// averaging four floating-point texels and rounding is a different chain.
pub(crate) fn png_texture(png:&[u8],mut sampling:crate::unity::TextureSampling)->Option<Texture> {
    let mut image=crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    image::imageops::flip_vertical_in_place(&mut image);
    let (width,height)=image.dimensions();if width==0||height==0{return None;}
    let mut data=image.as_raw().clone();let mut mips=1;
    while image.width()>1||image.height()>1 {
        let (w,h)=((image.width()/2).max(1),(image.height()/2).max(1));
        image=image::RgbaImage::from_fn(w,h,|x,y| {
            let x0=(2*x).min(image.width()-1);let x1=(x0+1).min(image.width()-1);
            let y0=(2*y).min(image.height()-1);let y1=(y0+1).min(image.height()-1);
            image::Rgba(std::array::from_fn(|c| {
                let top=(image.get_pixel(x0,y0)[c] as u16+image.get_pixel(x1,y0)[c] as u16)/2;
                let bottom=(image.get_pixel(x0,y1)[c] as u16+image.get_pixel(x1,y1)[c] as u16)/2;
                ((top+bottom)/2) as u8
            }))
        });
        data.extend_from_slice(image.as_raw());mips+=1;
    }
    sampling.srgb=true;sampling.mip_count=mips as i32;
    Some(Texture{data,width,height,format:29,mips,sampling,aniso:1,bias:0.})
}

#[repr(C)]
struct Input { data:*const u8,size:usize,width:u32,height:u32,format:u32,mips:u32,
               filter:u32,wrap_u:u32,wrap_v:u32,aniso:u32,bias:f32 }
#[cfg(windows)]
extern "C" {
    fn ok_face_create_gpu_bake(shader:*const u8,size:usize,input:*const Input,constants:*const f32,
        width:u32,height:u32,quality:u32,output:*mut u8,output_size:usize,error:*mut u8,error_size:usize)->i32;
}

pub(crate) fn bake(shader:&[u8],textures:&[Texture;8],inputs:&Inputs<'_>,
                   width:u32,height:u32,quality:u32)->Result<Vec<u8>,String> {
    #[cfg(not(windows))] { let _=(shader,textures,inputs,width,height,quality);return Err("D3D11 unavailable".into()); }
    #[cfg(windows)] {
        let mut constants=[[0.;4];17];
        let colors=inputs.colors.map(|color| std::array::from_fn(|c|
            if c<3 {crate::srgb_channel_to_linear(color[c])}else{color[c]}));
        for (i,color) in [(2,colors[0]),(4,colors[1]),(7,colors[3]),(9,colors[4]),
                          (11,colors[2]),(14,colors[6]),(16,colors[5])] {constants[i]=color;}
        constants[3]=inputs.mask.as_ref().map(|t|t.st).unwrap_or([1.,1.,0.,0.]);
        constants[5]=inputs.main.st;constants[6]=inputs.cheek_uv;constants[8]=inputs.lipline_uv;
        constants[10]=inputs.paint_uv[0];constants[12]=inputs.paint_mask.as_ref().map(|t|t.st).unwrap_or([1.,1.,0.,0.]);
        constants[13]=inputs.paint_uv[1];constants[15]=inputs.mole_uv;
        let native=textures.each_ref().map(|t| Input{data:t.data.as_ptr(),size:t.data.len(),width:t.width,height:t.height,
            format:t.format,mips:t.mips,filter:t.sampling.filter as u32,wrap_u:t.sampling.wrap_u as u32,
            wrap_v:t.sampling.wrap_v as u32,aniso:t.aniso,bias:t.bias});
        let size=(width as usize).checked_mul(height as usize).and_then(|s|s.checked_mul(4)).ok_or("image overflow")?;
        let mut output=vec![0;size];let mut error=[0u8;512];
        let result=unsafe {ok_face_create_gpu_bake(shader.as_ptr(),shader.len(),native.as_ptr(),constants.as_ptr().cast(),
            width,height,quality,output.as_mut_ptr(),output.len(),error.as_mut_ptr(),error.len())};
        if result!=0 {return Err(String::from_utf8_lossy(&error[..error.iter().position(|&b|b==0).unwrap_or(error.len())]).into());}
        let image=image::RgbaImage::from_raw(width,height,output).ok_or("invalid face readback")?;
        crate::encode_png(&image).ok_or("PNG encode failed".into())
    }
}
