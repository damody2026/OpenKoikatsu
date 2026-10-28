//! Clothes textures of the open: patterns, the create bake (GPU and CPU colour mask), runtime normal maps, over masks and overlay composites.
use super::*;

/// Illusion clothes: MainTex * (Color1*mask.r + Color2*mask.g + Color3*mask.b).
/// A clothes colour channel's pattern (StudioEditor `KoikatsuClothesBake`).
pub(crate) struct ClothesPatternSrc {
    pub(crate) png: Option<image::RgbaImage>,
    pub(crate) gpu: Option<clothes_gpu::Texture>,
    pub(crate) color: [f32; 4],
    pub(crate) tiling: [f32; 2],
    pub(crate) offset: [f32; 2],
    pub(crate) rotate: f32,
    pub(crate) sampling: crate::unity::TextureSampling,
}

pub(crate) fn named_texture_sampling(game:&Path,entry:&ListEntry,ab:&str,names:&[&str])->Option<crate::unity::TextureSampling> {
    let zip=entry.extra.get("__zip").map(Path::new);
    for name in names {
        if sideloader_png(entry.extra.get("__zip").map(String::as_str),game,ab,&[name]).is_some(){
            // Sideloader.LoadTextureFromPNG chooses wrap from the whole PNG path.
            let path=format!("{ab}/{name}");let wrap=if path.contains("clamp"){1}else{0};
            return Some(crate::unity::TextureSampling{wrap_u:wrap,wrap_v:wrap,..Default::default()});
        }
    }
    let read=|parsed:std::sync::Arc<crate::unity::ParsedBundle>| {
        for name in names {
            if !character_hi_poly() {
                if let Some(sampling)=parsed.texture_sampling_named(&format!("{name}_low")){return Some(sampling);}
            }
            if let Some(sampling)=parsed.texture_sampling_named(name){return Some(sampling);}
        }
        None
    };
    zip.and_then(|z|crate::unity::parse_bundle_zip_cached(z,ab)).and_then(read)
        .or_else(||crate::mods::override_bundle_source(&game.join("mods"),ab)
            .and_then(|p|crate::unity::parse_bundle_zip_cached(&p,ab)).and_then(read))
        .or_else(||crate::unity::parse_bundle_path_cached(&game.join("abdata").join(ab)).and_then(read))
}

pub(crate) fn named_texture_gpu(game:&Path,entry:&ListEntry,ab:&str,name:&str)->Option<clothes_gpu::Texture> {
    if sideloader_png(entry.extra.get("__zip").map(String::as_str),game,ab,&[name]).is_some(){return None;}
    let read=|bytes:crate::unity::SourceBytes| {
        let parsed=crate::unity::parse_bundle_ref(&bytes);
        if !character_hi_poly() {if let Some(tex)=parsed.texture_gpu_named(&format!("{name}_low")){return Some(tex);}}
        parsed.texture_gpu_named(name)
    };
    entry.extra.get("__zip").and_then(|z|crate::mods::zipmod_bundle_named(Path::new(z),ab)).and_then(|b|read(b.into()))
        .or_else(||crate::mods::override_bundle_with_source(&game.join("mods"),ab).and_then(|(_,b)|read(b.into())))
        .or_else(||crate::gamefs::read_source(game.join("abdata").join(ab)).ok().and_then(read))
}

pub(crate) fn clothes_gpu_bake(game:&Path,entry:&ListEntry,main_key:&str,mask_key:&str,colors:[[f32;4];4],patterns:&[ClothesPatternSrc])->Result<Vec<u8>,String> {
    let quality=clothes_gpu::quality(game).ok_or("installed quality profile unresolved")?;
    clothes_gpu_bake_with_quality(game,entry,main_key,mask_key,colors,patterns,quality)
}

pub(crate) fn clothes_gpu_bake_with_quality(game:&Path,entry:&ListEntry,main_key:&str,mask_key:&str,colors:[[f32;4];4],patterns:&[ClothesPatternSrc],quality:u32)->Result<Vec<u8>,String> {
    let texture=|key:&str|named_texture_gpu(game,entry,&clothes_tex_ab(entry,clothes_ab_key_for(key)),&entry.info(key)).ok_or_else(||format!("authored GPU texture unresolved: {key}"));
    let t0=Instant::now();let main=texture(main_key)?;let mask=texture(mask_key)?;profile_tick("gpu bake: textures",t0);let t0=Instant::now();
    let pat=|i:usize|match patterns.get(i) {Some(p) if p.png.is_some()=>p.gpu.clone().ok_or_else(||format!("authored GPU pattern unresolved: {i}")),_=>Ok(clothes_gpu::Texture::white())};
    let textures=[pat(0)?,mask,pat(1)?,pat(2)?,main];
    let ab="chara/mm_base.unity3d";
    let read=|bytes:crate::unity::SourceBytes|crate::unity::parse_bundle_ref(&bytes).shader_pixel_dxbc_named("Koikano/create_topN");
    let shader=crate::mods::override_bundle_with_source(&game.join("mods"),ab).and_then(|(_,b)|read(b.into()))
        .or_else(||crate::gamefs::read_source(game.join("abdata").join(ab)).ok().and_then(read)).ok_or("native clothes create PS unresolved")?;profile_tick("gpu bake: shader",t0);let t0=Instant::now();
    let mut cb=[[0.0;4];16];cb[2]=[1.0,1.0,0.0,0.0];cb[7]=cb[2];
    for (i,start) in [3,8,12].into_iter().enumerate() {
        let p=patterns.get(i);let tiling=p.map(|p|p.tiling).unwrap_or([0.0;2]);let offset=p.map(|p|p.offset).unwrap_or([0.5;2]);
        cb[start]=[tiling[0],tiling[1],offset[0],offset[1]];cb[start+1][0]=p.map(|p|p.rotate).unwrap_or(0.5);
        cb[start+2]=p.map(|p|p.color).unwrap_or([1.0;4]);cb[start+3]=colors[i];
        for k in 0..3 {cb[start+2][k]=srgb_channel_to_linear(cb[start+2][k]);cb[start+3][k]=srgb_channel_to_linear(cb[start+3][k]);}
    }
    if !crate::okstrict::assembling() { return crate::unity::solid_png([1.0;4],1,1).ok_or_else(||"solid png".into()); }
    let r=clothes_gpu::bake(&shader,&textures,&cb,quality);profile_tick("gpu bake: bake",t0);r
}

pub(crate) fn clothes_mask_sampling(game:&Path,entry:&ListEntry,key:&str)->crate::unity::TextureSampling {
    let ab=clothes_tex_ab(entry,clothes_ab_key_for(key));
    named_texture_sampling(game,entry,&ab,&[&entry.info(key)]).unwrap_or_default()
}

