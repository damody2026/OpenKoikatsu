//! Texture helpers of the open: PNG encode/flip/mean, sRGB tables and sampling, eye-white and iris bakes, composites.
use super::*;

/// Mean RGBA (0..1) of a PNG, for diagnostics.
pub(crate) fn png_mean(png: &[u8]) -> Option<[f32; 4]> {
    let img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let (w, h) = img.dimensions();
    if w == 0 || h == 0 {
        return None;
    }
    let mut sum = [0f64; 4];
    let n = (w * h) as f64;
    for p in img.pixels() {
        for c in 0..4 {
            sum[c] += p.0[c] as f64;
        }
    }
    Some([
        (sum[0] / n / 255.0) as f32,
        (sum[1] / n / 255.0) as f32,
        (sum[2] / n / 255.0) as f32,
        (sum[3] / n / 255.0) as f32,
    ])
}

pub(crate) fn encode_png(img: &image::RgbaImage) -> Option<Vec<u8>> {let _spot=crate::profile::spot("encode_png");
    // PNG, or the raw container with OK_RAW_TEXTURES=1 (teximg).
    crate::teximg::encode_rgba(img)
}

pub(crate) fn flip_png_v(png: &[u8]) -> Option<Vec<u8>> {let _spot=crate::profile::spot("flip_png_v");
    if std::env::var("OK_EYE_FLIP").as_deref() == Ok("0") {
        return Some(png.to_vec());
    }
    // Eye and face textures repeat across cards and outfits: decode+flip+encode once per content.
    png_transform_cache::transform("flip_png_v_v1",png,&[],||{
        let mut im = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
        image::imageops::flip_vertical_in_place(&mut im);
        encode_png(&im)
    })
}

