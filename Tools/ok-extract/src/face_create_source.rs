//! Catalog/material inputs for the native face create shader. Source kind,
//! not mouth pixels or the character name, determines texture row conversion.
use crate::{card::CardInfo, list::{ChaListCatalog, ListEntry}, face_create::{self, Inputs, Texture},
            unity::{MeshMaterial, TextureSampling}};
use std::{collections::HashMap, path::{Path, PathBuf}};

struct ImageSource { image: image::RgbaImage, sampling: TextureSampling, gpu: crate::clothes_gpu::Texture }
impl ImageSource {
    fn texture(&self, st: [f32; 4]) -> Texture<'_> {
        Texture { image: &self.image, sampling: self.sampling, st }
    }
}

fn source(game: &Path, entry: &ListEntry, ab: &str, name: &str) -> Option<ImageSource> {
    if name.is_empty() || name == "0" || ab.is_empty() || ab == "0" { return None; }
    let name = if super::character_hi_poly() { name.to_string() } else { format!("{name}_low") };
    let png = super::png_in_runtime_row_order(game, entry, ab, &[&name]).or_else(||{eprintln!("face-create missing PNG {ab} {name}");None})?;
    let sampling = super::named_texture_sampling(game, entry, ab, &[&name]).or_else(||{eprintln!("face-create missing sampler {ab} {name}");None})?;
    let gpu=if super::sideloader_png(entry.extra.get("__zip").map(String::as_str),game,ab,&[&name]).is_some() {
        crate::face_create_gpu::png_texture(&png,sampling)?
    } else {super::named_texture_gpu(game,entry,ab,&name).or_else(||{eprintln!("face-create missing GPU payload {ab} {name}");None})?};
    Some(ImageSource { image: crate::teximg::load_from_memory(&png).ok()?.to_rgba8(), sampling:gpu.sampling, gpu })
}

fn entry_ab(entry: &ListEntry, key: &str) -> String {
    // SetCreateTexture uses the exact list column. In particular AB=0
    // clears this slot; it cannot borrow the head prefab's MainAB.
    entry.info(key)
}

fn st(material: &MeshMaterial, slot: &str) -> [f32; 4] {
    std::array::from_fn(|i| material.float(&format!("{slot}_ST_{}", ["x","y","z","w"][i]))
        .unwrap_or(if i < 2 { 1. } else { 0. }))
}

