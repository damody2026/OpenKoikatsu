//! Studio animations into the OK animation library (S6): the `studio/info` Anime / HAnime lists (group, category, no
//! -> controller states) are sampled once from the installed game into `.okanim` files of the OK base library
//! (`<lib>/anims/studio`), listed in `<lib>/studio_anims.json`. OK Studio plays them without the Unity bundles.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct StudioAnim {
    /// "anime" (Studio motions) or "hanime" (H motions in Studio).
    pub kind: String,
    pub group: i32,
    pub category: i32,
    pub no: i32,
    pub name: String,
    /// Clip key the converter samples (`states:studio@…|state`).
    pub key: String,
    /// `.okanim` file, relative to the library (empty until converted).
    #[serde(default)]
    pub file: String,
    #[serde(default)]
    pub duration: f32,
    #[serde(default)]
    pub looping: bool,
    /// Who plays it (`motion_sex`): 0 either, 1 female, 2 male.
    #[serde(default)]
    pub sex: i32,
}

/// The lists carry no sex column: the game names it in the group (女H奉仕 / 男H奉仕, H女複数, レズ) or the category (キャラ / 男).
pub fn motion_sex(group: &str, category: &str) -> i32 {
    if group.contains('男') || category.contains('男') { 2 } else if group.contains('女') || group.contains("レズ") || category.contains('女') { 1 } else { 0 }
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Group { pub group: i32, pub id: i32, pub name: String }

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct AnimCatalog { pub format: String, pub groups: Vec<Group>, pub categories: Vec<Group>, pub anims: Vec<StudioAnim> }

pub const FILE_NAME: &str = "studio_anims.json";

fn cell(r: &[String], i: usize) -> String { r.get(i).map(|s| s.trim().to_owned()).unwrap_or_default() }
fn num(r: &[String], i: usize) -> Option<i32> { r.get(i).and_then(|c| c.trim().parse().ok()) }
fn bundle(path: &str) -> String { path.trim().replace('\\', "/").to_ascii_lowercase() }

/// `states:studio@` + hex(JSON [base bundle, controller, override bundle, override controller]).
pub fn studio_source(base: &str, owner: &str, over: &str, over_owner: &str) -> String {
    let json = serde_json::to_string(&[base, owner, over, over_owner]).unwrap_or_default();
    let hex: String = json.bytes().map(|b| format!("{b:02X}")).collect();
    format!("states:studio@{hex}")
}

/// The controller state a list row names, as the game resolves it: the owner controller's states
/// (`<controller> / [Studio layer N] <path>`) whose path is the state or ends with `.state`, layer 0 first.
fn resolve_state(labels: &[String], owner: &str, state: &str) -> Option<String> {
    let mut best: Option<(i32, &String)> = None;
    for label in labels {
        let Some((controller, rest)) = label.split_once(" / ") else { continue; };
        if controller != owner { continue; }
        let Some(rest) = rest.strip_prefix("[Studio layer ") else { continue; };
        let Some((layer, path)) = rest.split_once("] ") else { continue; };
        let Ok(layer) = layer.parse::<i32>() else { continue; };
        if path != state && !path.ends_with(&format!(".{state}")) { continue; }
        if best.is_none_or(|(l, _)| layer < l) { best = Some((layer, label)); }
    }
    best.map(|(_, l)| l.clone())
}

/// A Sideloader list file's text: UTF-16 with a byte-order mark, otherwise UTF-8.
fn list_text(bytes: &[u8]) -> String {
    let utf16 = |be: bool| char::decode_utf16(bytes[2..].chunks_exact(2).map(|c| if be { u16::from_be_bytes([c[0], c[1]]) } else { u16::from_le_bytes([c[0], c[1]]) }))
        .map(|c| c.unwrap_or('\u{FFFD}')).collect::<String>();
    match bytes {
        [0xFF, 0xFE, ..] => utf16(false),
        [0xFE, 0xFF, ..] => utf16(true),
        [0xEF, 0xBB, 0xBF, rest @ ..] => String::from_utf8_lossy(rest).into_owned(),
        _ => String::from_utf8_lossy(bytes).into_owned(),
    }
}

/// The Studio animation lists a zipmod adds (`abdata/studio/info/**/Anime*.csv`), as sheets named like the game's.
fn mod_sheets(zip: &Path) -> Vec<(String, Vec<Vec<String>>)> {
    let names = crate::gamefs::zip_memo("names", zip, || std::sync::Arc::new(crate::gamefs::open_zip(zip).map(|z| z.file_names()).unwrap_or_default()));
    let wanted: Vec<String> = names.iter().filter(|n| {
        let l = n.replace('\\', "/").to_ascii_lowercase();
        let stem = l.rsplit('/').next().unwrap_or_default();
        l.starts_with("abdata/studio/info/") && l.ends_with(".csv") && (stem.starts_with("anime") || stem.starts_with("hanime"))
    }).cloned().collect();
    if wanted.is_empty() { return Vec::new(); }
    let Some(mut archive) = crate::gamefs::open_zip(zip) else { return Vec::new(); };
    let mut out = Vec::new();
    for name in wanted {
        let Ok(mut f) = archive.by_name(&name) else { continue; };
        let mut bytes = Vec::new();
        if std::io::Read::read_to_end(&mut f, &mut bytes).is_err() { continue; }
        let stem = name.rsplit(['/', '\\']).next().unwrap_or_default().trim_end_matches(".csv").trim_end_matches(".CSV").to_owned();
        let rows = list_text(&bytes).lines().map(|l| l.split(',').map(|c| c.trim().to_owned()).collect()).collect();
        out.push((stem, rows));
    }
    out
}

/// A list row's bundle as an animation source: the game's file, or the zipmod that ships it (`mods/…zipmod!path`).
fn mod_bundle(game: &Path, shipped: &std::collections::HashMap<String, PathBuf>, path: &str) -> String {
    let b = bundle(path);
    if b.is_empty() || game.join("abdata").join(&b).is_file() { return b; }
    let Some(zip) = shipped.get(&format!("abdata/{b}")) else { return b; };
    let Ok(rel) = zip.strip_prefix(game) else { return b; };
    format!("{}!{b}", rel.to_string_lossy().replace('\\', "/"))
}

/// The (group, category, no) Studio motions characters play in the installed game's scenes.
pub fn scene_motions(game: &Path) -> std::collections::BTreeSet<(i32, i32, i32)> {
    fn walk(d: &Path, out: &mut Vec<PathBuf>) {
        let Ok(rd) = std::fs::read_dir(d) else { return; };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() { walk(&p, out); } else if p.extension().is_some_and(|x| x.eq_ignore_ascii_case("png")) { out.push(p); }
        }
    }
    fn collect(objects: &[crate::studio_scene::Object], out: &mut std::collections::BTreeSet<(i32, i32, i32)>) {
        use crate::studio_scene::Object;
        for o in objects {
            match o {
                Object::Character { children, anime, .. } => { if anime[0] >= 0 { out.insert((anime[0], anime[1], anime[2])); } for (_, c) in children { collect(c, out); } }
                Object::Item { children, .. } | Object::Folder { children, .. } | Object::Route { children, .. } => collect(children, out),
                _ => {}
            }
        }
    }
    let mut files = Vec::new();
    walk(&game.join("UserData/Studio/scene"), &mut files);
    let mut out = std::collections::BTreeSet::new();
    for f in files {
        if let Ok(scene) = std::fs::read(&f).map_err(|e| e.to_string()).and_then(|b| crate::studio_scene::read(&b)) { collect(&scene.objects, &mut out); }
    }
    out
}