pub(crate) fn sample_clothes_mask(image:&image::RgbaImage,u:f32,v:f32,sampling:crate::unity::TextureSampling)->[f32;4] {
    let (w,h)=image.dimensions();
    let address=|i:i32,n:u32,mode:i32|->u32 {
        let n=n as i32;
        match mode {
            0=>i.rem_euclid(n) as u32,
            2=>{let p=i.rem_euclid(2*n);(if p<n{p}else{2*n-1-p}) as u32},
            3=>{let p=if i<0{-1-i}else{i};p.min(n-1) as u32},
            _=>i.clamp(0,n-1) as u32,
        }
    };
    let pixel=|x:i32,y:i32|image.get_pixel(address(x,w,sampling.wrap_u),address(y,h,sampling.wrap_v)).0;
    let x=u*w as f32-0.5;let y=v*h as f32-0.5;
    let channel=|p:[u8;4],i:usize|{let value=p[i] as f32/255.0;if sampling.srgb && i<3 {srgb_channel_to_linear(value)}else{value}};
    if sampling.filter==0 {let p=pixel((x+0.5).floor() as i32,(y+0.5).floor() as i32);return std::array::from_fn(|i|channel(p,i));}
    let x0=x.floor() as i32;let y0=y.floor() as i32;let fx=x-x0 as f32;let fy=y-y0 as f32;
    let a=pixel(x0,y0);let b=pixel(x0+1,y0);let c=pixel(x0,y0+1);let d=pixel(x0+1,y0+1);
    std::array::from_fn(|i|lerp_f(lerp_f(channel(a,i),channel(b,i),fx),lerp_f(channel(c,i),channel(d,i),fx),fy))
}

/// `sample_clothes_mask` at the pixel centres of a `w`×`h` target, set up once: each column's and row's texels and
/// weights, and each byte's channel value. The arithmetic and its order are those of sampling pixel by pixel, so the
/// bytes are the same (a card's accessory bakes spent most of a conversion's CPU in the per-pixel set-up).
pub(crate) struct GridSampler<'a> {
    image: &'a image::RgbaImage,
    cols: Vec<(u32, u32, f32)>,
    rows: Vec<(u32, u32, f32)>,
    nearest: bool,
    /// [alpha, colour] channel values per byte.
    lut: [[f32; 256]; 2],
}

impl<'a> GridSampler<'a> {
    fn new(image: &'a image::RgbaImage, w: u32, h: u32, sampling: crate::unity::TextureSampling) -> Self {
        let (iw, ih) = image.dimensions();
        let address = |i: i32, n: u32, mode: i32| -> u32 {
            let n = n as i32;
            match mode {
                0 => i.rem_euclid(n) as u32,
                2 => { let p = i.rem_euclid(2 * n); (if p < n { p } else { 2 * n - 1 - p }) as u32 }
                3 => { let p = if i < 0 { -1 - i } else { i }; p.min(n - 1) as u32 }
                _ => i.clamp(0, n - 1) as u32,
            }
        };
        let nearest = sampling.filter == 0;
        let axis = |n: u32, size: u32, mode: i32| -> Vec<(u32, u32, f32)> {
            (0..n).map(|i| {
                let x = (i as f32 + 0.5) / n as f32 * size as f32 - 0.5;
                if nearest { let p = address((x + 0.5).floor() as i32, size, mode); (p, p, 0.0) }
                else { let x0 = x.floor() as i32; (address(x0, size, mode), address(x0 + 1, size, mode), x - x0 as f32) }
            }).collect()
        };
        let lut = [std::array::from_fn(|v| v as f32 / 255.0), std::array::from_fn(|v| {
            let value = v as f32 / 255.0;
            if sampling.srgb { srgb_channel_to_linear(value) } else { value }
        })];
        GridSampler { image, cols: axis(w, iw, sampling.wrap_u), rows: axis(h, ih, sampling.wrap_v), nearest, lut }
    }

    fn sample(&self, x: u32, y: u32) -> [f32; 4] {
        let (x0, x1, fx) = self.cols[x as usize];
        let (y0, y1, fy) = self.rows[y as usize];
        let channel = |p: [u8; 4], i: usize| self.lut[(i < 3) as usize][p[i] as usize];
        let a = self.image.get_pixel(x0, y0).0;
        if self.nearest { return std::array::from_fn(|i| channel(a, i)); }
        let b = self.image.get_pixel(x1, y0).0;
        let c = self.image.get_pixel(x0, y1).0;
        let d = self.image.get_pixel(x1, y1).0;
        std::array::from_fn(|i| lerp_f(lerp_f(channel(a, i), channel(b, i), fx), lerp_f(channel(c, i), channel(d, i), fx), fy))
    }
}

/// Colour-mask bakes without patterns already made in this process, by their inputs: the outfits of one card bake
/// the same accessories again. Dropped with the bundle caches (between the cards of a batch) or past 512 MiB.
pub(crate) static MASK_BAKES: std::sync::Mutex<(usize, Vec<([u8; 16], Option<std::sync::Arc<Vec<u8>>>)>)> = std::sync::Mutex::new((0, Vec::new()));

pub(crate) fn clear_mask_bakes() { *MASK_BAKES.lock().unwrap() = (0, Vec::new()); }

pub(crate) fn apply_clothes_color_mask(
    main: &[u8],
    mask: &[u8],
    colors: [[f32; 4]; 4],
    patterns: &[ClothesPatternSrc],
) -> Option<Vec<u8>> {
    // Direct accessory shader baking retains its separate colour convention.
    let sampling=crate::unity::TextureSampling{srgb:false,..Default::default()};
    if !patterns.is_empty() { return apply_clothes_color_mask_sampled(main,mask,colors,patterns,sampling,None); }
    let key = {
        use sha2::Digest;
        let mut h = sha2::Sha256::new();
        for part in [main, mask] { h.update((part.len() as u64).to_le_bytes()); h.update(part); }
        for c in colors.iter().flatten() { h.update(c.to_bits().to_le_bytes()); }
        h.update(std::env::var("OK_CLOTHES_MASK").unwrap_or_default().as_bytes());
        let mut id = [0u8; 16];
        id.copy_from_slice(&h.finalize()[..16]);
        id
    };
    if let Some((_, made)) = MASK_BAKES.lock().unwrap().1.iter().find(|(k, _)| *k == key) { return made.as_ref().map(|m| m.to_vec()); }
    let made = apply_clothes_color_mask_sampled(main,mask,colors,patterns,sampling,None);
    let mut bakes = MASK_BAKES.lock().unwrap();
    if bakes.0 > 512 << 20 { *bakes = (0, Vec::new()); }
    bakes.0 += made.as_ref().map_or(0, Vec::len);
    bakes.1.push((key, made.clone().map(std::sync::Arc::new)));
    made
}