pub(crate) fn lerp_f(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

/// Koikano/create_eyew PS 0001: lerp(_Color2, _Color, sampled R).
/// Material colours and sRGB samples are linear during the draw; the RT
/// encodes the result. Alpha comes from those colours, not texture coverage.
pub(crate) fn bake_eye_white(png: &[u8], base: [f32; 4], sub: [f32; 4]) -> Option<Vec<u8>> {
    bake_eye_white_with_space(png, base, sub, false)
}

/// ChaControl.InitBaseCustomTextureEtc creates a 512-square high-poly eye
/// white RT (256 low-poly), independent of the catalog texture dimensions.
pub(crate) fn bake_eye_white_create_rt(png:&[u8],base:[f32;4],sub:[f32;4],source_linear:bool,flip_source:bool)->Option<Vec<u8>>{
    bake_eye_white_create_rt_sized(png,base,sub,source_linear,if character_hi_poly(){512}else{256},flip_source)
}
pub(crate) fn bake_eye_white_create_rt_sized(png:&[u8],base:[f32;4],sub:[f32;4],source_linear:bool,size:u32,flip_source:bool)->Option<Vec<u8>>{
    let mut parameters=Vec::with_capacity(38);
    for v in base.into_iter().chain(sub){parameters.extend(v.to_bits().to_le_bytes());}
    parameters.push(u8::from(source_linear));parameters.extend(size.to_le_bytes());parameters.push(u8::from(flip_source));
    png_transform_cache::transform("create_eyew_rt_v1",png,&parameters,||{
        let image=crate::teximg::load_from_memory(png).ok()?.to_rgba8();
        let decode:[f32;256]=std::array::from_fn(|i|{
            let v=i as f32/255.;if source_linear{v}else{srgb_channel_to_linear(v)}
        });
        let base_alpha=base[3];
        let base:[f32;3]=std::array::from_fn(|i|srgb_channel_to_linear(base[i]));
        let sub_rgb:[f32;3]=std::array::from_fn(|i|srgb_channel_to_linear(sub[i]));
        let mut result=image::RgbaImage::new(size,size);
        for(y,row)in result.rows_mut().enumerate(){for(x,pixel)in row.enumerate(){
            let shade=sample_red_clamped_lookup(&image,(x as f32+0.5)/size as f32,(y as f32+0.5)/size as f32,&decode,flip_source);
            for c in 0..3{pixel[c]=to_unorm8(linear_channel_to_srgb(lerp_f(sub_rgb[c],base[c],shade)));}
            // Source alpha never supplies the create_eyew output alpha.
            pixel[3]=to_unorm8(lerp_f(sub[3],base_alpha,shade));
        }}
        encode_png(&result)
    })
}
pub(crate) fn sample_red_clamped_lookup(image:&image::RgbaImage,u:f32,v:f32,decode:&[f32;256],flip_source:bool)->f32{
    let(width,height)=image.dimensions();let x=u*width as f32-0.5;let y=v*height as f32-0.5;
    let x0=x.floor();let y0=y.floor();let fx=x-x0;let fy=y-y0;
    let sample=|x:f32,y:f32|{
        let y=y.clamp(0.,(height-1) as f32) as u32;
        decode[image.get_pixel(x.clamp(0.,(width-1) as f32) as u32,if flip_source{height-1-y}else{y})[0] as usize]
    };
    lerp_f(lerp_f(sample(x0,y0),sample(x0+1.,y0),fx),lerp_f(sample(x0,y0+1.),sample(x0+1.,y0+1.),fx),fy)
}
pub(crate) fn bake_eye_white_with_space(png: &[u8], base: [f32; 4], sub: [f32; 4], source_linear: bool) -> Option<Vec<u8>> {
    let mut parameters=Vec::with_capacity(33);
    for v in base.into_iter().chain(sub){parameters.extend(v.to_bits().to_le_bytes());}
    parameters.push(u8::from(source_linear));
    png_transform_cache::transform("create_eyew_v1",png,&parameters,||bake_eye_white_uncached(png,base,sub,source_linear))
}
pub(crate) fn bake_eye_white_uncached(png: &[u8], base: [f32; 4], sub: [f32; 4], source_linear: bool) -> Option<Vec<u8>> {
    let img = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let (w, h) = img.dimensions();
    let mut out = image::RgbaImage::new(w, h);
    // create_eyew depends only on the sampled red byte. Evaluate its exact
    // colour/alpha arithmetic once per possible value, rather than repeating
    // the sRGB powers for every texel of a potentially 4K source.
    let lookup: [[u8; 4]; 256] = std::array::from_fn(|red| {
            let shade = if source_linear {red as f32 / 255.0} else {srgb_channel_to_linear(red as f32 / 255.0)};
            let mut q = [0u8; 4];
            for c in 0..3 {
                let v = lerp_f(srgb_channel_to_linear(sub[c]), srgb_channel_to_linear(base[c]), shade);
                q[c] = to_unorm8(linear_channel_to_srgb(v));
            }
            // create_eyew interpolates colour alpha independently of source A.
            q[3] = to_unorm8(lerp_f(sub[3],base[3],shade));
            q
    });
    for (source, destination) in img.pixels().zip(out.pixels_mut()) {
        destination.0 = lookup[source[0] as usize];
    }
    encode_png(&out)
}

/// Koikano/create_eye PS (mm_base/0001): _Color2 -> _Color gradation,
/// multiplication/vivid-light blend in linear space, then the sRGB create RT.
pub(crate) fn bake_iris(
    png: &[u8], pupil: &crate::card::PupilInfo, grad: Option<&[u8]>,
    hl_up: Option<&[u8]>, hl_down: Option<&[u8]>,
    hl_up_color: [f32;4], hl_down_color: [f32;4], hl_up_y:f32, hl_down_y:f32,
) -> Option<Vec<u8>> {
    bake_iris_with_space(png,pupil,grad,hl_up,hl_down,hl_up_color,hl_down_color,hl_up_y,hl_down_y,false)
}

pub(crate) fn bake_iris_with_space(
    png:&[u8],pupil:&crate::card::PupilInfo,grad:Option<&[u8]>,
    hl_up:Option<&[u8]>,hl_down:Option<&[u8]>,hl_up_color:[f32;4],hl_down_color:[f32;4],
    hl_up_y:f32,hl_down_y:f32,source_linear:bool,
) -> Option<Vec<u8>> {
    let mut parameters=Vec::new();parameters.push(u8::from(source_linear));
    for value in pupil.base_color.into_iter().chain(pupil.sub_color).chain(hl_up_color).chain(hl_down_color)
        .chain([pupil.grad_blend,pupil.grad_offset_y,pupil.grad_scale,hl_up_y,hl_down_y]){parameters.extend(value.to_le_bytes());}
    for input in [grad,hl_up,hl_down]{parameters.push(u8::from(input.is_some()));if let Some(input)=input{parameters.extend((input.len() as u64).to_le_bytes());parameters.extend(input);}}
    png_transform_cache::transform("create_iris_rt_v1",png,&parameters,||bake_iris_with_space_uncached(png,pupil,grad,hl_up,hl_down,hl_up_color,hl_down_color,hl_up_y,hl_down_y,source_linear))
}

pub(crate) fn bake_iris_with_space_uncached(
    png:&[u8],pupil:&crate::card::PupilInfo,grad:Option<&[u8]>,
    hl_up:Option<&[u8]>,hl_down:Option<&[u8]>,hl_up_color:[f32;4],hl_down_color:[f32;4],
    hl_up_y:f32,hl_down_y:f32,source_linear:bool,
) -> Option<Vec<u8>> {
    let source=crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let gradient=grad.and_then(|p|crate::teximg::load_from_memory(p).ok().map(|i|i.to_rgba8()));
    let highlights=[hl_up,hl_down].map(|p|p.and_then(|p|crate::teximg::load_from_memory(p).ok().map(|i|i.to_rgba8())));
    let colors=[hl_up_color,hl_down_color];let offsets=[lerp_f(0.1,-0.1,hl_up_y),lerp_f(0.1,-0.1,hl_down_y)];
    let scale=2.0*(1.0-lerp_f(-1.0,1.0,pupil.grad_scale));let offset=lerp_f(-0.5,0.5,pupil.grad_offset_y);
    let sub=pupil.sub_color.map(srgb_channel_to_linear);let base=pupil.base_color.map(srgb_channel_to_linear);
    let mut result=image::RgbaImage::new(512,512);
    for y in 0..512 {for x in 0..512 {
        let u=(x as f32+0.5)/512.0;let v=(y as f32+0.5)/512.0;
        let main=sample_rgba_clamped_space(&source,u,v,!source_linear);
        // create_eye enables AlphaToMask. With the single-sample create RT,
        // coverage below one half leaves its transparent cleared background.
        if main[3]<0.5 {continue;}
        let g=gradient.as_ref().map_or(1.0,|image|sample_rgba_linear(image,u*scale+0.5-scale*0.5,(v-offset)*scale+0.5-scale*0.5)[0]);
        let mut rgb=[0.0;3];
        for c in 0..3 {
            let tint=lerp_f(sub[c],base[c],g);
            let vivid=if main[c]>0.5 {tint/(2.0*(1.0-main[c])).max(0.00001)} else {1.0-(1.0-tint)*0.5/main[c].max(0.00001)};
            let linear=lerp_f(tint*main[c],vivid.clamp(0.0,1.0),pupil.grad_blend)*(main[0]*64.0).clamp(0.0,1.0);
            rgb[c]=linear_channel_to_srgb(linear);
        }
        // Known main_eye/toon_eye draw shaders receive their highlights at
        // draw time. Preserve the legacy fallback only for unknown draw shaders.
        for i in 0..2 {if let Some(highlight)=&highlights[i] {
            let h=sample_rgba_clamped(highlight,u,v-offsets[i]);let a=h[3]*colors[i][3]*main[3];
            for c in 0..3 {rgb[c]=lerp_f(rgb[c],h[c]*colors[i][c],a);}
        }}
        result.put_pixel(x,y,image::Rgba([to_unorm8(rgb[0]),to_unorm8(rgb[1]),to_unorm8(rgb[2]),to_unorm8(main[3])]));
    }}encode_png(&result)
}

#[test]
pub(crate) fn iris_lut_bake_matches_reference(){
    let image=image::RgbaImage::from_fn(16,16,|x,y|{let n=(x+y*16)as u8;image::Rgba([n,255-n,n.wrapping_mul(17),n.wrapping_mul(7)])});
    for linear in [false,true]{for i in 0..257{
        let u=i as f32/113.-0.5;let v=(i*31%257)as f32/127.-0.5;
        assert_eq!(sample_rgba_clamped_space(&image,u,v,linear).map(f32::to_bits),sample_rgba_clamped_space_reference(&image,u,v,linear).map(f32::to_bits),"bilinear sample {i}/{linear}");
    }}
    let png=encode_png(&image).unwrap();
    let grad=encode_png(&image::RgbaImage::from_fn(8,8,|x,y|image::Rgba([(x*19+y*11)as u8,0,0,255]))).unwrap();
    let highlight=encode_png(&image::RgbaImage::from_fn(8,8,|x,y|image::Rgba([255,(x*30)as u8,(y*30)as u8,155]))).unwrap();
    let pupil=crate::card::PupilInfo{base_color:[0.14,0.52,0.87,0.95],sub_color:[0.72,0.22,0.38,0.8],grad_blend:0.65,grad_offset_y:0.31,grad_scale:0.37,..Default::default()};
    for (linear,layers) in [(false,false),(true,false),(false,true),(true,true)]{
        let g=layers.then_some(grad.as_slice());let h=layers.then_some(highlight.as_slice());
        let expected=bake_iris_with_space_reference(&png,&pupil,g,h,h,[0.9,0.2,0.7,0.7],[0.3,0.7,0.8,0.5],0.2,0.8,linear).unwrap();
        let optimized=bake_iris_with_space_uncached(&png,&pupil,g,h,h,[0.9,0.2,0.7,0.7],[0.3,0.7,0.8,0.5],0.2,0.8,linear).unwrap();
        assert_eq!(optimized,expected,"uncached iris pixels {linear}/{layers}");
        for _ in 0..2{assert_eq!(bake_iris_with_space(&png,&pupil,g,h,h,[0.9,0.2,0.7,0.7],[0.3,0.7,0.8,0.5],0.2,0.8,linear),Some(expected.clone()),"cold/warm iris result {linear}/{layers}");}
    }
}

#[cfg(test)]
pub(crate) fn bake_iris_with_space_reference(
    png:&[u8],pupil:&crate::card::PupilInfo,grad:Option<&[u8]>,
    hl_up:Option<&[u8]>,hl_down:Option<&[u8]>,hl_up_color:[f32;4],hl_down_color:[f32;4],
    hl_up_y:f32,hl_down_y:f32,source_linear:bool,
) -> Option<Vec<u8>> {
    let source=crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let gradient=grad.and_then(|p|crate::teximg::load_from_memory(p).ok().map(|i|i.to_rgba8()));
    let highlights=[hl_up,hl_down].map(|p|p.and_then(|p|crate::teximg::load_from_memory(p).ok().map(|i|i.to_rgba8())));
    let colors=[hl_up_color,hl_down_color];let offsets=[lerp_f(0.1,-0.1,hl_up_y),lerp_f(0.1,-0.1,hl_down_y)];
    let scale=2.0*(1.0-lerp_f(-1.0,1.0,pupil.grad_scale));let offset=lerp_f(-0.5,0.5,pupil.grad_offset_y);
    let mut result=image::RgbaImage::new(512,512);
    for y in 0..512 {for x in 0..512 {
        let u=(x as f32+0.5)/512.0;let v=(y as f32+0.5)/512.0;
        let main=sample_rgba_clamped_space_reference(&source,u,v,!source_linear);
        // create_eye enables AlphaToMask. With the single-sample create RT,
        // coverage below one half leaves its transparent cleared background.
        if main[3]<0.5 {continue;}
        let g=gradient.as_ref().map_or(1.0,|image|sample_rgba_linear_reference(image,u*scale+0.5-scale*0.5,(v-offset)*scale+0.5-scale*0.5)[0]);
        let mut rgb=[0.0;3];
        for c in 0..3 {
            let tint=lerp_f(srgb_channel_to_linear(pupil.sub_color[c]),srgb_channel_to_linear(pupil.base_color[c]),g);
            let vivid=if main[c]>0.5 {tint/(2.0*(1.0-main[c])).max(0.00001)} else {1.0-(1.0-tint)*0.5/main[c].max(0.00001)};
            let linear=lerp_f(tint*main[c],vivid.clamp(0.0,1.0),pupil.grad_blend)*(main[0]*64.0).clamp(0.0,1.0);
            rgb[c]=linear_channel_to_srgb(linear);
        }
        // Known main_eye/toon_eye draw shaders receive their highlights at
        // draw time. Preserve the legacy fallback only for unknown draw shaders.
        for i in 0..2 {if let Some(highlight)=&highlights[i] {
            let h=sample_rgba_clamped_reference(highlight,u,v-offsets[i]);let a=h[3]*colors[i][3]*main[3];
            for c in 0..3 {rgb[c]=lerp_f(rgb[c],h[c]*colors[i][c],a);}
        }}
        result.put_pixel(x,y,image::Rgba([to_unorm8(rgb[0]),to_unorm8(rgb[1]),to_unorm8(rgb[2]),to_unorm8(main[3])]));
    }}encode_png(&result)
}

#[cfg(test)]
pub(crate) fn sample_rgba_clamped_space_reference(image:&image::RgbaImage,u:f32,v:f32,linear:bool)->[f32;4] {
    let (width,height)=image.dimensions();let x=u*width as f32-0.5;let y=v*height as f32-0.5;
    let x0=x.floor();let y0=y.floor();let fx=x-x0;let fy=y-y0;
    let pixel=|x:f32,y:f32| image.get_pixel(x.clamp(0.,(width-1) as f32) as u32,y.clamp(0.,(height-1) as f32) as u32).0;
    let a=pixel(x0,y0);let b=pixel(x0+1.,y0);let c=pixel(x0,y0+1.);let d=pixel(x0+1.,y0+1.);
    std::array::from_fn(|i| {
        let channel=|p:[u8;4]| {let v=p[i] as f32/255.0;if linear&&i<3 {srgb_channel_to_linear(v)}else{v}};
        lerp_f(lerp_f(channel(a),channel(b),fx),lerp_f(channel(c),channel(d),fx),fy)
    })
}

#[cfg(test)]
pub(crate) fn sample_rgba_linear_reference(image:&image::RgbaImage,u:f32,v:f32)->[f32;4]{sample_rgba_clamped_space_reference(image,u,v,true)}
#[cfg(test)]
pub(crate) fn sample_rgba_clamped_reference(image:&image::RgbaImage,u:f32,v:f32)->[f32;4]{sample_rgba_clamped_space_reference(image,u,v,false)}
pub(crate) fn srgb_channel_to_linear(value:f32)->f32 {
    if value<=0.04045 {value/12.92}else{((value+0.055)/1.055).powf(2.4)}
}
pub(crate) fn linear_channel_to_srgb(value:f32)->f32 {
    if value<=0.0031308 {value*12.92}else{1.055*value.max(0.0).powf(1.0/2.4)-0.055}
}
pub(crate) fn to_unorm8(value:f32)->u8 {(value.clamp(0.0,1.0)*255.0).round() as u8}
/// The 8-bit sRGB -> linear and linear -> sRGB conversions of the texture passes, as tables: the same expressions
/// per byte value, without a powf per channel (a 4K normal map spent ~0.3 s in them).
pub(crate) fn srgb_to_linear_u8(value:u8)->u8 {
    static TABLE:std::sync::OnceLock<[u8;256]>=std::sync::OnceLock::new();
    TABLE.get_or_init(||std::array::from_fn(|v|(srgb_channel_to_linear(v as f32/255.)*255.).round().clamp(0.,255.) as u8))[value as usize]
}
pub(crate) fn linear_to_srgb_u8(value:u8)->u8 {
    static TABLE:std::sync::OnceLock<[u8;256]>=std::sync::OnceLock::new();
    TABLE.get_or_init(||std::array::from_fn(|v|{
        let linear = v as f32 / 255.0;
        let srgb = if linear <= 0.0031308 { linear * 12.92 } else { 1.055 * linear.powf(1.0 / 2.4) - 0.055 };
        (srgb * 255.0).round().clamp(0.0, 255.0) as u8
    }))[value as usize]
}
pub(crate) fn sample_rgba_linear(image:&image::RgbaImage,u:f32,v:f32)->[f32;4] {
    sample_rgba_clamped_space(image,u,v,true)
}

pub(crate) fn composite_over(under: &[u8], over: &[u8]) -> Option<Vec<u8>> {
    use std::io::Cursor;
    let mut dst = crate::teximg::load_from_memory(under).ok()?.to_rgba8();
    let src = crate::teximg::load_from_memory(over).ok()?.to_rgba8();
    let (dw, dh) = dst.dimensions();
    let (sw, sh) = src.dimensions();
    for y in 0..dh {
        for x in 0..dw {
            let sx = x * sw / dw.max(1);
            let sy = y * sh / dh.max(1);
            let p = src.get_pixel(sx.min(sw - 1), sy.min(sh - 1)).0;
            let a = p[3] as f32 / 255.0;
            if a <= 0.0 {
                continue;
            }
            let d = dst.get_pixel_mut(x, y);
            d[0] = (d[0] as f32 * (1.0 - a) + p[0] as f32 * a) as u8;
            d[1] = (d[1] as f32 * (1.0 - a) + p[1] as f32 * a) as u8;
            d[2] = (d[2] as f32 * (1.0 - a) + p[2] as f32 * a) as u8;
            d[3] = d[3].max(p[3]);
        }
    }
    crate::teximg::encode_rgba_owned(dst)
}