pub fn catalog(game: &Path) -> AnimCatalog { catalog_with_mods(game, &Default::default()) }

/// The game's Studio animations, plus the zipmod ones among `mod_motions` (group, category, no) the game lacks:
/// OK keeps the mod motions scenes use, not every installed mod's.
pub fn catalog_with_mods(game: &Path, mod_motions: &std::collections::BTreeSet<(i32, i32, i32)>) -> AnimCatalog {
    let dir = game.join("abdata/studio/info");
    let mut files: Vec<PathBuf> = crate::gamefs::read_dir(&dir).map(|it| it.filter_map(|e| e.ok()).map(|e| e.path()).filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("unity3d"))).collect()).unwrap_or_default();
    files.sort();
    let (mut groups, mut categories, mut anims) = (BTreeMap::new(), BTreeMap::new(), BTreeMap::new());
    // Controller states per source (each bundle's graph is read once).
    let mut states: std::collections::HashMap<String, Vec<String>> = std::collections::HashMap::new();
    let mut key_of = |source: String, owner: &str, state: &str| -> String {
        let labels = states.entry(source.clone()).or_insert_with(|| crate::anim::clip_names_for_source(game, &source));
        resolve_state(labels, owner, state).map(|l| format!("{source}|{l}")).unwrap_or_default()
    };
    let mut sheet_sets: Vec<(bool, Vec<(String, Vec<Vec<String>>)>)> = files.iter().filter_map(|f| crate::gamefs::read(f).ok()).map(|b| (false, crate::studio_items::sheets(b))).collect();
    let mut shipped = std::collections::HashMap::new();
    if !mod_motions.is_empty() {
        let mods = game.join("mods");
        let mut zips = crate::mods::all_zipmods(&mods);
        zips.sort();
        for zip in &zips {
            let names = crate::gamefs::zip_memo("names", zip, || std::sync::Arc::new(crate::gamefs::open_zip(zip).map(|z| z.file_names()).unwrap_or_default()));
            for n in names.iter() {
                let n = n.replace('\\', "/").to_ascii_lowercase();
                if n.starts_with("abdata/") && n.ends_with(".unity3d") { shipped.entry(n).or_insert_with(|| zip.clone()); }
            }
            let sheets = mod_sheets(zip);
            if !sheets.is_empty() { sheet_sets.push((true, sheets)); }
        }
    }
    let mut source_of = |modded: bool, path: &str| if modded { mod_bundle(game, &shipped, path) } else { bundle(path) };
    for (modded, sheets) in sheet_sets {
        for (name, rows) in sheets {
            let kind = name.split('_').next().unwrap_or_default().to_owned();
            let suffix: i32 = name.rsplit('_').next().and_then(|s| s.parse().ok()).unwrap_or(-1);
            for r in rows.iter().filter(|r| num(r, 0).is_some()) {
                let no = num(r, 0).unwrap();
                let (g, c) = (num(r, 1).unwrap_or_default(), num(r, 2).unwrap_or_default());
                // A mod row only fills a motion a scene plays that the game itself does not list.
                let fills = |anims: &BTreeMap<(String, i32, i32, i32), StudioAnim>| !modded
                    || (mod_motions.contains(&(g, c, no)) && !anims.keys().any(|k| (k.1, k.2, k.3) == (g, c, no)));
                match kind.as_str() {
                    "AnimeGroup" => { if !modded || !groups.contains_key(&no) { groups.insert(no, Group { group: -1, id: no, name: cell(r, 1) }); } }
                    "AnimeCategory" => { if !modded || !categories.contains_key(&(suffix, no)) { categories.insert((suffix, no), Group { group: suffix, id: no, name: cell(r, 1) }); } }
                    // 管理番号, 大きい項目, 中間項目, 表示名, バンドルパス, ファイル名, クリップ名
                    "Anime" if fills(&anims) => {
                        let key = key_of(studio_source(&source_of(modded, &cell(r, 4)), &cell(r, 5), "", ""), &cell(r, 5), &cell(r, 6));
                        anims.insert(("anime".to_owned(), g, c, no), StudioAnim { kind: "anime".into(), group: g, category: c, no, name: cell(r, 3), key, ..Default::default() });
                    }
                    // 管理番号, 大きい項目, 中間項目, 表示名, base bundle, base controller, bundle, controller, クリップ名, …
                    "HAnime" if fills(&anims) => {
                        let (base, owner, over, over_owner) = (source_of(modded, &cell(r, 4)), cell(r, 5), source_of(modded, &cell(r, 6)), cell(r, 7));
                        // The second pair overrides the base controller when it names another one.
                        let (over, over_owner) = if over_owner.is_empty() || over.is_empty() || (over == base && over_owner == owner) { (String::new(), String::new()) } else { (over, over_owner) };
                        let key = key_of(studio_source(&base, &owner, &over, &over_owner), &owner, &cell(r, 8));
                        anims.insert(("hanime".to_owned(), g, c, no), StudioAnim { kind: "hanime".into(), group: g, category: c, no, name: cell(r, 3), key, ..Default::default() });
                    }
                    _ => {}
                }
            }
        }
    }
    let groups: Vec<Group> = groups.into_values().collect();
    let categories: Vec<Group> = categories.into_values().collect();
    let name_of = |list: &[Group], g: i32, id: i32| list.iter().find(|x| x.group == g && x.id == id).map(|x| x.name.clone()).unwrap_or_default();
    // Rows whose state the controller does not have are left out (the game cannot play them either).
    let anims = anims.into_values().filter(|a: &StudioAnim| !a.key.is_empty())
        .map(|mut a| { a.sex = motion_sex(&name_of(&groups, -1, a.group), &name_of(&categories, a.group, a.category)); a }).collect();
    AnimCatalog { format: "ok-studio-anims-1".into(), groups, categories, anims }
}