pub(crate) fn apply_clothes_color_mask_sampled(
    main:&[u8],mask:&[u8],colors:[[f32;4];4],patterns:&[ClothesPatternSrc],sampling:crate::unity::TextureSampling,main_srgb:Option<bool>,
) -> Option<Vec<u8>> {
    // Koikano/create_topN PS DXBC 0001: three transformed pattern samples,
    // alpha-dependent pattern colour arithmetic, then cascading RGB masks.
    if !crate::okstrict::assembling() { return Some(main.to_vec()); }
    let mask_mode = std::env::var("OK_CLOTHES_MASK").unwrap_or_default();
    let src = crate::teximg::load_from_memory(main).ok()?.to_rgba8();
    let mask = crate::teximg::load_from_memory(mask).ok()?.to_rgba8();
    let (w, h) = src.dimensions();
    let (mw, mh) = mask.dimensions();
    if w == 0 || h == 0 || mw == 0 || mh == 0 {
        return None;
    }
    // Precompute each channel's base colour and pattern sampler.
    let mut base = [[1.0f32; 3]; 3];
    let mut has_pat = [false; 3];
    let mut pat_col = [[1.0f32; 3]; 3];
    let mut pat_scale = [[1.0f32; 2]; 3];
    for c in 0..3 {
        base[c] = [colors[c][0], colors[c][1], colors[c][2]];
        if main_srgb.is_some(){base[c]=base[c].map(srgb_channel_to_linear);}
        if let Some(p) = patterns.get(c) {
            pat_col[c] = [p.color[0], p.color[1], p.color[2]];
            if main_srgb.is_some(){pat_col[c]=pat_col[c].map(srgb_channel_to_linear);}
            let sx = 20.0 - 19.0 * p.tiling[0];
            let sy = 20.0 - 19.0 * p.tiling[1];
            pat_scale[c] = [sx, sy];
            has_pat[c] = p.png.is_some();
        }
    }
    let mut out = src;
    let grid = GridSampler::new(&mask, w, h, sampling);
    let rotations: Vec<(f32, f32)> = patterns.iter().map(|p| ((p.rotate - 0.5) * std::f32::consts::TAU).sin_cos()).collect();
    // Each pixel depends only on its own inputs: rows are baked in parallel (the same bytes as one by one; a heavy
    // accessory's bakes took seconds of an open on one thread).
    use rayon::prelude::*;
    let pixels: &mut [u8] = &mut out;
    pixels.par_chunks_mut(w as usize * 4).enumerate().for_each(|(y, row)| {
        let y = y as u32;
        for x in 0..w {
            let m = grid.sample(x, y);
            let wr = m[0].clamp(0.0, 1.0);
            let wg = m[1].clamp(0.0, 1.0);
            let wb = m[2].clamp(0.0, 1.0);
            let mut ch = [[0f32; 3]; 3];
            for c in 0..3 {
                ch[c] = base[c];
                if has_pat[c] {
                    let pattern = &patterns[c];
                    if let Some(p) = pattern.png.as_ref() {
                        let u=(x as f32+0.5)/w as f32;
                        let v=(y as f32+0.5)/h as f32;
                        let qx=(u+2.0*pattern.offset[0]-1.0)*pat_scale[c][0]-0.5;
                        let qy=(v+2.0*pattern.offset[1]-1.0)*pat_scale[c][1]-0.5;
                        let (sn,cs)=rotations[c];
                        let ps=sample_clothes_mask(p,cs*qx+sn*qy+0.5,
                            -sn*qx+cs*qy+0.5,pattern.sampling);
                        let alpha=pattern.color[3];
                        let avg=((ps[0]+ps[1]+ps[2])/3.0).max(1.0-alpha);
                        let low=if ps[3]>=0.0 && ps[3]<=0.25 {1.0_f32}else{0.0};
                        let mix=(ps[3]*3.0-2.0).clamp(0.0,1.0).max(low).max(1.0-alpha);
                        let original=low.min(alpha);
                        for k in 0..3 {
                            let target=pat_col[c][k]+avg*(base[c][k]-pat_col[c][k])-ps[k]*pat_col[c][k];
                            let tinted=ps[k]*pat_col[c][k]+mix*target;
                            ch[c][k]=tinted+original*(ps[k]-tinted);
                        }
                    }
                }
            }
            let mut tint = [1.0f32; 3];
            if mask_mode == "norm" {
                let weight = wr + wg + wb;
                let inv = 1.0 / weight.max(0.0001);
                let t = weight.clamp(0.0, 1.0);
                for k in 0..3 {
                    let mixed = (ch[0][k] * wr + ch[1][k] * wg + ch[2][k] * wb) * inv;
                    tint[k] = 1.0 * (1.0 - t) + mixed * t;
                }
            } else if mask_mode == "add" {
                for k in 0..3 {
                    tint[k] = (ch[0][k] * wr + ch[1][k] * wg + ch[2][k] * wb)
                        .clamp(0.0, 1.0);
                }
            } else {
                for k in 0..3 {
                    let mut t = 1.0f32;
                    t += (ch[0][k] - t) * wr;
                    t += (ch[1][k] - t) * wg;
                    t += (ch[2][k] - t) * wb;
                    tint[k] = t;
                }
            }
            let p = &mut row[x as usize * 4..x as usize * 4 + 4];
            for k in 0..3 {
                let mut source=p[k] as f32/255.0;
                if main_srgb==Some(true){source=srgb_channel_to_linear(source);}
                let value=source*tint[k];
                p[k]=to_unorm8(if main_srgb.is_some(){linear_channel_to_srgb(value)}else{value});
            }
        }
    });
    encode_png(&out)
}

/// Match the vertical orientation of a Unity Graphics.Blit output. This is
/// shared by the clothes colour bake and MaterialEditor's normal conversion.
pub(crate) fn flip_runtime_blit_v(png: Vec<u8>) -> Vec<u8> {
    crate::teximg::load_from_memory(&png).ok()
        .and_then(|img| {
            let mut im = img.to_rgba8();
            image::imageops::flip_vertical_in_place(&mut im);
            encode_png(&im)
        })
        .unwrap_or(png)
}

/// MaterialEditor's embedded normal_convert shader, Windows PS 4.0:
/// sample _MainTex, write RGB/A as (1, sample.g, sample.b, sample.a).
/// Graphics.Blit also reverses the PNG row direction used by our decoder.
pub(crate) fn convert_runtime_normal_map(png: Vec<u8>) -> Vec<u8> {
    convert_runtime_normal_map_sampled(png,false)
}

pub(crate) fn convert_runtime_normal_map_sampled(png: Vec<u8>,source_srgb:bool) -> Vec<u8> {
    convert_runtime_normal_map_rows(png,source_srgb,true)
}

pub(crate) fn convert_runtime_normal_map_rows(png: Vec<u8>,source_srgb:bool,flip_rows:bool) -> Vec<u8> {let _spot=crate::profile::spot("convert_runtime_normal_map_rows");
    // Same source and switches, same output: outfit switches and shared base normals reuse it (~0.1 s per map).
    png_transform_cache::transform("runtime_normal_v1",&png,&[source_srgb as u8,flip_rows as u8],||Some(convert_runtime_normal_map_rows_uncached(png.clone(),source_srgb,flip_rows))).unwrap_or(png)
}
pub(crate) fn convert_runtime_normal_map_rows_uncached(png: Vec<u8>,source_srgb:bool,flip_rows:bool) -> Vec<u8> {
    crate::teximg::load_from_memory(&png).ok()
        .and_then(|img| {
            let mut im = img.to_rgba8();
            if flip_rows {image::imageops::flip_vertical_in_place(&mut im);}
            for pixel in im.pixels_mut() {
                pixel[0] = 255;
                if source_srgb {
                    for c in 1..3 {pixel[c]=srgb_to_linear_u8(pixel[c]);}
                }
            }
            encode_png(&im)
        })
        .unwrap_or(png)
}