pub(crate) fn compose(game: &Path, vanilla: &ChaListCatalog, mods: &ChaListCatalog,
    card: &CardInfo, skin: [f32; 4], skin_sub: [f32; 4], outfit: i32,
    guids: &HashMap<String, PathBuf>) -> Option<Vec<u8>> {
    let mut lists = HashMap::new();
    let entry = super::resolve_face_entry(vanilla, card, guids, &mut lists)?;
    let main_name = entry.main_tex.clone();
    let main_ab=entry_ab(&entry,"MainTexAB");
    let mut main = if main_name=="0" || main_ab=="0" {
        ImageSource{image:image::RgbaImage::from_pixel(1,1,image::Rgba([255;4])),
                    sampling:TextureSampling::default(),gpu:crate::clothes_gpu::Texture::white()}
    } else {
        source(game,&entry,&main_ab,&main_name)
            .or_else(|| (main_name == "cf_face_00_t").then(|| source(game, &entry, "chara/bo_head_00.unity3d", &main_name)).flatten())?
    };
    // Hooks.OverlayBlitImpl creates a temporary sRGB RT only for underlays.
    // Linear source bytes must first undergo that Blit before its overlay.
    if let Some(overlay) = super::ksox_overlay_png(card, outfit, 4) {
        let png = super::encode_png(&main.image)?;
        let png = if main.sampling.srgb { png } else { super::linear_texture_to_srgb_png(&png)? };
        let png = super::composite_overlay(&png, &overlay)?;
        main.image = crate::teximg::load_from_memory(&png).ok()?.to_rgba8();
        main.sampling.srgb = true;
        let mut authored=main.image.clone();image::imageops::flip_vertical_in_place(&mut authored);
        main.gpu=crate::clothes_gpu::Texture{data:authored.into_raw(),width:main.image.width(),height:main.image.height(),
            format:29,mips:1,sampling:main.sampling,aniso:0,bias:0.};
    }
    let mask_name = entry.info("ColorMaskTex");
    let mask = source(game, &entry, &entry_ab(&entry, "ColorMaskAB"), &mask_name)
        .or_else(|| (mask_name == "cf_face_00_mc").then(|| source(game, &entry, "chara/bo_head_00.unity3d", &mask_name)).flatten());
    let ab="chara/mm_base.unity3d";
    // Sideloader overrides individual assets. A partial mm_base replacement
    // containing only clothes assets cannot hide the vanilla face material.
    let candidate = |bytes:crate::unity::SourceBytes| {
        let bundle=crate::unity::parse_bundle_ref(&bytes);
        let material=bundle.material_named("cf_m_face_create")?;
        if material.shader != "Koikano/create_skinface" { return None; }
        Some((bytes,bundle,material))
    };
    let (bytes,bundle,material) = crate::mods::override_bundle(&game.join("mods"),ab).map(crate::unity::SourceBytes::from).and_then(candidate)
        .or_else(||crate::gamefs::read_source(game.join("abdata").join(ab)).ok().and_then(candidate))?;
    if material.shader != "Koikano/create_skinface" { eprintln!("face-create unexpected shader {}",material.shader);return None; }
    let paint_mask = material.slot("_paintmask").and_then(|name| {
        let (_, png) = crate::unity::named_png_from_bytes(bytes.clone(), &[name])?;
        let png = super::flip_runtime_blit_v(png);
        Some(ImageSource { image: crate::teximg::load_from_memory(&png).ok()?.to_rgba8(),
                           sampling: bundle.texture_sampling_named(name)?,gpu:bundle.texture_gpu_named(name)? })
    });
    let makeup = |cat, id, key: &str, property: &str| {
        let entry = super::face_detail_entry_for_property(vanilla, mods, card, guids, cat, id, Some(property))?;
        source(game, &entry, &entry.main_ab, &entry.info(key))
    };
    // ID zero is a genuine catalog row: ChangeCreateFaceTexture calls
    // SetCreateTexture for it, whose selected property can explicitly be null.
    let cheek = makeup(402, card.cheek_id, "CheekTex", "ChaFileMakeup.cheekId");
    let lipline = makeup(404, card.lip_line_id, "LiplineTex", "ChaFileFace.lipLineId");
    let paint = [makeup(405, card.paint_ids[0], "PaintTex", "ChaFileMakeup.PaintID1"),
                 makeup(405, card.paint_ids[1], "PaintTex", "ChaFileMakeup.PaintID2")];
    let mole = makeup(415, card.mole_id, "MoleTex", "ChaFileFace.moleId");
    let identity = [1.,1.,0.,0.];
    let paint_uv = card.paint_layouts.map(|l| [super::lerp_f(0.25,-0.25,l[0]),
        super::lerp_f(0.3,-0.3,l[1]),super::lerp_f(1.,-1.,l[2]),super::lerp_f(-8.,0.7,l[3])]);
    let l = card.mole_layout;
    let inputs = Inputs { main: main.texture(st(&material,"_MainTex")),
        mask: mask.as_ref().map(|s|s.texture(st(&material,"_ColorMask"))),
        cheek: cheek.as_ref().map(|s|s.texture(identity)), lipline: lipline.as_ref().map(|s|s.texture(identity)),
        paint: paint.each_ref().map(|s|s.as_ref().map(|s|s.texture(identity))),
        mole: mole.as_ref().map(|s|s.texture(identity)),
        paint_mask: paint_mask.as_ref().map(|s|s.texture(st(&material,"_paintmask"))),
        colors: [skin,skin_sub,card.paint_colors[0],card.cheek_color,card.lip_line_color,card.mole_color,card.paint_colors[1]],
        cheek_uv: material.color("_tex4uv")?,lipline_uv: material.color("_tex5uv")?,
        paint_uv,mole_uv:[super::lerp_f(0.25,-0.25,l[0]),super::lerp_f(0.3,-0.3,l[1]),0.,0.7*l[3]] };
    let size = if super::character_hi_poly() { 1024 } else { 512 };
    let transparent=||{let mut t=crate::clothes_gpu::Texture::white();t.data=vec![0;4];t};
    let overlay=|s:&Option<ImageSource>|s.as_ref().map(|s|s.gpu.clone()).unwrap_or_else(transparent);
    let textures=[mask.as_ref().map(|s|s.gpu.clone()).unwrap_or_else(crate::clothes_gpu::Texture::white),
        overlay(&cheek),main.gpu.clone(),overlay(&lipline),overlay(&paint[0]),
        paint_mask.as_ref().map(|s|s.gpu.clone()).unwrap_or_else(crate::clothes_gpu::Texture::white),overlay(&paint[1]),overlay(&mole)];
    let shader=bundle.shader_pixel_dxbc_named("Koikano/create_skinface")
        .or_else(||crate::gamefs::read_source(game.join("abdata").join(ab)).ok()
            .and_then(|b|crate::unity::parse_bundle_ref(&b).shader_pixel_dxbc_named("Koikano/create_skinface")))?;
    let quality=crate::clothes_gpu::quality(game)?;
    let png=match crate::face_create_gpu::bake(&shader,&textures,&inputs,size,size,quality) {
        Ok(png)=>png,Err(error)=>{eprintln!("ok-extract: native face create GPU failed: {error}");return None;}
    };
    if let Some(overlay) = super::ksox_overlay_png(card,outfit,2) {
        super::composite_overlay(&png,&overlay)
    } else { Some(png) }
}