/// `--oklib anim-convert <lib> <card> [filter] [threads]`: samples the Studio animations (those whose
/// `kind_group_category_no` contains `filter`) with a character of the installed game and writes the library files.
pub fn convert(lib: &Path, game: &Path, card: &Path, filter: Option<&str>, threads: usize) -> Result<AnimCatalog, String> {
    // Mod motions join the library when the installed game's scenes play them.
    let mut cat = catalog_with_mods(game, &scene_motions(game));
    let previous: Option<AnimCatalog> = std::fs::read_to_string(lib.join(FILE_NAME)).ok().and_then(|t| serde_json::from_str(&t).ok());
    let file_of = |a: &StudioAnim| format!("anims/studio/{}_{}_{}_{}.okanim", a.kind, a.group, a.category, a.no);
    let wanted = |a: &StudioAnim| filter.is_none_or(|f| format!("{}_{}_{}_{}", a.kind, a.group, a.category, a.no).contains(f));
    let items: Vec<crate::okanim::batch::Item> = cat.anims.iter().filter(|a| wanted(a))
        .map(|a| crate::okanim::batch::Item { key: a.key.clone(), file: lib.join(file_of(a)).display().to_string(), source: a.key.split('|').next().unwrap_or_default().to_owned() })
        .collect();
    let mut session = crate::oklib::open_test_card(card, 0)?;
    session.game_root = game.to_path_buf();
    let sampler = crate::okanim::convert::ClipSampler::of_session(&mut session)?;
    let started = std::time::Instant::now();
    let batch = crate::okanim::batch::Batch::start(sampler, items, 30.0, threads);
    let mut last = 0;
    let progress = loop {
        std::thread::sleep(std::time::Duration::from_secs(2));
        let p = batch.poll(0);
        let processed = p["processed"].as_u64().unwrap_or(0);
        if processed / 100 != last / 100 { println!("OKANIM {processed}/{} converted={} skipped={} failed={} {:.0}s", p["total"], p["converted"], p["skipped"], p["failed"], started.elapsed().as_secs_f64()); }
        last = processed;
        if !p["running"].as_bool().unwrap_or(false) { break p; }
    };
    println!("OKANIM done {} converted={} skipped={} failed={} first_error={} {:.0}s", progress["total"], progress["converted"], progress["skipped"], progress["failed"], progress["first_error"], started.elapsed().as_secs_f64());
    // Catalogue: converted entries get their file, duration and looping; the rest keep a previous conversion's.
    let done: BTreeMap<String, serde_json::Value> = progress["entries"].as_array().into_iter().flatten().filter_map(|e| Some((e["key"].as_str()?.to_owned(), e.clone()))).collect();
    for a in &mut cat.anims {
        if let Some(e) = done.get(&a.key) {
            a.file = file_of(a);
            a.duration = e["duration"].as_f64().unwrap_or(0.) as f32;
            a.looping = e["looping"].as_bool().unwrap_or(false);
        } else if let Some(p) = previous.as_ref().and_then(|p| p.anims.iter().find(|x| x.key == a.key && !x.file.is_empty())) {
            a.file = p.file.clone(); a.duration = p.duration; a.looping = p.looping;
        }
    }
    std::fs::write(lib.join(FILE_NAME), serde_json::to_string_pretty(&cat).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    Ok(cat)
}

#[cfg(test)]
mod tests {
    #[test]
    #[ignore = "installed game: the Studio animation lists"]
    fn vanilla_anim_catalog() {
        let c = super::catalog(&crate::test_support::game_path(""));
        let anime = c.anims.iter().filter(|a| a.kind == "anime").count();
        let hanime = c.anims.len() - anime;
        println!("groups={} categories={} anime={anime} hanime={hanime}", c.groups.len(), c.categories.len());
        for a in c.anims.iter().take(3) { println!("  {} {}/{}/{} {} {}", a.kind, a.group, a.category, a.no, a.name, &a.key[a.key.len().saturating_sub(40)..]); }
        assert!(anime > 500 && hanime > 1000);
    }

    #[test]
    fn sideloader_lists_decode() {
        let utf16: Vec<u8> = [0xFF, 0xFE].into_iter().chain("9,A\r\n".encode_utf16().flat_map(|u| u.to_le_bytes())).collect();
        assert_eq!(super::list_text(&utf16), "9,A\r\n");
        assert_eq!(super::list_text(b"\xEF\xBB\xBF1,B"), "1,B");
    }

    #[test]
    #[ignore = "installed game + mods: a scene's mod motion comes from its zipmod list and bundle"]
    fn mod_motion_from_zipmod() {
        let game = &crate::test_support::game_path("");
        // AgiShark's Mixamo pack: Anime 0 (group 1011, category 0) "Administering Cpr", its bundle inside the zipmod.
        let used = std::collections::BTreeSet::from([(1011, 0, 0), (0, 0, 0)]);
        let vanilla = super::catalog(game);
        let with = super::catalog_with_mods(game, &used);
        let found: Vec<_> = with.anims.iter().filter(|a| (a.group, a.category, a.no) == (1011, 0, 0)).collect();
        for a in &found { println!("  {} {}/{}/{} {} {}", a.kind, a.group, a.category, a.no, a.name, a.key.split('|').nth(1).unwrap_or_default()); }
        assert_eq!(with.anims.len(), vanilla.anims.len() + 1, "only the asked mod motion joins; mod rows never replace the game's");
        assert_eq!(found.len(), 1);
        let source = found[0].key.split('|').next().unwrap();
        let hex = source.strip_prefix("states:studio@").unwrap();
        let json = String::from_utf8((0..hex.len()).step_by(2).map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap()).collect()).unwrap();
        assert!(json.contains(".zipmod!studio/anime/mixamo_action_part1.unity3d"), "{json}");
    }
}