/// MaterialEditorPlugin.ConvertNormalMap allocates a default sRGB RT with
/// automatic mips and preserves the replaced texture's wrapMode. Native shader
/// programs sample that view; the legacy linear-normal adapter is separate.
pub(crate) fn mark_native_runtime_normal_texture(mesh:&mut ExtractedMesh,property:&str) {
    let wrap=mesh.runtime_textures.get(property).map(|s|s[1])
        .or_else(||mesh.mat.as_ref().and_then(|m|m.tex_wraps.get(property).copied())).unwrap_or(0);
    mesh.runtime_textures.insert(property.into(),[1,wrap,wrap,1,1]);
}

pub(crate) fn material_editor_converts_normals(game_root: &Path) -> bool {
    let path = game_root.join("BepInEx/config/com.deathweasel.bepinex.materialeditor.cfg");
    crate::gamefs::read_to_string(path).ok().map(|cfg| {
        cfg.lines().find_map(|line| line.trim().strip_prefix("Convert Normal Maps ="))
            .map(|value| value.trim().eq_ignore_ascii_case("true"))
            .unwrap_or(true)
    // The project-owned standalone source intentionally has no installed
    // BepInEx config. Its supported MaterialEditor semantics use the decompiled
    // Config.Bind default (true), rather than treating absence as explicit false.
    // Keep unmarked installed sources and explicit config values compatible.
    }).unwrap_or_else(|| game_root.join("require_complete_mods").ok_is_file())
}

/// MaterialEditorCharaController.RemoveRim runs after card float overrides.
/// It changes only clothing, hair and accessories with a `_rimV` shader
/// property and only when that exact material/slot has no saved rim override.
pub(crate) fn apply_material_editor_remove_rim(
    remove_rim: bool,
    overrides: Option<&crate::card::MeFloatMap>,
    outfit: i32,
    role: &str,
    property: Option<&str>,
    mesh: &mut ExtractedMesh,
) {
    if !remove_rim { return; }
    let (Some(ot @ (1 | 2 | 3)), Some(slot), Some(mat)) =
        (me_object_type(role), me_slot(role, property), mesh.mat.as_mut()) else { return; };
    if !material_has_rim_property(mat) { return; }
    let name = crate::card::me_mat_key(&mat.name);
    let has_card_value = overrides.is_some_and(|map| map.keys().any(|(t, coord, s, material, prop)|
        *t == ot && *coord == outfit && *s == slot && *material == name
            && prop.trim_start_matches('_').eq_ignore_ascii_case("rimv")));
    if !has_card_value { mat.floats.insert("_rimV".into(), 0.0); }
}

pub(crate) fn material_editor_removes_rim(game_root: &Path) -> bool {
    if !game_root.join("BepInEx/plugins/KKS_Plugins/KKS_MaterialEditor.dll").ok_is_file()
        && !game_root.join("BepInEx/plugins/KKS_MaterialEditor.dll").ok_is_file() { return false; }
    let path = game_root.join("BepInEx/config/com.deathweasel.bepinex.materialeditor.cfg");
    crate::gamefs::read_to_string(path).ok().and_then(|cfg| {
        cfg.lines().find_map(|line| line.trim().strip_prefix("Remove Rim Lighting =")
            .map(|value| value.trim().eq_ignore_ascii_case("true")))
    }).unwrap_or(false)
}

/// Prefer the referenced Unity Shader's serialized property declaration,
/// which is the source of Material.HasProperty even for unsaved default values.
/// Some replacement shaders are external to the material bundle; for those,
/// use the installed MaterialEditor shader declarations or a serialized float.
pub(crate) fn material_has_rim_property(mat: &crate::unity::MeshMaterial) -> bool {
    if let Some(has_rim) = mat.shader_has_rim { return has_rim; }
    if mat.float("_rimV").is_some() { return true; }
    matches!(mat.shader.to_ascii_lowercase().as_str(),
        "koikano/main_clothes_item_glasses"
        | "koikano/hair_main_sun"
        | "koikano/hair_main_sun_front"
        | "shader forge/main_item_emission"
        | "shader forge/main_item_studio_alpha"
        | "shader forge/main_alpha"
        | "shader forge/main_hair"
        | "shader forge/main_hair_front"
        | "shader forge/main_item"
        | "shader forge/main_item_studio"
        | "shader forge/main_opaque"
        | "shader forge/main_opaque2"
        | "shader forge/main_skin")
}

/// The game's clothes albedo is a runtime RenderTexture produced by
/// `cf_m_clothesN_create`; Unity's blit writes it vertically flipped relative
/// to the source `MainTex`, and the renderer samples the RT with the mesh UV.
/// Our CPU bake has the source orientation, so flip V to match the game.
pub(crate) fn finalize_clothes_texture(meshes: &mut [ExtractedMesh]) {
    if std::env::var("OK_CLOTH_NOFLIP").is_ok() {
        return;
    }
    // Each mesh's own texture (decode, flip, encode): all at once.
    use rayon::prelude::*;
    crate::session::native_parallel_work(|| meshes.par_iter_mut().for_each(|m| {
        if let Some(png) = m.texture_png.take() {
            m.texture_png = Some(flip_runtime_blit_v(png));
        }
    }));
}

/// Load the three colour channels' pattern textures (cat 430) for a clothes part.
pub(crate) fn pattern_resolve_matches(r:&crate::card::ResolveInfo,outfit:i32,part:usize,channel:usize,id:i32)->bool {
    let Some(kind)=["Top","Bot","Bra","Shorts","Gloves","Pants","Socks","ShoesInner","ShoesOuter"].get(part) else{return false};
    r.category_no==430 && (r.slot==id||r.local_slot==id)
        && r.property==format!("outfit{outfit}.ChaFileClothes.Clothes{kind}Pattern{channel}")
}

pub(crate) fn build_clothes_patterns(
    game_root: &Path,
    vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog,
    card: &CardInfo,
    guid_index: &std::collections::HashMap<String,PathBuf>,
    part: usize,
    ch0: usize,
    head_texs: &[(String, Vec<u8>)],
    body_texs: &[(String, Vec<u8>)],
    outfit: i32,
) -> Vec<ClothesPatternSrc> {
    // `ch0` is the `ColorInfo` index feeding shader channel 1 (`_Color`).
    // `ChaControl.ChangeCustomClothes`: main clothes and sub-kinds A/B use
    // `colorInfo[0]`; sub-kind C (`!main && 2 == kind`) uses `colorInfo[3]`.
    let pats = card.clothes_patterns.get(part);
    let idx = [ch0, 1, 2];
    let mut out = Vec::with_capacity(3);
    for c in 0..3 {
        let (id, color, tiling, offset, rotate) = pats
            .map(|p| {
                let e = &p[idx[c]];
                (e.pattern, e.pattern_color, e.tiling, e.offset, e.rotate)
            })
            .unwrap_or((0, [1.0, 1.0, 1.0, 1.0], [0.0, 0.0], [0.5,0.5], 0.5));
        let mut pattern_sampling=crate::unity::TextureSampling::default();
        let mut gpu=None;
        let png = if id != 0 {
            let mut zip_lists=std::collections::HashMap::new();
            let entry=if let Some(resolve)=card.resolves.iter().find(|r|
                pattern_resolve_matches(r,outfit,part,idx[c],id)) {
                lookup_entry(vanilla,&mut zip_lists,guid_path(guid_index,&resolve.mod_id).map(|p|p.as_path()),
                    &resolve.mod_id,430,resolve.slot,resolve.local_slot)
            }else{vanilla.get(430,id).or_else(||mod_catalog.get(430,id)).cloned()};
            entry.and_then(|entry| {
                // ChangeCustomClothes uses MainTexAB, independently of MainAB.
                let ab=entry.info("MainTexAB");let name=entry.info("MainTex");
                if ab.is_empty() || ab=="0" || name.is_empty() || name=="0" {return None;}
                pattern_sampling=named_texture_sampling(game_root,&entry,&ab,&[&name]).unwrap_or_default();
                gpu=named_texture_gpu(game_root,&entry,&ab,&name);
                let name=if character_hi_poly(){name}else{format!("{name}_low")};
                png_from_bundle(game_root,&ab,entry.extra.get("__zip").map(|p|p.as_str()),
                    &[name.as_str()],head_texs,body_texs)
            })
            .and_then(|b| crate::teximg::load_from_memory(&b).ok())
            .map(|i| i.to_rgba8())
        } else {
            None
        };
        if std::env::var("OK_PATDIAG").is_ok() && id != 0 {
            eprintln!(
                "ok-extract: clothes pattern part={part} ch={c} id={id} tiling={tiling:?} loaded={}",
                png.is_some()
            );
        }
        out.push(ClothesPatternSrc { png, gpu, color, tiling, offset, rotate, sampling:pattern_sampling });
    }
    out
}

/// Studio item patterns (OIItemInfo patterns: the character list's mt_pattern, category 430, as clothes patterns):
/// (id, name) of every pattern, from `root` (a game root, or the OK base library replaying one).
pub fn studio_patterns(root: &Path) -> Vec<(i32, String)> {
    let vanilla = load_characustom_dir(&root.join("abdata/list/characustom"));
    let mut out: Vec<(i32, String)> = vanilla.entries().filter(|e| e.category_no == 430 && e.id > 0).map(|e| (e.id, e.name.clone())).collect();
    out.sort();
    out
}

/// A Studio item pattern's mask texture as PNG. Studio takes the list's thumbnail without its "thumb/" folder and
/// "thumb_" prefix (OCIItem.SetPatternTex); MainTexAB / MainTex (clothes patterns) when that gives nothing.
pub fn studio_pattern_png(root: &Path, id: i32) -> Option<Vec<u8>> {
    let vanilla = load_characustom_dir(&root.join("abdata/list/characustom"));
    let entry = vanilla.get(430, id)?.clone();
    let zip = entry.extra.get("__zip").map(|p| p.as_str());
    let usable = |s: &str| !s.is_empty() && s != "0";
    let (thumb_ab, thumb) = (entry.info("ThumbAB").replace("thumb/", ""), entry.info("ThumbTex").replace("thumb_", ""));
    if usable(&thumb_ab) && usable(&thumb) {
        if let Some(png) = png_from_bundle(root, &thumb_ab, zip, &[thumb.as_str()], &[], &[]) { return Some(png); }
    }
    let (ab, name) = (entry.info("MainTexAB"), entry.info("MainTex"));
    if !usable(&ab) || !usable(&name) { return None; }
    png_from_bundle(root, &ab, zip, &[name.as_str()], &[], &[])
}

/// Load the `OverBodyMask` of a clothes list entry (its own `OverBodyMaskAB`
/// bundle), matching `ChaControl.LoadAlphaMaskTexture(..., type 0)`.
pub(crate) fn load_over_clothes_mask(game_root: &Path, e: &ListEntry, key: &str) -> Option<Vec<u8>> {
    let name = e.info(key);
    let ab = e.info(&format!("{key}AB"));
    if name.is_empty() || name == "0" || ab.is_empty() || ab == "0" {
        return None;
    }
    let low = format!("{name}_low");
    let names: Vec<&str> = if character_hi_poly() { vec![&name] } else { vec![&low, &name] };
    // Mod bundles live inside the zipmod, not on disk; use the entry's source zip.
    let zip = e.extra.get("__zip").map(String::as_str);
    if let Some(zip) = zip {
        if let Some(bytes) = crate::mods::zipmod_bundle_named(Path::new(zip), &ab) {
            if let Some((_, b)) = crate::unity::named_png_from_bytes(bytes, &names) {
                return Some(b);
            }
        }
    }
    // Sideloader serves one abdata from every zipmod: the mask bundle may come from another mod than the item (a
    // sweater whose masks ship in a shared pack), or the mask be a png it overrides. `--mask-report` found these missed.
    if let Some(b) = sideloader_png(zip, game_root, &ab, &names) {
        return Some(b);
    }
    if let Some((_, b)) = crate::mods::override_bundle(&game_root.join("mods"), &ab).and_then(|b| crate::unity::named_png_from_bytes(b, &names)) {
        return Some(b);
    }
    let found = png_from_ab(game_root, &ab, &names, &[]);
    if found.is_none() {
        eprintln!("ok-extract: {key} {name} not found in {ab} (zip={:?})", e.extra.get("__zip"));
    }
    found
}