#[cfg(test)]
mod acceptance {
    use crate::okanim::OkAnim;
    #[test]
    #[ignore = "installed game + converted library: sampled .okanim files match the game's Animator between frames"]
    fn okanim_matches_native_animator() {
        let manifest = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let lib = manifest.join("../../Content/IndependentResources/base_v1");
        let game = &crate::test_support::game_path("");
        let cat: super::AnimCatalog = serde_json::from_str(&std::fs::read_to_string(lib.join(super::FILE_NAME)).unwrap()).unwrap();
        let mut s = crate::oklib::open_test_card(&manifest.join("../../Content/CharacterCards/CrowdAPNG/C002.png"), 0).unwrap();
        s.game_root = game.to_path_buf();
        let sampler = crate::okanim::convert::ClipSampler::of_session(&mut s).unwrap();
        let step = (cat.anims.len() / 60).max(1);
        let (mut worst, mut sum, mut count, mut checked) = (0f32, 0f64, 0usize, 0usize);
        let mut worst_at = String::new();
        crate::anim::begin_animation_preparation();
        for a in cat.anims.iter().step_by(step) {
            let anim = OkAnim::from_bytes(&std::fs::read(lib.join(&a.file)).unwrap()).unwrap();
            let d = anim.duration();
            for frac in [0.13f32, 0.5, 0.77] {
                let t = d * frac;
                let mut skel = sampler.base.clone();
                crate::anim::crossfade::sample(&sampler.game, &mut skel, &a.key, t, &sampler.params, sampler.clock);
                let pose = anim.sample(t);
                for (k, track) in anim.tracks.iter().enumerate() {
                    let Some(native) = skel.tfs.values().find(|x| x.name == track.bone).map(|x| x.rot) else { continue; };
                    let ok = crate::pose::quat_mul(track.rest_local, pose.rot[k]);
                    let dot = (0..4).map(|i| native[i] * ok[i]).sum::<f32>().abs().min(1.0);
                    let err = 2.0 * dot.acos().to_degrees();
                    sum += err as f64; count += 1;
                    if err > worst { worst = err; worst_at = format!("{} {} t={t:.2} {}", a.name, a.kind, track.bone); }
                }
            }
            checked += 1;
        }
        crate::anim::end_animation_preparation();
        println!("checked {checked} animations, {count} bone samples: mean {:.3} deg, worst {worst:.2} deg ({worst_at})", sum / count.max(1) as f64);
        let moving = cat.anims.iter().filter(|a| a.duration > 0.0 && a.duration != 1.0).count();
        println!("{moving} of {} animations have their own duration", cat.anims.len());
        assert!(checked >= 50 && sum / (count as f64) < 1.0 && worst < 20.0 && moving * 2 > cat.anims.len());
    }
}