/// `--mask-report`: every clothes mask a card's outfits ask for (OverBodyMask, OverBraMask, OverInnerMask) and where
/// it can be found, one line each. `ok`: the extraction finds it. `ours`: it exists (a Sideloader png, or the bundle in
/// another zipmod) but the extraction does not look there, which is ours to fix. `mod`: no source has it (the mod ships
/// without it; the game cannot load it either). `none`: the item asks for no mask.
pub fn mask_report(game_root: &Path, card_path: &Path) -> Result<Vec<String>, String> {
    let bytes = std::fs::read(card_path).map_err(|e| format!("read {}: {e}", card_path.display()))?;
    let count = crate::card::parse_card_bytes_mode(&bytes, 0, false).map(|c| c.coordinate_count).unwrap_or(1).max(1);
    let vanilla = load_characustom_dir(&game_root.join("abdata/list/characustom"));
    let mods_dir = game_root.join("mods");
    let mod_catalog = crate::list::load_mods_catalog_shared(&mods_dir);
    let mut rows = Vec::new();
    for outfit in 0..count as i32 {
        let card = parse_card_outfit(card_path, outfit).map_err(|e| format!("parse {} outfit {outfit}: {e}", card_path.display()))?;
        let wanted: std::collections::HashSet<String> = card.resolves.iter().map(|r| r.mod_id.clone()).collect();
        let guids = if wanted.is_empty() { Default::default() } else { crate::mods::find_guids(&mods_dir, Some(&wanted)) };
        for key in ["OverBodyMask", "OverBraMask", "OverInnerMask"] {
            let kind = match key { "OverBodyMask" => "BodyMask", "OverBraMask" => "BraMask", _ => "InnerMask" };
            let Some(entry) = active_clothes_mask_entry(&card, &vanilla, &mod_catalog, &guids, outfit, key) else { continue };
            let (name, ab) = (entry.info(key), entry.info(&format!("{key}AB")));
            let item = format!("{} outfit {outfit} {key}", card_path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default());
            if card.clothes_masks.contains_key(&(outfit, kind.to_string())) { rows.push(format!("ok   {item}: in the card")); continue; }
            if name.is_empty() || name == "0" || ab.is_empty() || ab == "0" { rows.push(format!("none {item}")); continue; }
            let low = format!("{name}_low");
            let names: Vec<&str> = if character_hi_poly() { vec![&name] } else { vec![&low, &name] };
            let zip = entry.extra.get("__zip").map(String::as_str);
            // The extraction now searches every source below too; a source it still misses would be "ours".
            let source = if load_over_clothes_mask(game_root, &entry, key).is_some() { Some(("ok  ", "found")) }
                else if sideloader_png(zip, game_root, &ab, &names).is_some() { Some(("ours", "Sideloader png")) }
                else if crate::mods::override_bundle(&mods_dir, &ab).and_then(|b| crate::unity::named_png_from_bytes(b, &names)).is_some() { Some(("ours", "bundle in another zipmod")) }
                else { None };
            rows.push(match source {
                Some((tag, what)) => format!("{tag} {item}: {name} in {ab} ({what})"),
                None => format!("mod  {item}: {name} in {ab} found nowhere (zip={})", zip.unwrap_or("-")),
            });
        }
    }
    Ok(rows)
}

pub(crate) fn set_material_color(mesh: &mut ExtractedMesh, property: &str, value: [f32; 4]) {
    if let Some(mat) = mesh.mat.as_mut() {
        mat.colors.retain(|name, _| !name.eq_ignore_ascii_case(property));
        mat.colors.insert(property.to_string(), value);
    }
}

// ChaControl.ChangeSettingLip/EyeShadow and ChangeHohoAkaRate operate on
// matDraw after CreateFaceTexture. MaterialEditor then overrides that material.
pub(crate) fn apply_native_face_overlays(game: &Path, vanilla: &ChaListCatalog, mods: &ChaListCatalog,
    card: &CardInfo, outfit: i32, mesh: &mut ExtractedMesh) {
    if !mesh.mat.as_ref().is_some_and(|m|matches!(m.shader.to_ascii_lowercase().as_str(),"koikano/main_skin"|"shader forge/main_skin"|"xukmi/skinplus")) {return;}
    let mut hoho=mesh.mat.as_ref().and_then(|m|m.color("_overcolor2")).unwrap_or([1.;4]);
    hoho[3]=0.2*card.hoho_aka_rate.clamp(0.,1.);
    if crate::gamefs::read_to_string(game.join("UserData/config/system.xml")).ok()
        .is_some_and(|s|s.contains("<hohoAka>false</hohoAka>")) {hoho[3]=0.;}
    for (slot,color) in [("_overcolor1",card.lip_color),("_overcolor2",hoho),("_overcolor3",card.eyeshadow_color)] {
        let value=me_material_color(Some(&card.me_color),outfit,"head",None,mesh,slot.trim_start_matches('_')).unwrap_or(color);
        set_material_color(mesh,slot,value);
    }
    // This existing prefab texture is authored Texture2D data. Unlike the
    // catalog display helper it has not crossed the runtime PNG row boundary.
    if let Some(png)=mesh.shader_extra_textures.remove("_overtex2") {
        mesh.shader_extra_textures.insert("_overtex2".into(),flip_png_v(&png).unwrap_or(png));
    }
    for (slot,category,id,key) in [("_overtex1",403,card.lip_id,"LipTex"),("_overtex3",401,card.eyeshadow_id,"EyeshadowTex")] {
        mesh.shader_extra_textures.remove(slot);mesh.runtime_textures.remove(slot);
        if let Some((png,settings))=load_catalog_display_texture(game,vanilla,mods,category,id,key) {
            mesh.shader_extra_textures.insert(slot.into(),png);
            if let Some(settings)=settings {mesh.runtime_textures.insert(slot.into(),settings);}
        }
    }
    for slot in ["_overtex1","_overtex2","_overtex3"] {
        if let Some(png)=me_texture(Some(&card.me_tex),outfit,"head",None,mesh,slot.trim_start_matches('_')).map(|p|p.to_vec()) {
            mesh.shader_extra_textures.insert(slot.into(),png);mark_material_editor_texture(mesh,slot);
        }
    }
}

pub(crate) fn load_over_body_mask(game_root: &Path, e: &ListEntry) -> Option<Vec<u8>> {
    load_over_clothes_mask(game_root, e, "OverBodyMask")
}

/// `ChangeClothesTopPartsAsync` assigns the subpart A mask to the bra and
/// subpart B mask to the inner renderer. Both are loaded from the equipped
/// list entry, not the underlying bra/inner material's prefab texture.
pub(crate) fn active_clothes_mask_entry(
    card: &CardInfo, vanilla: &ChaListCatalog, mod_catalog: &ChaListCatalog,
    guid_index: &std::collections::HashMap<String,PathBuf>, outfit: i32, key: &str,
) -> Option<ListEntry> {
    let selected=select_card_clothes(card,outfit,vanilla,mod_catalog,guid_index);
    let top=selected.first()?.as_ref()?;
    let top_type=top.0.info("Kind").parse().unwrap_or(0);
    let subparts=select_card_top_subparts(card,outfit,top_type,vanilla,mod_catalog,guid_index);
    let sub_index=if key=="OverInnerMask" {1}else{0};
    Some(subparts.get(sub_index).and_then(Option::as_ref)
        .map(|(_,entry,_,_)|entry).unwrap_or(&top.0).clone())
}

pub(crate) fn over_layer_mask_png(
    game_root: &Path, card: &CardInfo, vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog, guid_index: &std::collections::HashMap<String,PathBuf>,
    outfit: i32, key: &str,
) -> Option<Vec<u8>> {let _spot=crate::profile::spot("over_layer_mask_png");
    // ChangeClothesTop loads its list mask first. Only Kind 1/2 loads SubA/B
    // afterwards, replacing that mask. IDs/resolves retained from another
    // style do not load subparts, even when their ID zero is a valid item.
    let entry=active_clothes_mask_entry(card,vanilla,mod_catalog,guid_index,outfit,key)?;
    let mask_kind=match key {"OverBodyMask"=>"BodyMask","OverBraMask"=>"BraMask","OverInnerMask"=>"InnerMask",_=>""};
    if let Some(png)=card.clothes_masks.get(&(outfit,mask_kind.to_string())) {return flip_png_v(png);}
    load_over_clothes_mask(game_root,&entry,key)
}

/// ChaFileDefine.alphaState toggles the two _AlphaMask channels. Clothes
/// combine them with MainTex.a; Koikano/main_skin clips the channels alone.
pub(crate) fn clothes_alpha_state(top_state: u8) -> [bool; 2] {
    // ChaFileDefine.alphaState[state, 0..2], used by ChangeAlphaMask.
    match top_state {
        0 => [true, true],
        1 | 2 => [false, true],
        _ => [false, false],
    }
}

pub(crate) fn apply_native_clothes_over_mask(mesh: &mut ExtractedMesh, mask: &[u8],
    v_scale: f32, v_offset: f32, enabled: [bool; 2]) {
    // Catalog textures are Unity bottom-origin; native shader PNG bindings
    // use top-origin rows, matching the existing CPU mask sampling contract.
    mesh.alpha_mask_png = flip_png_v(mask);
    mesh.runtime_textures.remove("_AlphaMask");
    if let Some(mat) = mesh.mat.as_mut() {
        for (key, value) in [("_AlphaMask_ST_x", 1.), ("_AlphaMask_ST_y", v_scale),
            ("_AlphaMask_ST_z", 0.), ("_AlphaMask_ST_w", v_offset),
            ("_alpha_a", if enabled[0] {1.} else {0.}),
            ("_alpha_b", if enabled[1] {1.} else {0.})] {
            mat.floats.retain(|name, _| !name.eq_ignore_ascii_case(key));
            mat.floats.insert(key.into(), value);
        }
    }
}

#[test]
pub(crate) fn native_clothes_dynamic_mask_matches_cpu_rows_and_keeps_main_texture() {
    let mut mask = image::RgbaImage::new(2, 2);
    mask.put_pixel(0,0,image::Rgba([0,255,255,255]));
    mask.put_pixel(1,0,image::Rgba([255,0,255,255]));
    mask.put_pixel(0,1,image::Rgba([255,255,255,255]));
    mask.put_pixel(1,1,image::Rgba([128,255,255,255]));
    let mask = encode_png(&mask).unwrap();
    let main = crate::unity::solid_png([1.;4],2,2).unwrap();
    for state in 0..4 {
        let flags=clothes_alpha_state(state);
        let mut mesh=ExtractedMesh {mat:Some(crate::unity::MeshMaterial {
            shader:"Shader Forge/main_opaque".into(),..Default::default()}),
            texture_png:Some(main.clone()),..Default::default()};
        apply_native_clothes_over_mask(&mut mesh,&mask,1.,0.,flags);
        let part=crate::session::SessionPart::from_mesh("clothes_top",&mesh,"masked",true);
        assert_eq!(part.shader_textures["_MainTex"],main);
        assert_eq!(part.alpha_mask_st,[1.,1.,0.,0.]);
        let native=crate::teximg::load_from_memory(&part.shader_textures["_AlphaMask"]).unwrap().to_rgba8();
        let cpu=crate::teximg::load_from_memory(&apply_clothes_over_mask(&main,&mask,1.,0.,flags).unwrap()).unwrap().to_rgba8();
        for (a,b) in native.pixels().zip(cpu.pixels()) {
            assert_eq!(masked_alpha(255,[a[0],a[1]],flags),b[3]);
        }
        let mat=part.source_material.unwrap();
        assert_eq!(mat.float("_alpha_a"),Some(if flags[0] {1.}else{0.}));
        assert_eq!(mat.float("_alpha_b"),Some(if flags[1] {1.}else{0.}));
    }
}

pub(crate) fn masked_alpha(alpha: u8, mask: [u8; 2], enabled: [bool; 2]) -> u8 {
    let red = if enabled[0] { mask[0] } else { 255 };
    let green = if enabled[1] { mask[1] } else { 255 };
    alpha.min(red).min(green)
}

pub(crate) fn apply_clothes_over_mask(png: &[u8], mask_png: &[u8], v_scale: f32, v_offset: f32, enabled: [bool; 2]) -> Option<Vec<u8>> {
    let mut parameters=Vec::with_capacity(mask_png.len()+10);
    parameters.extend_from_slice(&v_scale.to_le_bytes());parameters.extend_from_slice(&v_offset.to_le_bytes());
    parameters.extend([enabled[0] as u8,enabled[1] as u8]);parameters.extend_from_slice(mask_png);
    png_transform_cache::transform("clothes_over_mask_v1",png,&parameters,||apply_clothes_over_mask_uncached(png,mask_png,v_scale,v_offset,enabled))
}
pub(crate) fn apply_clothes_over_mask_uncached(png: &[u8], mask_png: &[u8], v_scale: f32, v_offset: f32, enabled: [bool; 2]) -> Option<Vec<u8>> {
    let mut main = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let mask = crate::teximg::load_from_memory(mask_png).ok()?.to_rgba8();
    let (w,h) = main.dimensions();
    let (mw,mh) = mask.dimensions();
    if w == 0 || h == 0 || mw == 0 || mh == 0 { return None; }
    for y in 0..h {
        let v = ((y as f32 + 0.5) / h as f32 * v_scale + v_offset).rem_euclid(1.0);
        let my = ((1.0 - v) * mh as f32).floor().min((mh-1) as f32) as u32;
        for x in 0..w {
            let mx = (x as u64 * mw as u64 / w as u64) as u32;
            let m = mask.get_pixel(mx.min(mw-1),my).0;
            let p = main.get_pixel_mut(x,y);
            p[3] = masked_alpha(p[3], [m[0], m[1]], enabled);
        }
    }
    encode_png(&main)
}

/// Shader Forge/main_hair and main_hair_front discard when
/// `_MainTex.a * _AlphaMask.r < 0.5`. Material Editor can supply the mask even
/// when the prefab material's `_AlphaMask` slot is empty. Both decoded PNGs
/// use the same top-origin row convention, so combine corresponding texels.
pub(crate) fn apply_hair_alpha_mask(png: &[u8], mask_png: &[u8]) -> Option<Vec<u8>> {let _spot=crate::profile::spot("apply_hair_alpha_mask");
    png_transform_cache::transform("hair_alpha_mask_v1",png,mask_png,||apply_hair_alpha_mask_uncached(png,mask_png))
}
pub(crate) fn apply_hair_alpha_mask_uncached(png: &[u8], mask_png: &[u8]) -> Option<Vec<u8>> {
    let mut main = crate::teximg::load_from_memory(png).ok()?.to_rgba8();
    let mask = crate::teximg::load_from_memory(mask_png).ok()?.to_rgba8();
    let (w, h) = main.dimensions();
    let (mw, mh) = mask.dimensions();
    if w == 0 || h == 0 || mw == 0 || mh == 0 { return None; }
    for y in 0..h {
        let my = (y as u64 * mh as u64 / h as u64).min((mh - 1) as u64) as u32;
        for x in 0..w {
            let mx = (x as u64 * mw as u64 / w as u64).min((mw - 1) as u64) as u32;
            let alpha = &mut main.get_pixel_mut(x, y)[3];
            *alpha = ((*alpha as u16 * mask.get_pixel(mx, my)[0] as u16 + 127) / 255) as u8;
        }
    }
    encode_png(&main)
}

/// Body `_AlphaMask` texture supplied by the equipped top / inner sub-clothes
/// (`ChaControl.cs:5968/6245/9444`). Port of StudioEditor `KoikatsuBodyMaskBake`:
///   body.a *= lerp(1, mask.r, chR) * lerp(1, mask.g, chG)  with chR=chG=1.
pub(crate) fn over_body_mask_png(
    game_root: &Path, card: &CardInfo, vanilla: &ChaListCatalog,
    mod_catalog: &ChaListCatalog, guid_index: &std::collections::HashMap<String,PathBuf>,
    outfit: i32,
) -> Option<Vec<u8>> {
    over_layer_mask_png(game_root,card,vanilla,mod_catalog,guid_index,outfit,"OverBodyMask")
}

/// ChaControl's alpha_a/alpha_b are binary outfit-state switches. Whiten
/// disabled channels, then let the GPU sample and min the two channels at the
/// body's authored UV, matching Koikano/main_skin t0 and avoiding a MainTex
/// alpha bake that a later Material Editor override can erase.
/// The face's draw blend: "masked" when its _AlphaMask (with the material's _alpha_a/_alpha_b) can clip anything.
pub(crate) fn face_alpha_blend(mesh: &mut ExtractedMesh) -> &'static str {
    let Some(mask) = mesh.alpha_mask_png.take() else { return "opaque" };
    let enabled = ["_alpha_a", "_alpha_b"].map(|key| mesh.mat.as_ref().and_then(|m| m.float(key)).unwrap_or(1.0) != 0.0);
    mesh.alpha_mask_png = if enabled == [false, false] { None } else { body_mask_for_alpha_state(&mask, enabled) };
    if mesh.alpha_mask_png.is_some() { "masked" } else { "opaque" }
}

pub(crate) fn body_mask_for_alpha_state(mask_png: &[u8], enabled: [bool; 2]) -> Option<Vec<u8>> {
    if enabled == [true, true] { return Some(mask_png.to_vec()); }
    png_transform_cache::transform("body_alpha_state_v1",mask_png,&[enabled[0] as u8,enabled[1] as u8],||body_mask_for_alpha_state_uncached(mask_png,enabled))
}
pub(crate) fn body_mask_for_alpha_state_uncached(mask_png: &[u8], enabled: [bool; 2]) -> Option<Vec<u8>> {
    if enabled == [true, true] { return Some(mask_png.to_vec()); }
    let mut mask = crate::teximg::load_from_memory(mask_png).ok()?.to_rgba8();
    for pixel in mask.pixels_mut() {
        if !enabled[0] { pixel[0] = 255; }
        if !enabled[1] { pixel[1] = 255; }
    }
    encode_png(&mask)
}

/// KSOX skin overlay for (outfit, kind); mirrors `KoikatsuSkinOverlayData.TryGetTexture`
/// (use the outfit's coordinate, else coordinate 0, else the first).
pub(crate) fn ksox_overlay_png(card: &CardInfo, outfit: i32, kind: i32) -> Option<Vec<u8>> {
    if card.ksox.is_empty() {
        return None;
    }
    let has = |c: i32| card.ksox.keys().any(|(cc, _)| *cc == c);
    let coord = if has(outfit) {
        outfit
    } else if has(0) {
        0
    } else {
        card.ksox.keys().next().map(|(c, _)| *c)?
    };
    card.ksox.get(&(coord, kind)).cloned()
}

pub(crate) fn sample_rgba_clamped(image:&image::RgbaImage,u:f32,v:f32)->[f32;4] {
    sample_rgba_clamped_space(image,u,v,false)
}
pub(crate) fn sample_rgba_clamped_space(image:&image::RgbaImage,u:f32,v:f32,linear:bool)->[f32;4] {
    static TRANSFER:std::sync::OnceLock<[f32;256]>=std::sync::OnceLock::new();
    let transfer=TRANSFER.get_or_init(||std::array::from_fn(|i|srgb_channel_to_linear(i as f32/255.0)));
    let (width,height)=image.dimensions();let x=u*width as f32-0.5;let y=v*height as f32-0.5;
    let x0=x.floor();let y0=y.floor();let fx=x-x0;let fy=y-y0;
    let pixel=|x:f32,y:f32| image.get_pixel(x.clamp(0.,(width-1) as f32) as u32,y.clamp(0.,(height-1) as f32) as u32).0;
    let a=pixel(x0,y0);let b=pixel(x0+1.,y0);let c=pixel(x0,y0+1.);let d=pixel(x0+1.,y0+1.);
    std::array::from_fn(|i| {
        let channel=|p:[u8;4]| {let v=p[i] as f32/255.0;if linear&&i<3 {transfer[p[i] as usize]}else{v}};
        lerp_f(lerp_f(channel(a),channel(b),fx),lerp_f(channel(c),channel(d),fx),fy)
    })
}

/// KSOX PS lerps RGB in linear space and emits max(base.a, overlay.a).
/// Its SrcAlpha/OneMinusSrcAlpha pass blends back over the existing RT.
/// KSOX face/body/eye hooks all use this same composite material and pass.
pub(crate) fn composite_overlay(base:&[u8],overlay:&[u8])->Option<Vec<u8>> {
    composite_overlay_space(base,overlay,true,true)
}

pub(crate) fn composite_overlay_space(base:&[u8],overlay:&[u8],base_srgb:bool,output_srgb:bool)->Option<Vec<u8>> {
    if !crate::okstrict::assembling() { return Some(base.to_vec()); }
    let mut parameters=Vec::with_capacity(overlay.len()+2);parameters.extend([base_srgb as u8,output_srgb as u8]);parameters.extend_from_slice(overlay);
    png_transform_cache::transform("composite_overlay_v1",base,&parameters,||composite_overlay_space_uncached(base,overlay,base_srgb,output_srgb))
}
pub(crate) fn composite_overlay_space_uncached(base:&[u8],overlay:&[u8],base_srgb:bool,output_srgb:bool)->Option<Vec<u8>> {
    let mut dst=crate::teximg::load_from_memory(base).ok()?.to_rgba8();let over=crate::teximg::load_from_memory(overlay).ok()?.to_rgba8();
    let (w,h)=dst.dimensions();if w==0||h==0||over.width()==0||over.height()==0{return None;}
    for y in 0..h {for x in 0..w {
        let p=sample_rgba_linear(&over,(x as f32+0.5)/w as f32,(y as f32+0.5)/h as f32);
        let d=dst.get_pixel_mut(x,y);let alpha=d[3] as f32/255.0;let source_alpha=alpha.max(p[3]);
        for c in 0..3 {
            let base=d[c] as f32/255.;let base=if base_srgb {srgb_channel_to_linear(base)}else{base};
            let value=lerp_f(base,p[c],p[3]*source_alpha);
            d[c]=to_unorm8(if output_srgb {linear_channel_to_srgb(value)}else{value});
        }
        d[3]=to_unorm8(lerp_f(alpha,source_alpha,source_alpha));
    }}encode_png(&dst)
}
