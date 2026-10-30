//! Vanilla Studio catalogue: the ExcelData lists in `abdata/studio/info` (ItemGroup, ItemCategory, ItemList, Map,
//! Light). The converter writes it into the library as `studio_items.json`, each entry with the library model id of
//! its bundle, so the game places items and maps from the library alone.

use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

use serde::{Deserialize, Serialize};
use unity_asset_core::UnityValue;
use unity_asset_decode::object::UnityObject;

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Group { pub id: i32, pub name: String }

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Category { pub group: i32, pub id: i32, pub name: String }

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Item {
    /// 管理番号: unique inside its group and category (the game keys items by group, category, no).
    pub no: i32,
    pub group: i32,
    pub category: i32,
    pub name: String,
    /// Game bundle (`abdata/...`) and prefab.
    pub bundle: String,
    pub prefab: String,
    /// Library model of the bundle (32 hex; empty when the bundle is missing).
    pub model: String,
    /// 子の接続先: child attach point inside the item.
    pub child: String,
    pub anime: bool,
    /// カラー１..３ and their 柄 (pattern) flags.
    pub colors: [bool; 3],
    pub patterns: [bool; 3],
    pub scale: bool,
    /// ItemComponent.defLineWidth: the outline width the item is made with (0: no outline; the scene's line width
    /// scales it). None: not read (an older catalogue).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub line_width: Option<f32>,
    /// ItemComponent.rendAlpha is not empty: the item fades with the scene's item alpha.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub alpha: bool,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Map {
    pub no: i32, pub category: i32, pub name: String, pub bundle: String, pub prefab: String, pub model: String,
    /// What the map shows: the prefab, or for a scene bundle (`<prefab>.unity` in its container) every scene root.
    #[serde(default)]
    pub roots: Vec<String>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Light { pub no: i32, pub name: String, pub bundle: String, pub prefab: String, pub model: String }

/// Character attach point a scene child can hang on (AccessoryPoint sheet): `key` is ChaReference.RefObjKey, `bone` the
/// transform it names in the character.
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct AccessoryPoint { pub no: i32, pub group: i32, pub key: String, pub name: String, pub bone: String }

/// The transform a ChaReference.RefObjKey names (ChaReference.CreateReferenceInfo; the a_n_* keys name themselves).
pub fn reference_bone(key: &str) -> String {
    match key {
        "HeadParent" => "cf_s_head", "HairParent" => "cf_J_FaceUp_ty", "A_ROOTBONE" => "cf_j_hips", "BUSTUP_TARGET" => "cf_j_spine03",
        "NECK_LOOK_TARGET" => "cf_s_spine03", "CORRECT_ARM_L" => "cf_j_forearm01_L", "CORRECT_ARM_R" => "cf_j_forearm01_R",
        "CORRECT_HAND_L" => "cf_j_hand_L", "CORRECT_HAND_R" => "cf_j_hand_R", "S_ANA" => "cf_s_ana", "S_Son" => "n_dankon",
        k => k,
    }.to_owned()
}

/// Studio screen filter (Filter sheet): an Amplify Color LUT texture `file` in `bundle` (SceneInfo.aceNo picks one).
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Filter { pub no: i32, pub name: String, pub bundle: String, pub file: String }

/// Studio FK bone (Bone sheet): group 0 body .. 13; op 0 the main skeleton, 1 optional parts (hair, skirt, fingers...).
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct FkBone { pub no: i32, pub bone: String, pub name: String, pub group: i32, pub op: i32 }

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Catalog {
    pub format: String,
    pub groups: Vec<Group>,
    pub categories: Vec<Category>,
    pub items: Vec<Item>,
    pub maps: Vec<Map>,
    pub map_categories: Vec<Group>,
    pub lights: Vec<Light>,
    #[serde(default)]
    pub fk_bones: Vec<FkBone>,
    #[serde(default)]
    pub accessory_points: Vec<AccessoryPoint>,
    #[serde(default)]
    pub filters: Vec<Filter>,
    /// Names of the eyes patterns (character list cha_eyeset, category 2): ChaFileStatus.eyesPtn as Studio lists it.
    #[serde(default)]
    pub eye_names: Vec<Group>,
    /// Studio hand patterns (HandAnime sheets: group 0 left, 1 right): the row's display name, else its clip name.
    #[serde(default)]
    pub hand_names: Vec<Category>,
}

pub const FILE_NAME: &str = "studio_items.json";

/// ExcelData sheets of one bundle: (MonoBehaviour name, rows of cells).
pub fn sheets(bytes: Vec<u8>) -> Vec<(String, Vec<Vec<String>>)> {
    let parsed = crate::unity::parse_bundle_cached(bytes);
    let mut out = Vec::new();
    parsed.with_serialized(|files| for f in files {
        for info in f.objects_of_type(114) {
            let Ok(obj) = UnityObject::from_serialized_file(f, &info) else { continue; };
            let Some(name) = obj.name() else { continue; };
            let props = obj.class.properties();
            let Some(rows) = props.get("list").and_then(|v| v.as_array()) else { continue; };
            let rows = rows.iter().map(|row| match row {
                UnityValue::Object(o) => o.get("list").and_then(|v| v.as_array()).map(|cells| cells.iter().map(|c| c.as_str().unwrap_or_default().to_owned()).collect()).unwrap_or_default(),
                _ => Vec::new(),
            }).collect();
            out.push((name.to_string(), rows));
        }
    });
    out.sort_by(|a, b| a.0.cmp(&b.0));
    out
}

fn num(cell: Option<&String>) -> Option<i32> { cell.and_then(|c| c.trim().parse().ok()) }
fn cell(row: &[String], i: usize) -> String { row.get(i).map(|s| s.trim().to_owned()).unwrap_or_default() }
fn flag(row: &[String], i: usize) -> bool { row.get(i).is_some_and(|s| s.trim().eq_ignore_ascii_case("true")) }
/// `studio/00.unity3d` → `abdata/studio/00.unity3d`.
fn game_bundle(path: &str) -> String {
    let p = path.trim().replace('\\', "/").to_ascii_lowercase();
    if p.is_empty() || p.starts_with("abdata/") { p } else { format!("abdata/{p}") }
}
/// Data rows: the ones whose first cell is a number (titles and column names are not).
fn data_rows(rows: &[Vec<String>]) -> impl Iterator<Item = &Vec<String>> { rows.iter().filter(|r| num(r.first()).is_some()) }
/// Trailing number of a sheet name (`ItemCategory_00_07` → 7).
fn suffix(name: &str) -> Option<i32> { name.rsplit('_').next()?.parse().ok() }

/// Builds the catalogue from the game's info bundles (later bundles and sheets override earlier entries, as in the game).
pub fn catalog(game: &Path) -> Catalog {
    if std::env::var_os("OK_STUDIO_INFO_DUMP").is_some() {
        let lists = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
        let mut by_cat: BTreeMap<i32, Vec<String>> = BTreeMap::new();
        for ((c, i), e) in lists.by_id.iter() { if *c < 20 || (*c >= 100 && *c < 140) || *c >= 900 { by_cat.entry(*c).or_default().push(format!("{i}={}", e.name)); } }
        for (c, names) in by_cat { eprintln!("LIST {c} n={} {}", names.len(), names.iter().take(40).cloned().collect::<Vec<_>>().join(", ")); }
    }
    let dir = game.join("abdata/studio/info");
    let mut files: Vec<_> = crate::gamefs::read_dir(&dir).map(|it| it.filter_map(|e| e.ok()).map(|e| e.path()).filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("unity3d"))).collect()).unwrap_or_default();
    files.sort();
    let mut groups = BTreeMap::new();
    let mut categories = BTreeMap::new();
    let mut items = BTreeMap::new();
    let mut maps = BTreeMap::new();
    let mut map_categories = BTreeMap::new();
    let mut lights = BTreeMap::new();
    let mut fk_bones = BTreeMap::new();
    let mut accessory_points = BTreeMap::new();
    let mut filters = BTreeMap::new();
    let mut hand_names = BTreeMap::new();
    for file in files {
        let Ok(bytes) = crate::gamefs::read(&file) else { continue; };
        for (name, rows) in sheets(bytes) {
            let kind = name.split('_').next().unwrap_or_default();
            // OK_STUDIO_INFO_DUMP=1: every sheet's name and first rows (to find the columns of a list).
            if std::env::var_os("OK_STUDIO_INFO_DUMP").is_some() {
                eprintln!("SHEET {} {name} rows={}", file.display(), rows.len());
                for r in rows.iter().take(4) { eprintln!("  {}", r.join(" | ")); }
            }
            for r in data_rows(&rows) {
                let no = num(r.first()).unwrap_or_default();
                match kind {
                    "ItemGroup" => { groups.insert(no, Group { id: no, name: cell(r, 1) }); }
                    "ItemCategory" => { let group = suffix(&name).unwrap_or_default(); categories.insert((group, no), Category { group, id: no, name: cell(r, 1) }); }
                    "ItemList" => {
                        let key = (num(r.get(1)).unwrap_or_default(), num(r.get(2)).unwrap_or_default(), no);
                        items.insert(key, Item {
                            no, group: num(r.get(1)).unwrap_or_default(), category: num(r.get(2)).unwrap_or_default(), name: cell(r, 3),
                            bundle: game_bundle(&cell(r, 5)), prefab: cell(r, 6), model: String::new(), child: cell(r, 7), anime: flag(r, 8),
                            colors: [flag(r, 9), flag(r, 11), flag(r, 13)], patterns: [flag(r, 10), flag(r, 12), flag(r, 14)], scale: flag(r, 15), line_width: None, alpha: false,
                        });
                    }
                    "Map" => { maps.insert(no, Map { no, category: num(r.get(1)).unwrap_or_default(), name: cell(r, 2), bundle: game_bundle(&cell(r, 3)), prefab: cell(r, 4), model: String::new(), roots: Vec::new() }); }
                    "MapCategory" => { map_categories.insert(no, Group { id: no, name: cell(r, 1) }); }
                    "Bone" => { fk_bones.insert(no, FkBone { no, bone: cell(r, 1), name: cell(r, 2), group: num(r.get(3)).unwrap_or_default(), op: num(r.get(4)).unwrap_or_default() }); }
                    "Filter" => { filters.insert(no, Filter { no, name: cell(r, 1), bundle: game_bundle(&cell(r, 2)), file: cell(r, 3) }); }
                    "AccessoryPoint" => { let key = cell(r, 2); accessory_points.insert(no, AccessoryPoint { no, group: num(r.get(1)).unwrap_or_default(), bone: reference_bone(&key), key, name: cell(r, 3) }); }
                    "HandAnime" => {
                        // HandAnime_<side>_<sheet>: side 0 left, 1 right.
                        let side = name.split('_').nth(1).and_then(|s| s.parse::<i32>().ok()).unwrap_or_default();
                        let label = if cell(r, 1).is_empty() { cell(r, 4) } else { cell(r, 1) };
                        hand_names.insert((side, no), Category { group: side, id: no, name: label });
                    }
                    "Light" => { lights.insert(no, Light { no, name: cell(r, 1), bundle: game_bundle(&cell(r, 3)), prefab: cell(r, 4), model: String::new() }); }
                    _ => {}
                }
            }
        }
    }
    Catalog {
        format: "ok-studio-items-1".into(),
        groups: groups.into_values().collect(),
        categories: categories.into_values().collect(),
        items: items.into_values().filter(|i: &Item| !i.bundle.is_empty() && !i.prefab.is_empty()).collect(),
        maps: maps.into_values().filter(|m: &Map| !m.bundle.is_empty() && !m.prefab.is_empty()).collect(),
        map_categories: map_categories.into_values().collect(),
        eye_names: eye_names(game),
        hand_names: hand_names.into_values().collect(),
        lights: lights.into_values().collect(),
        fk_bones: fk_bones.into_values().filter(|b: &FkBone| !b.bone.is_empty()).collect(),
        accessory_points: accessory_points.into_values().filter(|p: &AccessoryPoint| !p.key.is_empty()).collect(),
        filters: filters.into_values().filter(|f: &Filter| !f.bundle.is_empty() && !f.file.is_empty()).collect(),
    }
}

/// A texture of a game bundle by name (`bundle` relative to the game root or to `abdata`), e.g. a Studio filter LUT.
/// A mod's bundle is `<zipmod relative to the game root>|<bundle>`.
pub fn texture_png(root: &Path, bundle: &str, name: &str) -> Option<Vec<u8>> {
    let bundle = bundle.replace('\\', "/");
    let (zip, ab) = match bundle.split_once('|') { Some((zip, ab)) => (Some(root.join(zip).to_string_lossy().into_owned()), ab), None => (None, bundle.as_str()) };
    let ab = ab.strip_prefix("abdata/").unwrap_or(ab);
    crate::png_from_bundle(root, ab, zip.as_deref(), &[name], &[], &[])
}

/// The mods' screen filter LUTs (studio_mod_items.json `filters`) recorded into the library, so a scene saved with a
/// mod filter grades the same without the mod. Returns (recorded, missing).
pub fn record_mod_filters(game: &Path, lib: &Path) -> Result<(usize, usize), String> {
    let text = std::fs::read_to_string(lib.join(MOD_FILE_NAME)).map_err(|e| format!("{MOD_FILE_NAME}: {e}"))?;
    let mods: ModCatalog = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    let store = crate::oklib::activate(lib, crate::oklib::Mode::Record)?;
    crate::gamefs::activate(crate::gamefs::Mode::Record, game, lib)?;
    let was_converting = crate::oklib::CONVERTING.swap(true, std::sync::atomic::Ordering::Relaxed);
    let (mut found, mut missing) = (0, 0);
    for f in &mods.filters {
        if texture_png(game, &f.bundle, &f.file).is_some() { found += 1 } else { missing += 1; eprintln!("mod filter {} {}: no {} in {}", f.no, f.name, f.file, f.bundle); }
    }
    store.flush();
    let saved = crate::gamefs::save(lib);
    crate::unity::clear_bundle_cache();
    crate::oklib::deactivate();
    crate::gamefs::deactivate();
    crate::oklib::CONVERTING.store(was_converting, std::sync::atomic::Ordering::Relaxed);
    saved.map(|_| (found, missing))
}

/// Prefab names the lists ask for, per game bundle (converter hints).
pub fn prefab_hints(c: &Catalog) -> BTreeMap<String, BTreeSet<String>> {
    let mut out: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
    for (bundle, prefab) in c.items.iter().map(|i| (&i.bundle, &i.prefab)).chain(c.maps.iter().map(|m| (&m.bundle, &m.prefab))).chain(c.lights.iter().map(|l| (&l.bundle, &l.prefab))) {
        out.entry(bundle.clone()).or_default().insert(prefab.clone());
    }
    out
}

/// Fills in the library model of every bundle (record mode hashes the game file; replay reads the recorded id).
pub fn resolve_models(game: &Path, c: &mut Catalog) -> usize {
    let mut ids: BTreeMap<String, String> = BTreeMap::new();
    let mut id = |bundle: &str| -> String {
        ids.entry(bundle.to_owned()).or_insert_with(|| crate::gamefs::read(game.join(bundle)).map(|b| crate::oklib::content_id(&b).iter().map(|x| format!("{x:02x}")).collect()).unwrap_or_default()).clone()
    };
    for i in &mut c.items { i.model = id(&i.bundle); }
    for m in &mut c.maps {
        m.model = id(&m.bundle);
        // Studio maps are scene bundles loaded by scene name: they show every scene root.
        let top = crate::gamefs::read(game.join(&m.bundle)).map(|b| crate::unity::parse_bundle_cached(b).prefab_directory().top.clone()).unwrap_or_default();
        m.roots = if top.is_empty() || top.iter().any(|t| t.eq_ignore_ascii_case(&m.prefab)) { vec![m.prefab.clone()] } else { top };
    }
    for l in &mut c.lights { l.model = id(&l.bundle); }
    item_components(game, c);
    c.items.iter().filter(|i| i.model.is_empty()).count() + c.maps.iter().filter(|m| m.model.is_empty()).count()
}

/// Each item's ItemComponent (the MonoBehaviour with `rendAlpha` on the prefab's root): its outline width and whether
/// it has fading renderers. Items whose bundle or component is not found keep `line_width: None`.
pub fn item_components(game: &Path, c: &mut Catalog) {
    let mut by_bundle: BTreeMap<String, BTreeMap<String, (f32, bool)>> = BTreeMap::new();
    for bundle in c.items.iter().map(|i| i.bundle.clone()).collect::<BTreeSet<_>>() {
        let Ok(bytes) = crate::gamefs::read(game.join(&bundle)) else { continue };
        let mut found = BTreeMap::new();
        crate::unity::parse_bundle_cached(bytes).with_serialized(|sers| for ser in sers {
            let behaviours = ser.objects_of_type(114);
            if behaviours.is_empty() { continue; }
            let names: BTreeMap<i64, String> = ser.objects_of_type(1).into_iter()
                .filter_map(|i| UnityObject::from_serialized_file(ser, &i).ok().and_then(|o| o.name()).map(|n| (i.path_id, n))).collect();
            for info in behaviours {
                let Ok(o) = UnityObject::from_serialized_file(ser, &info) else { continue };
                let p = o.class.properties();
                let Some(UnityValue::Array(alpha)) = p.get("rendAlpha") else { continue };
                let go = p.get("m_GameObject").and_then(|g| g.as_object()).and_then(|g| g.get("m_PathID")).and_then(|v| v.as_i64()).unwrap_or(0);
                let width = p.get("defLineWidth").and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
                if let Some(name) = names.get(&go) { found.insert(name.clone(), (width, !alpha.is_empty())); }
            }
        });
        by_bundle.insert(bundle, found);
    }
    for i in &mut c.items {
        if let Some(&(width, alpha)) = by_bundle.get(&i.bundle).and_then(|m| m.get(&i.prefab)) { i.line_width = Some(width); i.alpha = alpha; }
    }
}

/// The eyes patterns' names from the character lists (category 2).
pub fn eye_names(game: &Path) -> Vec<Group> {
    let lists = crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mut out: Vec<Group> = lists.by_id.iter().filter(|((c, _), _)| *c == crate::eye_set::CATEGORY).map(|((_, i), e)| Group { id: *i, name: e.name.trim().to_owned() }).collect();
    out.sort_by_key(|g| g.id);
    out
}

/// Adds the eye and hand pattern names to the library's studio_items.json (nothing else of it changes).
pub fn add_names(game: &Path, lib: &Path) -> Result<(usize, usize), String> {
    let file = lib.join(FILE_NAME);
    let text = std::fs::read_to_string(&file).map_err(|e| format!("{}: {e}", file.display()))?;
    // Read and written as the catalogue itself, so the rest of the file keeps its order and form.
    let mut c: Catalog = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    let fresh = catalog(game);
    c.eye_names = fresh.eye_names;
    c.hand_names = fresh.hand_names;
    write(lib, &c)?;
    Ok((c.eye_names.len(), c.hand_names.len()))
}

/// A Studio item of a sideloader mod (its zipmod's `abdata/studio/info/**/ItemList_*.csv`, the vanilla list's columns).
/// Scenes name it by the mod's GUID and its 管理番号 (StudioResolveInfo ModID and Slot); group and category are the mod's own.
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct ModItem {
    pub guid: String,
    #[serde(flatten)]
    pub item: Item,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct ModCatalog {
    pub format: String,
    /// The mods' ItemGroup and ItemCategory lists (the item browser's names for the mods' groups and categories).
    #[serde(default)]
    pub groups: Vec<Group>,
    #[serde(default)]
    pub categories: Vec<Category>,
    pub items: Vec<ModItem>,
    /// The mods' screen filters (Filter_*.csv: their own fixed numbers), bundle `<zipmod>|<bundle>`; their LUTs are
    /// recorded into the library (`record_mod_filters`).
    #[serde(default)]
    pub filters: Vec<Filter>,
}

/// Mod items live beside the shared catalogue, in a file of their own that stays local (third-party content).
pub const MOD_FILE_NAME: &str = "studio_mod_items.json";

/// `--oklib studio-mods <lib>`: every Studio item list of the installation's zipmods, each item with the library model
/// of its bundle (the converter's `converted.tsv`: mod Studio bundles are converted with the character mods). Returns
/// (items, items whose bundle is not in the library).
pub fn add_mod_items(game: &Path, lib: &Path) -> Result<(usize, usize), String> {
    use std::io::Read;
    let converted = std::fs::read_to_string(lib.join("converted.tsv")).map_err(|e| format!("converted.tsv: {e}"))?;
    let mut models: std::collections::HashMap<&str, &str> = std::collections::HashMap::new();
    for line in converted.lines() {
        let mut f = line.split('\t');
        let (Some(label), Some("ok"), _, Some(detail)) = (f.next(), f.next(), f.next(), f.next()) else { continue };
        if let Some(id) = detail.split(' ').next().filter(|id| id.len() == 32) { models.insert(label, id); }
    }
    let mods = game.join("mods");
    let mut zips = crate::mods::all_zipmods(&mods);
    zips.sort();
    let mut out = Vec::new();
    let (mut groups, mut categories, mut filters) = (BTreeMap::new(), BTreeMap::new(), BTreeMap::new());
    let mut missing = 0usize;
    for zip_path in zips {
        let Ok(file) = std::fs::File::open(&zip_path) else { continue };
        let Ok(mut zip) = zip::ZipArchive::new(file) else { continue };
        let names: Vec<String> = zip.file_names().map(str::to_owned).collect();
        let info_csv = |n: &String, prefix: &str| { let l = n.replace('\\', "/").to_ascii_lowercase(); l.starts_with("abdata/studio/info/") && l.rsplit('/').next().is_some_and(|f| f.starts_with(prefix) && f.ends_with(".csv")) };
        // Filter_<x>.csv (管理番号, 名称, bundle, LUT texture): screen filters keep their own numbers (scenes save them).
        for n in names.iter().filter(|n| info_csv(n, "filter_")) {
            let Ok(mut f) = zip.by_name(n) else { continue };
            let mut bytes = Vec::new();
            if f.read_to_end(&mut bytes).is_err() { continue; }
            let rel = zip_path.strip_prefix(game).unwrap_or(&zip_path).to_string_lossy().replace('\\', "/").to_ascii_lowercase();
            for line in String::from_utf8_lossy(&bytes).trim_start_matches('\u{feff}').lines() {
                let r: Vec<String> = line.split(',').map(|c| c.trim().to_owned()).collect();
                let (Some(no), bundle, file) = (num(r.first()), game_bundle(&cell(&r, 2)), cell(&r, 3)) else { continue };
                if bundle.is_empty() || file.is_empty() { continue; }
                filters.entry(no).or_insert(Filter { no, name: cell(&r, 1), bundle: format!("{rel}|{bundle}"), file });
            }
        }
        let lists: Vec<&String> = names.iter().filter(|n| info_csv(n, "itemlist_")).collect();
        if lists.is_empty() { continue; }
        // ItemGroup_<name>.csv (id, name) and ItemCategory_<x>_<group>.csv (id, name).
        for n in names.iter().filter(|n| info_csv(n, "itemgroup_") || info_csv(n, "itemcategory_")) {
            let Ok(mut f) = zip.by_name(n) else { continue };
            let mut bytes = Vec::new();
            if f.read_to_end(&mut bytes).is_err() { continue; }
            let file = n.replace('\\', "/").to_ascii_lowercase();
            let group = file.rsplit('/').next().unwrap_or_default().trim_end_matches(".csv").rsplit('_').next().and_then(|g| g.parse::<i32>().ok());
            for line in String::from_utf8_lossy(&bytes).trim_start_matches('\u{feff}').lines() {
                let r: Vec<String> = line.split(',').map(|c| c.trim().to_owned()).collect();
                let Some(id) = num(r.first()) else { continue };
                if file.contains("/itemgroup_") { groups.insert(id, Group { id, name: cell(&r, 1) }); }
                else if let Some(group) = group { categories.insert((group, id), Category { group, id, name: cell(&r, 1) }); }
            }
        }
        let Some(guid) = crate::mods::guids_from_zipmod(&zip_path).into_iter().next() else { continue };
        let rel = zip_path.strip_prefix(game).unwrap_or(&zip_path).to_string_lossy().replace('\\', "/").to_ascii_lowercase();
        for list in lists {
            let mut text = String::new();
            let Ok(mut f) = zip.by_name(list) else { continue };
            let mut bytes = Vec::new();
            if f.read_to_end(&mut bytes).is_err() { continue; }
            text.push_str(String::from_utf8_lossy(&bytes).trim_start_matches('\u{feff}'));
            for line in text.lines() {
                let r: Vec<String> = line.split(',').map(|c| c.trim().to_owned()).collect();
                let Some(no) = num(r.first()) else { continue };
                let bundle = game_bundle(&cell(&r, 5));
                let prefab = cell(&r, 6);
                if bundle.is_empty() || prefab.is_empty() { continue; }
                let model = models.get(format!("{rel}|{bundle}").as_str()).map(|s| s.to_string()).unwrap_or_default();
                if model.is_empty() { missing += 1; }
                out.push(ModItem { guid: guid.clone(), item: Item {
                    no, group: num(r.get(1)).unwrap_or_default(), category: num(r.get(2)).unwrap_or_default(), name: cell(&r, 3),
                    bundle: format!("{rel}|{bundle}"), prefab, model, child: cell(&r, 7), anime: flag(&r, 8),
                    colors: [flag(&r, 9), flag(&r, 11), flag(&r, 13)], patterns: [flag(&r, 10), flag(&r, 12), flag(&r, 14)], scale: flag(&r, 15),
                    line_width: None, alpha: false,
                } });
            }
        }
    }
    let n = out.len();
    let text = serde_json::to_string_pretty(&ModCatalog { format: "ok-studio-mod-items-1".into(), groups: groups.into_values().collect(), categories: categories.into_values().collect(), items: out, filters: filters.into_values().collect() }).map_err(|e| e.to_string())?;
    std::fs::write(lib.join(MOD_FILE_NAME), text).map_err(|e| format!("{MOD_FILE_NAME}: {e}"))?;
    Ok((n, missing))
}

/// `--oklib studio-mods <lib> <scene folder> [threads]`: the mod items the KStudio scenes below the folder use, whose
/// bundles the library lacks, converted into it (the converter's sources `<zipmod>|<bundle>`), then the mod item list
/// rewritten with their models. Returns (mod items the scenes use, bundles converted, items still without a model).
pub fn convert_scene_mod_items(game: &Path, lib: &Path, scenes: &Path, threads: usize) -> Result<(usize, usize, usize), String> {
    fn walk(dir: &Path, out: &mut Vec<std::path::PathBuf>) {
        for e in std::fs::read_dir(dir).into_iter().flatten().flatten() {
            let p = e.path();
            if p.is_dir() { walk(&p, out) } else if p.extension().is_some_and(|x| x.eq_ignore_ascii_case("png")) { out.push(p) }
        }
    }
    add_mod_items(game, lib)?;
    let text = std::fs::read_to_string(lib.join(MOD_FILE_NAME)).map_err(|e| e.to_string())?;
    let mods: ModCatalog = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    let by_key: std::collections::HashMap<(String, i32), &ModItem> = mods.items.iter().map(|m| ((m.guid.to_ascii_lowercase(), m.item.no), m)).collect();
    let mut files = Vec::new();
    walk(scenes, &mut files);
    let (mut used, mut labels) = (BTreeSet::new(), BTreeSet::new());
    for f in &files {
        let Ok(bytes) = std::fs::read(f) else { continue };
        for (guid, slot) in crate::studio_scene::scene_mod_items(&bytes).into_values() {
            let key = (guid.to_ascii_lowercase(), slot);
            if let Some(m) = by_key.get(&key) { if m.item.model.is_empty() { labels.insert(m.item.bundle.clone()); } }
            used.insert(key);
        }
    }
    let converted = labels.len();
    if !labels.is_empty() {
        let list = lib.join("studio_mod_sources.txt");
        std::fs::write(&list, labels.iter().cloned().collect::<Vec<_>>().join("\n")).map_err(|e| e.to_string())?;
        crate::oklib_convert::convert_with(game, lib, threads, Some(&format!("@{}", list.display())), false)?;
    }
    add_mod_items(game, lib)?;
    let text = std::fs::read_to_string(lib.join(MOD_FILE_NAME)).map_err(|e| e.to_string())?;
    let mods: ModCatalog = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    let still = mods.items.iter().filter(|m| m.item.model.is_empty() && used.contains(&(m.guid.to_ascii_lowercase(), m.item.no))).count();
    Ok((used.len(), converted, still))
}

pub fn write(lib: &Path, c: &Catalog) -> Result<(), String> {
    let text = serde_json::to_string_pretty(c).map_err(|e| e.to_string())?;
    std::fs::write(lib.join(FILE_NAME), text).map_err(|e| format!("{}: {e}", lib.join(FILE_NAME).display()))
}

/// The game's own Studio images into the library: the background pictures (`UserData/bg`, SceneInfo.background) as
/// `studio/bg/<file>`, the frames (`UserData/frame`, SceneInfo.frame) as `studio/frame/<file>`, so imported scenes
/// show them without the game. Returns how many files were copied (unchanged ones are kept).
pub fn copy_user_images(game: &Path, lib: &Path) -> Result<usize, String> {
    let mut copied = 0;
    for (from, to) in [("UserData/bg", "studio/bg"), ("UserData/frame", "studio/frame")] {
        let Ok(entries) = std::fs::read_dir(game.join(from)) else { continue };
        let dir = lib.join(to);
        std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
        for e in entries.flatten() {
            let path = e.path();
            if !path.extension().is_some_and(|x| x.eq_ignore_ascii_case("png")) { continue; }
            let target = dir.join(e.file_name());
            let bytes = std::fs::read(&path).map_err(|e| format!("{}: {e}", path.display()))?;
            if std::fs::read(&target).is_ok_and(|old| old == bytes) { continue; }
            std::fs::write(&target, bytes).map_err(|e| format!("{}: {e}", target.display()))?;
            copied += 1;
        }
    }
    // Official KStudio scenes for the OK Studio tests and comparisons (`studio/samples`): the ones the self-tests import
    // and twelve spread evenly over the sorted list (the pick OKStudio.CompareKStudio makes).
    let official = game.join("UserData/Studio/scene/Official");
    let mut names: Vec<String> = std::fs::read_dir(&official).map(|it| it.flatten().map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n.to_ascii_lowercase().ends_with(".png")).collect()).unwrap_or_default();
    names.sort();
    let mut picks: Vec<String> = ["koikatu_cs0000010.png", "koikatu_cs0000059.png"].iter().map(|s| s.to_string()).collect();
    if !names.is_empty() { for i in 0..12usize { picks.push(names[i * names.len() / 12].clone()); } }
    let dir = lib.join("studio/samples");
    std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    for name in picks {
        let Ok(bytes) = std::fs::read(official.join(&name)) else { continue };
        let target = dir.join(&name);
        if std::fs::read(&target).is_ok_and(|old| old == bytes) { continue; }
        std::fs::write(&target, bytes).map_err(|e| format!("{}: {e}", target.display()))?;
        copied += 1;
    }
    Ok(copied)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    #[ignore = "installed game: reads the vanilla studio/info lists"]
    fn vanilla_catalog() {
        let root = std::env::var("OK_GAME_ROOT").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned());
        let c = catalog(Path::new(&root));
        let bundles: BTreeSet<_> = c.items.iter().map(|i| i.bundle.clone()).collect();
        println!("groups={} categories={} items={} maps={} lights={} item_bundles={}", c.groups.len(), c.categories.len(), c.items.len(), c.maps.len(), c.lights.len(), bundles.len());
        for b in &bundles { println!("  {b} {}", c.items.iter().filter(|i| &i.bundle == b).count()); }
        let missing: Vec<_> = bundles.iter().filter(|b| !Path::new(&root).join(b).exists()).collect();
        println!("missing bundles: {missing:?}");
        for m in &c.maps { println!("  map {} {} {} {}", m.no, m.name, m.bundle, m.prefab); }
        assert!(c.items.len() > 1000 && !c.maps.is_empty() && c.lights.len() >= 3);
    }
}



/// Record Standard map texture dependencies and sampler provenance omitted by
/// old character-oriented mesh snapshots. Source map identities stay unchanged.
pub fn record_standard_map_textures(game: &Path, lib: &Path, filter: Option<&str>) -> Result<(), String> {
    let text=std::fs::read_to_string(lib.join(FILE_NAME)).map_err(|e|e.to_string())?;
    let catalog:Catalog=serde_json::from_str(&text).map_err(|e|e.to_string())?;
    let store=crate::oklib::activate(lib,crate::oklib::Mode::Record)?;
    crate::gamefs::activate(crate::gamefs::Mode::Record,game,lib)?;
    let was_converting=crate::oklib::CONVERTING.swap(true,std::sync::atomic::Ordering::Relaxed);
    let result=(||{
        let mut bundles:BTreeMap<String,BTreeSet<String>>=BTreeMap::new();
        for map in &catalog.maps {
            if filter.is_some_and(|f|!f.split(',').any(|name|map.bundle.ends_with(name))) {continue;}
            for root in &map.roots {bundles.entry(map.bundle.clone()).or_default().insert(root.clone());}
        }
        if bundles.is_empty(){return Err("no matching source map bundles".into());}
        let mut total=0;
        for (bundle,_roots) in bundles {
            let bytes=crate::gamefs::read(game.join(&bundle)).map_err(|e|format!("{bundle}: {e}"))?;
            let id=crate::oklib::content_id(&bytes);let parsed=crate::unity::parse_bundle_cached(bytes);parsed.configure_game_references(game);
            // Resolve native material PPtrs without rebuilding vertex/skin
            // streams or decoding every old mesh texture during metadata work.
            let names=parsed.standard_material_texture_dependencies();
            let mut found=0;
            for name in names {if parsed.texture_png_named(&name).is_some(){parsed.texture_sampling_named(&name);found+=1;}else{return Err(format!("Standard map texture missing: {bundle} / {name}"));}}
            println!("STANDARD MAP TEXTURES {bundle}: {found}");total+=found;
            store.forget_model(&id);
        }
        println!("STANDARD MAP TEXTURES total={total}");Ok(())
    })();
    store.flush();let saved=crate::gamefs::save(lib);crate::unity::clear_bundle_cache();crate::oklib::deactivate();crate::gamefs::deactivate();
    crate::oklib::CONVERTING.store(was_converting,std::sync::atomic::Ordering::Relaxed);
    result.and(saved)
}

/// Record prefab hierarchy, lightmaps and renderer policies for Studio assets.
pub fn record_hierarchies(game: &Path, lib: &Path, threads: usize) -> Result<(), String> {
    use rayon::prelude::*;
    let text = std::fs::read_to_string(lib.join(FILE_NAME)).map_err(|e| format!("{FILE_NAME}: {e}"))?;
    let c: Catalog = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    let mut bundles: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
    for (bundle, prefab) in c.items.iter().map(|i| (&i.bundle, &i.prefab)).chain(c.lights.iter().map(|l| (&l.bundle, &l.prefab)))
        .chain(c.maps.iter().flat_map(|m| m.roots.iter().map(move |r| (&m.bundle, r)))) {
        if !bundle.is_empty() && !prefab.is_empty() { bundles.entry(bundle.clone()).or_default().insert(prefab.clone()); }
    }
    let started = std::time::Instant::now();
    let store = crate::oklib::activate(lib, crate::oklib::Mode::Record)?;
    crate::oklib::CONVERTING.store(true, std::sync::atomic::Ordering::Relaxed);
    crate::gamefs::activate(crate::gamefs::Mode::Record, game, lib)?;
    let totals = std::sync::Mutex::new((0usize, 0usize, 0usize, 0usize));
    let list: Vec<_> = bundles.into_iter().collect();
    let pool = rayon::ThreadPoolBuilder::new().num_threads(threads.max(1)).build().map_err(|e| e.to_string())?;
    pool.install(|| list.par_iter().for_each(|(bundle, prefabs)| {
        let Ok(bytes) = crate::gamefs::read(game.join(bundle)) else { println!("STUDIO NODES missing {bundle}"); return; };
        let id = crate::oklib::content_id(&bytes);
        let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let parsed = crate::unity::parse_bundle_cached(bytes);
            parsed.configure_game_references(game);
            let (mut n, mut l, mut empty) = (0, 0, 0);
            for p in prefabs {
                let h = parsed.prefab_hierarchy(p);
                n += h.nodes.len(); l += h.lights.len();
                if h.nodes.is_empty() { empty += 1; }
                if !parsed.prefab_lightmaps(p).textures.is_empty() { parsed.prefab_lightmap_directions(p); }
                parsed.prefab_renderer_settings(p);
                parsed.prefab_render_textures(p); parsed.prefab_reflection_probes(p); parsed.prefab_animated_materials(p); parsed.prefab_renderer_visibility(p); parsed.prefab_sun_light(p); parsed.prefab_light_modes(p);
                // The prefab's meshes, where already recorded: OKLIB_REFRESH=meshes_for_maindata records them again
                // after a decoding fix (main UV: TexCoord0 over the lightmap's TexCoord1).
                for disabled in [false, true] {
                    if !crate::oklib::has_record(Some(&id), "meshes_for_maindata", &(p.as_str(), disabled)) { continue; }
                    if disabled { parsed.meshes_for_maindata_including_disabled(p); } else { parsed.meshes_for_maindata(p); }
                }
            }
            (n, l, empty)
        }));
        crate::unity::clear_bundle_cache();
        store.forget_model(&id);
        let mut t = totals.lock().unwrap();
        match r { Ok((n, l, e)) => { t.0 += 1; t.1 += n; t.2 += l; t.3 += e; } Err(_) => println!("STUDIO NODES panic {bundle}") }
        if t.0 % 25 == 0 { store.flush(); println!("STUDIO NODES {}/{} {:.0}s", t.0, list.len(), started.elapsed().as_secs_f64()); }
    }));
    store.flush();
    crate::gamefs::save(lib)?;
    crate::oklib::deactivate();
    crate::gamefs::deactivate();
    let t = totals.into_inner().unwrap();
    println!("STUDIO NODES bundles={}/{} nodes={} lights={} prefabs_without_nodes={} {:.0}s", t.0, list.len(), t.1, t.2, t.3, started.elapsed().as_secs_f64());
    Ok(())
}

/// `--oklib studio-mod-nodes <lib> <scene folder> [threads]`: what `record_hierarchies` records for vanilla items, for the
/// mod items the KStudio scenes below the folder use (their bundles were converted as character sources, which records
/// no node tree, render-to-texture cameras or screens). Returns (bundles, prefabs, render cameras found).
pub fn record_scene_mod_nodes(game: &Path, lib: &Path, scenes: &Path, threads: usize) -> Result<(usize, usize, usize), String> {
    use rayon::prelude::*;
    use std::io::Read;
    fn walk(dir: &Path, out: &mut Vec<std::path::PathBuf>) {
        for e in std::fs::read_dir(dir).into_iter().flatten().flatten() {
            let p = e.path();
            if p.is_dir() { walk(&p, out) } else if p.extension().is_some_and(|x| x.eq_ignore_ascii_case("png")) { out.push(p) }
        }
    }
    let text = std::fs::read_to_string(lib.join(MOD_FILE_NAME)).map_err(|e| format!("{MOD_FILE_NAME}: {e} (run studio-mods first)"))?;
    let mods: ModCatalog = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    let by_key: std::collections::HashMap<(String, i32), &ModItem> = mods.items.iter().map(|m| ((m.guid.to_ascii_lowercase(), m.item.no), m)).collect();
    let mut files = Vec::new();
    walk(scenes, &mut files);
    let mut bundles: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
    for f in &files {
        let Ok(bytes) = std::fs::read(f) else { continue };
        for (guid, slot) in crate::studio_scene::scene_mod_items(&bytes).into_values() {
            if let Some(m) = by_key.get(&(guid.to_ascii_lowercase(), slot)).filter(|m| !m.item.model.is_empty()) {
                bundles.entry(m.item.bundle.clone()).or_default().insert(m.item.prefab.clone());
            }
        }
    }
    let started = std::time::Instant::now();
    let store = crate::oklib::activate(lib, crate::oklib::Mode::Record)?;
    crate::oklib::CONVERTING.store(true, std::sync::atomic::Ordering::Relaxed);
    crate::gamefs::activate(crate::gamefs::Mode::Record, game, lib)?;
    let totals = std::sync::Mutex::new((0usize, 0usize, 0usize));
    let list: Vec<_> = bundles.into_iter().collect();
    let pool = rayon::ThreadPoolBuilder::new().num_threads(threads.max(1)).build().map_err(|e| e.to_string())?;
    pool.install(|| list.par_iter().for_each(|(label, prefabs)| {
        let Some((zip, entry)) = label.split_once('|') else { return };
        let zip_path = game.join(zip);
        let bytes = crate::gamefs::open_zip(&zip_path).and_then(|mut z| {
            let name = z.file_names().into_iter().find(|n| n.replace('\\', "/").eq_ignore_ascii_case(entry))?;
            let mut f = z.by_name(&name).ok()?;
            let mut b = Vec::new();
            f.read_to_end(&mut b).ok().map(|_| b)
        });
        let Some(bytes) = bytes else { println!("STUDIO MOD NODES missing {label}"); return };
        let id = crate::oklib::content_id(&bytes);
        let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let parsed = crate::unity::parse_bundle_cached(bytes);
            parsed.configure_game_references_from_zip(game, Some(&zip_path));
            let mut cameras = 0;
            for p in prefabs {
                let _ = parsed.prefab_hierarchy(p);
                let _ = parsed.prefab_renderer_settings(p);
                cameras += parsed.prefab_render_textures(p).cameras.len();
                let _ = parsed.prefab_animated_materials(p);
                let _ = parsed.prefab_renderer_visibility(p);
            }
            cameras
        }));
        crate::unity::clear_bundle_cache();
        store.forget_model(&id);
        let mut t = totals.lock().unwrap();
        match r { Ok(c) => { t.0 += 1; t.1 += prefabs.len(); t.2 += c; } Err(_) => println!("STUDIO MOD NODES panic {label}") }
    }));
    store.flush();
    crate::gamefs::save(lib)?;
    crate::oklib::deactivate();
    crate::gamefs::deactivate();
    let t = totals.into_inner().unwrap();
    println!("STUDIO MOD NODES bundles={}/{} prefabs={} render cameras={} {:.0}s", t.0, list.len(), t.1, t.2, started.elapsed().as_secs_f64());
    Ok(t)
}

/// `--oklib studio-check <lib>`: every catalogue item and map shows meshes from the library alone (replay).
pub fn check(lib: &Path) -> Result<(), String> {
    let text = std::fs::read_to_string(lib.join(FILE_NAME)).map_err(|e| format!("{FILE_NAME}: {e}"))?;
    let c: Catalog = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    let started = std::time::Instant::now();
    crate::okmodel_view::with_library(lib, || {
        let mut views: BTreeMap<String, Option<crate::okmodel_view::ModelView>> = BTreeMap::new();
        let mut meshes = |model: &str, prefab: &str| -> usize {
            let v = views.entry(model.to_owned()).or_insert_with(|| crate::oklib::id_from_hex_pub(model).and_then(|id| crate::okmodel_view::ModelView::open(lib, id).ok()));
            v.as_ref().map_or(0, |v| v.prefab_meshes(prefab, false).len())
        };
        let (mut ok, mut bad, mut silent, mut effects) = (0usize, Vec::new(), 0usize, 0usize);
        let group_name = |g: i32| c.groups.iter().find(|x| x.id == g).map(|x| x.name.as_str()).unwrap_or_default();
        let category_name = |g: i32, k: i32| c.categories.iter().find(|x| x.group == g && x.id == k).map(|x| x.name.as_str()).unwrap_or_default();
        for i in &c.items {
            if meshes(&i.model, &i.prefab) > 0 { ok += 1; continue; }
            // Not drawn in the game either: 3D sound emitters and object-less gimmicks. Particle effects are drawn by
            // ParticleSystems (no meshes yet).
            if group_name(i.group) == "3DSE" || category_name(i.group, i.category).contains("obj無し") { silent += 1; continue; }
            if group_name(i.group) == "エフェクト" { effects += 1; continue; }
            bad.push(format!("item {}/{}/{} {} {} {}", i.group, i.category, i.no, i.name, i.bundle, i.prefab));
        }
        let (mut maps_ok, mut map_parts) = (0usize, 0usize);
        for m in &c.maps {
            let parts: usize = m.roots.iter().map(|r| meshes(&m.model, r)).sum();
            if parts > 0 { maps_ok += 1; map_parts += parts; } else { bad.push(format!("map {} {} {} {:?}", m.no, m.name, m.bundle, m.roots)); }
        }
        for b in &bad { println!("STUDIO MISSING {b}"); }
        println!("STUDIO CHECK items={ok}/{} sound_or_gimmick={silent} particle_effects={effects} maps={maps_ok}/{} map_meshes={map_parts} missing={} {:.0}s", c.items.len(), c.maps.len(), bad.len(), started.elapsed().as_secs_f64());
        if bad.is_empty() { Ok(()) } else { Err(format!("{} studio entries without meshes", bad.len())) }
    })
}


#[cfg(test)]
mod hierarchy_tests {
    #[test]
    #[ignore = "OK base library with studio-nodes recorded: catalogue child points resolve from the library alone"]
    fn child_points_from_the_library() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let c: super::Catalog = serde_json::from_str(&std::fs::read_to_string(lib.join(super::FILE_NAME)).unwrap()).unwrap();
        let with_child: Vec<_> = c.items.iter().filter(|i| !i.child.is_empty() && !i.model.is_empty()).collect();
        let (mut found, mut missing) = (0usize, Vec::new());
        crate::okmodel_view::with_library(&lib, || {
            for i in &with_child {
                let view = crate::okmodel_view::ModelView::open(&lib, crate::oklib::id_from_hex_pub(&i.model).unwrap())?;
                let h = view.hierarchy(&i.prefab);
                match h.find(&i.child).and_then(|n| h.node_world(n)) {
                    Some(w) => { if found < 3 { println!("{} {} -> {} at {:?}", i.name, i.prefab, i.child, w.0); } found += 1; }
                    None => missing.push(format!("{}/{}/{} {} {} nodes={}", i.group, i.category, i.no, i.prefab, i.child, h.nodes.len())),
                }
            }
            Ok(())
        }).unwrap();
        println!("child points {found}/{}; missing {:?}", with_child.len(), &missing[..missing.len().min(10)]);
        assert!(!with_child.is_empty() && missing.len() * 20 < with_child.len(), "child points resolve");
    }

    #[test]
    #[ignore = "OK base library with studio-nodes recorded: item FK turns a rigid item's meshes (15/74/979)"]
    fn rigid_item_fk() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let c: super::Catalog = serde_json::from_str(&std::fs::read_to_string(lib.join(super::FILE_NAME)).unwrap()).unwrap();
        let item = c.items.iter().find(|i| (i.group, i.category, i.no) == (15, 74, 979)).expect("item 15/74/979");
        crate::okmodel_view::with_library(&lib, || {
            let view = crate::okmodel_view::ModelView::open(&lib, crate::oklib::id_from_hex_pub(&item.model).unwrap())?;
            let h = view.hierarchy(&item.prefab);
            let bones = view.fk_bones(&item.prefab);
            let rigid: Vec<_> = view.prefab_meshes(&item.prefab, false).iter().filter(|m| !m.uses_skin_renderer()).map(|m| m.name.clone()).collect();
            println!("{} {}: nodes {} mesh nodes {} rigid {:?} fk {:?}", item.name, item.prefab, h.nodes.len(), h.mesh_nodes.len(), rigid, bones);
            let centre = |s: &crate::session::OkSession| {
                let (mut sum, mut n) = ([0f64; 3], 0usize);
                for p in &s.parts { for v in &p.positions { for a in 0..3 { sum[a] += v[a] as f64; } n += 1; } }
                (sum.map(|x| x / n.max(1) as f64), n)
            };
            let (c0, n0) = centre(&view.prefab_session(&item.prefab, false)?);
            let mut moved = Vec::new();
            for b in &bones {
                let (c1, _) = centre(&view.prefab_session_fk(&item.prefab, false, &[(b.clone(), [0., 0., 60.])])?);
                let d = ((c1[0] - c0[0]).powi(2) + (c1[1] - c0[1]).powi(2) + (c1[2] - c0[2]).powi(2)).sqrt();
                if d > 0.005 { moved.push((b.clone(), d)); }
            }
            println!("vertices {n0}; bones moving the centre: {moved:?}");
            assert!(!rigid.is_empty() && !moved.is_empty(), "turning an FK node moves the rigid meshes");
            Ok(())
        }).unwrap();
    }
}

#[cfg(test)]
mod map_rigid_probe {
    #[test]
    #[ignore = "OK base library: how far map rigid meshes sit from the map origin (prefab matrix)"]
    fn map_rigid_offsets() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let c: super::Catalog = serde_json::from_str(&std::fs::read_to_string(lib.join(super::FILE_NAME)).unwrap()).unwrap();
        crate::okmodel_view::with_library(&lib, || {
            for m in c.maps.iter().take(3) {
                let view = crate::okmodel_view::ModelView::open(&lib, crate::oklib::id_from_hex_pub(&m.model).unwrap())?;
                for r in m.roots.iter().take(2) {
                    let meshes = view.prefab_meshes(r, false);
                    let rigid: Vec<_> = meshes.iter().filter(|x| !x.uses_skin_renderer()).collect();
                    let far = rigid.iter().filter(|x| x.prefab_local.as_ref().is_some_and(|l| { let p = crate::pose::mat_pos(l); (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]).sqrt() > 0.5 })).count();
                    let span = |v: &Vec<[f32; 3]>| v.iter().fold(0f32, |a, p| a.max(p[0].abs()).max(p[2].abs()));
                    let max_vertex = rigid.iter().map(|x| span(&x.positions)).fold(0f32, f32::max);
                    let h = view.hierarchy(r);
                    let session = view.prefab_session(r, false)?;
                    let extra = session.parts.iter().filter(|p| p.name.contains('#')).count();
                    println!("map {} {} root {r}: meshes {} rigid {} far-from-origin {far} max |vertex| {max_vertex:.1} m; nodes {} mesh instances {} parts {} (extra instances {extra})", m.no, m.name, meshes.len(), rigid.len(), h.nodes.len(), h.mesh_nodes.len(), session.parts.len());
                    let combined: Vec<_> = meshes.iter().filter(|x| x.name.contains("Combined")).map(|x| (x.name.clone(), x.prefab_local.as_ref().map(crate::pose::mat_pos), h.mesh_nodes.iter().filter(|(i, _)| *i == x.path_id).count())).take(5).collect();
                    println!("  combined meshes {} {:?}", meshes.iter().filter(|x| x.name.contains("Combined")).count(), combined);
                    assert!(h.nodes.len() > 0 && extra > 0, "map hierarchy recorded and shared meshes drawn at every instance");
                    for x in rigid.iter().filter(|x| x.prefab_local.as_ref().is_some_and(|l| crate::pose::mat_pos(l).iter().any(|v| v.abs() > 0.5))).take(6) { let s = span(&x.positions); println!("  far {} renderers {:?} local {:?} vertex span {s:.2}", x.name, x.renderer_names, x.prefab_local.as_ref().map(crate::pose::mat_pos)); }
                }
            }
            Ok(())
        }).unwrap();
    }
}

#[cfg(test)]
mod pattern_tests {
    #[test]
    #[ignore = "OK base library: Studio item patterns (mt_pattern) list and decode from the library alone"]
    fn patterns_from_the_library() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let list = crate::okmodel_view::with_library(&lib, || Ok(crate::studio_patterns(&lib))).unwrap();
        println!("{} patterns, first {:?}", list.len(), list.iter().take(3).collect::<Vec<_>>());
        let mut decoded = 0;
        let vanilla = crate::list::load_characustom_dir(&lib.join("abdata/list/characustom"));
        for e in vanilla.entries().filter(|e| e.category_no == 430).take(3) { println!("  {} thumb {} / {} main {} / {}", e.id, e.info("ThumbAB"), e.info("ThumbTex"), e.info("MainTexAB"), e.info("MainTex")); }
        for (id, _) in list.iter().take(5) {
            let png = crate::okmodel_view::with_library(&lib, || crate::studio_pattern_png(&lib, *id).ok_or("none".to_string()));
            if let Ok(b) = png { let i = crate::teximg::load_from_memory(&b).unwrap(); println!("pattern {id}: {}x{}", i.width(), i.height()); decoded += 1; }
        }
        assert!(list.len() > 10 && decoded >= 3, "patterns list and decode");
    }
}

#[cfg(test)]
mod accessory_point_tests {
    use super::*;
    /// Adds the AccessoryPoint sheet to the library's studio_items.json without a full conversion (the catalogue
    /// reads the recorded studio/info lists; models and the other lists stay as they are).
    #[test]
    #[ignore = "OK base library: rewrites studio_items.json with the accessory points"]
    fn refresh_accessory_points() {
        let lib = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let fresh = crate::okmodel_view::with_library(&lib, || Ok(catalog(&lib))).unwrap();
        println!("{} accessory points, {} items", fresh.accessory_points.len(), fresh.items.len());
        let game = &crate::test_support::game_path("");
        let fresh = if fresh.accessory_points.is_empty() && game.exists() { catalog(game) } else { fresh };
        println!("{} accessory points from the game", fresh.accessory_points.len());
        for p in fresh.accessory_points.iter().take(80) { println!("  {} g{} {} {} -> {}", p.no, p.group, p.key, p.name, p.bone); }
        assert!(fresh.accessory_points.len() > 20, "AccessoryPoint sheet");
        let mut c: Catalog = serde_json::from_slice(&std::fs::read(lib.join(FILE_NAME)).unwrap()).unwrap();
        c.accessory_points = fresh.accessory_points;
        c.filters = fresh.filters;
        write(&lib, &c).unwrap();
    }
}

#[cfg(test)]
mod sky_dome_tests {
    /// Studio's 青空 item: a built-in Unity Sphere (radius 0.5, Library/unity default resources) with Skybox/Cubemap,
    /// plus Standard cloud meshes. OK builds the sphere and carries `_Tex` as the cube atlas `_OKSkyCube`.
    #[test]
    #[ignore = "installed game: studio/00.unity3d"]
    fn sky_dome_item_has_sphere_and_cube_atlas() {
        let root = &crate::test_support::game_path("");
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join("abdata/studio/00.unity3d")).unwrap());
        parsed.configure_game_references(root);
        let meshes = parsed.meshes_for_maindata("p_koi_stu_skyhiru00_01").0;
        let h = parsed.prefab_hierarchy(&std::env::var("OK_SKY_PREFAB").unwrap_or("p_koi_stu_skyhiru00_01".into()));
        for (i, n) in h.nodes.iter().enumerate() { println!("node {i} {} parent={} pos={:?} rot={:?} scale={:?}", n.name, n.parent, n.pos, n.rot, n.scale); }
        println!("mesh_nodes {:?}", h.mesh_nodes);
        for m in &meshes { println!("mesh {} path_id={} prefab_local={:?} v0={:?}", m.name, m.path_id, m.prefab_local.is_some(), m.positions.first()); }
        for m in &meshes { println!("{} verts={} shader={:?} extra={:?} slots={:?}", m.name, m.positions.len(), m.mat.as_ref().map(|x| &x.shader), m.shader_extra_textures.keys().collect::<Vec<_>>(), m.mat.as_ref().map(|x| &x.tex_slots)); }
        let dome = meshes.iter().find(|m| m.mat.as_ref().is_some_and(|x| x.shader == "Skybox/Cubemap")).expect("sky dome part");
        assert_eq!(dome.name, "Sphere");
        assert!(dome.positions.iter().all(|p| ((p[0] * p[0] + p[1] * p[1] + p[2] * p[2]).sqrt() - 0.5).abs() < 1e-4));
        let png = dome.shader_extra_textures.get("_OKSkyCube").expect("cube atlas");
        if let Ok(out) = std::env::var("OK_SKY_ATLAS_OUT") { std::fs::write(out, png).unwrap(); }
        let atlas = crate::teximg::load_from_memory(png).unwrap();
        assert_eq!(atlas.width(), atlas.height() * 3);
        assert_eq!(dome.mat.as_ref().unwrap().floats.get("_OKSkybox"), Some(&1.0));
    }
}

#[cfg(test)]
mod dependency_material_tests {
    /// Studio items keep their materials in a dependency bundle (`m_FileID` != 0): the sphere gets
    /// Shader Forge/main_item_studio with its colours and ColorMask.
    #[test]
    #[ignore = "installed game: studio/00.unity3d and its material bundle"]
    fn studio_item_material_from_dependency_bundle() {
        let root = &crate::test_support::game_path("");
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join("abdata/studio/00.unity3d")).unwrap());
        parsed.configure_game_references(root);
        let meshes = parsed.meshes_for_maindata("p_koi_stu_sphere01_02").0;
        for m in &meshes {
            println!("{} mat={:?} shader={:?} colors={:?} mask={:?} tex={:?}", m.name, m.mat.as_ref().map(|x| &x.name), m.mat.as_ref().map(|x| &x.shader),
                m.mat.as_ref().map(|x| x.colors.keys().cloned().collect::<Vec<_>>()), m.color_mask_png.as_ref().map(|v| v.len()), m.texture_png.as_ref().map(|v| v.len()));
        }
        let mat = meshes.first().and_then(|m| m.mat.clone()).expect("sphere material");
        for k in ["_Color", "_Color2", "_Color3", "_Color1_2", "_Color2_2", "_Color3_2"] { println!("  {k} = {:?}", mat.colors.get(k)); }
        println!("  floats {:?}", mat.floats.iter().filter(|(k, _)| !k.ends_with("_ST_x") && !k.ends_with("_ST_y") && !k.ends_with("_ST_z") && !k.ends_with("_ST_w")).collect::<std::collections::BTreeMap<_, _>>());
        if let Some(mask) = meshes[0].color_mask_png.as_ref().and_then(|p| crate::teximg::load_from_memory(p).ok()) {
            let rgba = mask.to_rgba8();
            let px: std::collections::BTreeSet<[u8; 4]> = rgba.pixels().map(|p| p.0).collect();
            println!("  mask {}x{} distinct pixels {:?}", rgba.width(), rgba.height(), px.iter().take(8).collect::<Vec<_>>());
        }
        assert!(mat.shader.to_ascii_lowercase().starts_with("shader forge/main_item_studio"), "shader {}", mat.shader);
        assert!(mat.colors.contains_key("_Color") && mat.colors.contains_key("_Color2"));
    }
}
#[cfg(test)]
mod map_texture_probe {
    #[test]
    #[ignore = "installed game: a Studio map root's materials and textures"]
    fn map_root_textures() {
        let root = &crate::test_support::game_path("");
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join("abdata/studio/map/1f.unity3d")).unwrap());
        parsed.configure_game_references(root);
        parsed.with_serialized(|sers| for s in sers { println!("ser externals={:?} textures={} materials={}", s.externals.iter().map(|e| e.path.clone()).collect::<Vec<_>>(), s.objects_of_type(28).len(), s.objects_of_type(21).len()); });
        let meshes = parsed.meshes_for_maindata("Map").0;
        for m in meshes.iter().take(6) {
            println!("{} mat={:?} slots={:?} tex={:?}", m.name, m.mat.as_ref().map(|x| (&x.name, &x.shader)), m.mat.as_ref().map(|x| x.tex_slots.clone()), m.texture_png.as_ref().map(|v| v.len()));
        }
    }
}
#[cfg(test)]
mod sphere_mask_decode {
    /// The basic shapes' ColorMask is solid red (every DXT1 block f800 f800): decoded from its own bundle and as a
    /// dependency texture of studio/00 it must come out red.
    #[test]
    #[ignore = "installed game: decodes the basic-shape ColorMask both ways"]
    fn sphere_mask_is_red() {
        let pixels = |png: &[u8]| { let i = crate::teximg::load_from_memory(png).unwrap().to_rgba8(); i.pixels().map(|p| p.0).collect::<std::collections::BTreeSet<_>>() };
        let own = crate::unity::parse_bundle_cached(std::fs::read(crate::test_support::game_path("/abdata/studio/mat/00.unity3d")).unwrap());
        let a = own.texture_png_named("t_koi_stu_kihon00mc_02").expect("own decode");
        println!("own bundle: {:?}", pixels(&a));
        let root = &crate::test_support::game_path("");
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join("abdata/studio/00.unity3d")).unwrap());
        parsed.configure_game_references(root);
        let b = parsed.meshes_for_maindata("p_koi_stu_sphere01_02").0[0].color_mask_png.clone().expect("dependency decode");
        println!("dependency: {:?}", pixels(&b));
        let red: std::collections::BTreeSet<[u8; 4]> = [[255, 0, 0, 255]].into();
        assert!(pixels(&a) == red && pixels(&b) == red, "solid red ColorMask");
    }
}

#[cfg(test)]
mod scene_item_probe {
    use super::*;
    /// The catalogue items of one KStudio scene (OK_SCENE, default the bazooka scene) with their library model.
    #[test]
    #[ignore = "installed game + OK base library: lists a scene's items"]
    fn scene_items() {
        let scene = std::env::var("OK_SCENE").unwrap_or_else(|_| crate::test_support::game_path("/UserData/Studio/scene/Official/koikatu_cs0002534.png").to_string_lossy().into_owned());
        let out = std::env::temp_dir().join("ok_scene_probe");
        let json = crate::studio_scene::export(std::path::Path::new(&scene), &out).unwrap();
        let lib = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let c: Catalog = serde_json::from_slice(&std::fs::read(lib.join(FILE_NAME)).unwrap()).unwrap();
        fn walk(v: &serde_json::Value, c: &Catalog, depth: usize) {
            match v {
                serde_json::Value::Object(m) => {
                    if let Some(k) = m.get("kind").and_then(|k| k.as_str()) {
                        if k == "Camera" { println!("{}Camera {}", "  ".repeat(depth), serde_json::to_string(m).unwrap_or_default().chars().take(300).collect::<String>()); }
                        if k == "Character" { println!("{}Character hand={} status={}", "  ".repeat(depth), m.get("hand").map(|v| v.to_string()).unwrap_or_default(), m.get("status").map(|v| v.to_string()).unwrap_or_default()); }
                        let (g, cat, no) = (m.get("group").and_then(|x| x.as_i64()), m.get("category").and_then(|x| x.as_i64()), m.get("no").and_then(|x| x.as_i64()));
                        let item = c.items.iter().find(|i| Some(i.group as i64) == g && Some(i.category as i64) == cat && Some(i.no as i64) == no);
                        if let (Ok(want), Some(i)) = (std::env::var("OK_ITEM_FULL"), item) { if i.name.contains(want.as_str()) { let mut s = m.clone(); s.remove("child"); s.remove("children"); println!("FULL {}", serde_json::to_string(&s).unwrap_or_default()); } }
                        println!("{}{k} {:?}/{:?}/{:?} {:?} colors={}", "  ".repeat(depth), g, cat, no, item.map(|i| &i.name), m.get("colors").map(|c| c.to_string().chars().take(160).collect::<String>()).unwrap_or_default()); if let (Some(_), Some(ch)) = (m.get("alpha"), m.get("change")) { println!("{}  pos={} rot={} scale={}", "  ".repeat(depth), ch.get("pos").map(|v| v.to_string()).unwrap_or_default(), ch.get("rot").map(|v| v.to_string()).unwrap_or_default(), ch.get("scale").map(|v| v.to_string()).unwrap_or_default()); } if let (Some(a), true) = (m.get("alpha"), false) { println!("{}  alpha={a} line={} w={} emission={} power={} cancel={}", "  ".repeat(depth), m.get("line_color").map(|v| v.to_string()).unwrap_or_default(), m.get("line_width").map(|v| v.to_string()).unwrap_or_default(), m.get("emission_color").map(|v| v.to_string()).unwrap_or_default(), m.get("emission_power").map(|v| v.to_string()).unwrap_or_default(), m.get("light_cancel").map(|v| v.to_string()).unwrap_or_default()); } if false { println!("{}{}", "  ".repeat(depth), m.get("change").and_then(|c| c.get("scale")).map(|s| s.to_string()).unwrap_or_default()); }
                    }
                    for (_, x) in m { walk(x, c, depth + 1); }
                }
                serde_json::Value::Array(a) => for x in a { walk(x, c, depth); },
                _ => {}
            }
        }
        walk(&json["objects"], &c, 0);
        println!("map {:?}", json.get("map"));
        println!("effects {}", json.get("effects").map(|c| c.to_string()).unwrap_or_default());
        println!("camera {}", json.get("camera").map(|c| c.to_string().chars().take(400).collect::<String>()).unwrap_or_default());
    }
}

#[cfg(test)]
mod filter_tests {
    use super::*;
    /// The Studio filters' Amplify Color LUTs come out of the library (1024x32: 32 blue slices of 32x32).
    #[test]
    #[ignore = "OK base library: decodes the filter LUTs"]
    fn filter_luts_from_the_library() {
        let lib = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let c: Catalog = serde_json::from_slice(&std::fs::read(lib.join(FILE_NAME)).unwrap()).unwrap();
        assert!(!c.filters.is_empty(), "filters in studio_items.json (refresh_accessory_points refreshes them)");
        for f in &c.filters {
            let png = crate::okmodel_view::with_library(&lib, || texture_png(&lib, &f.bundle, &f.file).ok_or("none".to_string())).unwrap();
            let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
            let (top, bottom) = (img.get_pixel(0, 0).0, img.get_pixel(0, img.height() - 1).0);
            println!("{} {} {}: {}x{} top-left {:?} bottom-left {:?} (31,0) {:?} (32,0) {:?} (1023,0) {:?} (1023,31) {:?}", f.no, f.name, f.file, img.width(), img.height(), top, bottom, img.get_pixel(31, 0).0, img.get_pixel(32, 0).0, img.get_pixel(1023, 0).0, img.get_pixel(1023, 31).0);
            let at = |r: u32, g: u32, b: u32| img.get_pixel(r + 32 * b, g).0;
            println!("   grey16 {:?} grey20 {:?} olive(14,16,13) {:?} grey(17,18,18) {:?}", at(16, 16, 16), at(20, 20, 20), at(14, 16, 13), at(17, 18, 18));
            assert_eq!((img.width(), img.height()), (1024, 32));
        }
    }
}

#[cfg(test)]
mod shadow_only_probe {
    /// Renderers of a Studio map bundle by cast-shadows mode (3 = ShadowsOnly: drawn into shadows, never seen).
    #[test]
    #[ignore = "installed game: cast-shadow modes of a map's renderers"]
    fn map_shadow_only_renderers() {
        use unity_asset_binary::object::UnityObject;
        let path = std::env::var("OK_MAP_BUNDLE").unwrap_or_else(|_| crate::test_support::game_path("/abdata/studio/map/1ftoilet.unity3d").to_string_lossy().into_owned());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(&path).unwrap());
        let mut modes = std::collections::BTreeMap::<String, usize>::new();
        parsed.with_serialized(|sers| for ser in sers {
            if std::env::var("OK_MAP_LIST").is_ok() {
                println!("externals {:?}", ser.externals.iter().map(|e| e.path.clone()).collect::<Vec<_>>());
                let shaders: std::collections::HashMap<i64, String> = ser.objects_of_type(48).into_iter().filter_map(|i| UnityObject::from_serialized_file(ser, &i).ok().map(|o| (i.path_id, crate::unity::shader_runtime_name(&o)))).collect();
                for i in ser.objects_of_type(21) { if let Ok(m) = UnityObject::from_serialized_file(ser, &i) {
                    let s = m.class.properties().get("m_Shader").and_then(|s| s.as_object()).map(|g| (g.get("m_FileID").and_then(|v| v.as_i64()).unwrap_or(0), g.get("m_PathID").and_then(|v| v.as_i64()).unwrap_or(0))).unwrap_or_default();
                    println!("material {} {} shader {:?} {}", i.path_id, m.name().unwrap_or_default(), s, shaders.get(&s.1).cloned().unwrap_or_default());
                    if std::env::var("OK_MAP_MATERIAL").is_ok_and(|w| m.name().unwrap_or_default() == w) { println!("  saved {:?}", m.class.properties().get("m_SavedProperties")); }
                    if std::env::var("OK_MAP_EMISSION").is_ok() { let p = m.class.properties(); let kw = p.get("m_ShaderKeywords").and_then(|v| v.as_str()).unwrap_or("").to_string(); let saved = format!("{:?}", p.get("m_SavedProperties")); let at = saved.find("_EmissionColor").map(|i| saved[i..(i + 160).min(saved.len())].to_string()).unwrap_or_default(); let map = saved.find("_EmissionMap").map(|i| saved[i..(i + 120).min(saved.len())].to_string()).unwrap_or_default(); println!("  keywords [{kw}] {at} || {map}"); }
                } }
            }
            let names: std::collections::HashMap<i64, String> = ser.objects_of_type(1).into_iter().filter_map(|i| UnityObject::from_serialized_file(ser, &i).ok().and_then(|o| o.name()).map(|n| (i.path_id, n))).collect();
            // OK_MAP_BEHAVIOUR=<GameObject name>: the MonoBehaviours on it, with their fields.
            if std::env::var("OK_MAP_RENDER").is_ok() { for t in [104, 157] { for info in ser.objects_of_type(t) { if let Ok(o) = UnityObject::from_serialized_file(ser, &info) { println!("class {t} {:?}", o.class.properties()); } } } }
            if let Ok(want) = std::env::var("OK_MAP_OBJECT") { let id: i64 = want.parse().unwrap_or(0); for t in [28, 84, 20] { for info in ser.objects_of_type(t) { if info.path_id == id { if let Ok(o) = UnityObject::from_serialized_file(ser, &info) { let s = format!("{:?}", o.class.properties()); println!("object {id} class {t} {}", &s[..s.len().min(600)]); } } } } }
            if std::env::var("OK_MAP_CAMERAS").is_ok() { for info in ser.objects_of_type(20) { if let Ok(o) = UnityObject::from_serialized_file(ser, &info) { let p = o.class.properties(); let go = p.get("m_GameObject").and_then(|g| g.as_object()).and_then(|g| g.get("m_PathID")).and_then(|v| v.as_i64()).unwrap_or(0); println!("camera go={} enabled={:?} target={:?} depth={:?} cull={:?}", names.get(&go).cloned().unwrap_or_default(), p.get("m_Enabled"), p.get("m_TargetTexture"), p.get("m_Depth"), p.get("m_CullingMask")); } } }
            if let Ok(want) = std::env::var("OK_MAP_BEHAVIOUR") {
                for info in ser.objects_of_type(114) {
                    let Ok(o) = UnityObject::from_serialized_file(ser, &info) else { continue };
                    let p = o.class.properties();
                    let go = p.get("m_GameObject").and_then(|g| g.as_object()).and_then(|g| g.get("m_PathID")).and_then(|v| v.as_i64()).unwrap_or(0);
                    if names.get(&go).is_some_and(|n| n == &want) { println!("behaviour {:?}", p); }
                }
            }
            for t in [23, 137] { for info in ser.objects_of_type(t) {
                let Ok(o) = UnityObject::from_serialized_file(ser, &info) else { continue };
                let p = o.class.properties();
                let mode = p.get("m_CastShadows").map(|v| format!("{v:?}")).unwrap_or_default();
                let enabled = p.get("m_Enabled").map(|v| format!("{v:?}")).unwrap_or_default();
                *modes.entry(format!("{mode} enabled={enabled}")).or_default() += 1;
                // OK_MAP_LIST=1: every renderer with its materials and their shaders.
                if std::env::var("OK_MAP_LIST").is_ok() {
                    let pid = |v: &unity_asset_core::UnityValue, k: &str| v.as_object().and_then(|g| g.get(k)).and_then(|v| v.as_i64()).unwrap_or(0);
                    let go = p.get("m_GameObject").map(|g| pid(g, "m_PathID")).unwrap_or(0);
                    let mats: Vec<String> = match p.get("m_Materials") {
                        Some(unity_asset_core::UnityValue::Array(a)) => a.iter().map(|m| {
                            let (file, id) = (pid(m, "m_FileID"), pid(m, "m_PathID"));
                            (file == 0).then(|| ser.objects_of_type(21).into_iter().find(|i| i.path_id == id)).flatten()
                                .and_then(|i| UnityObject::from_serialized_file(ser, &i).ok())
                                .map(|mo| {
                                    let sid = mo.class.properties().get("m_Shader").map(|s| pid(s, "m_PathID")).unwrap_or(0);
                                    let shader = ser.objects_of_type(48).into_iter().find(|i| i.path_id == sid)
                                        .and_then(|i| UnityObject::from_serialized_file(ser, &i).ok())
                                        .map(|so| crate::unity::shader_runtime_name(&so)).unwrap_or_else(|| format!("shader?{sid}"));
                                    format!("{}:{shader}", mo.name().unwrap_or_default())
                                })
                                .unwrap_or_else(|| format!("ext{file}:{id}"))
                        }).collect(),
                        _ => vec![],
                    };
                    println!("{} cast={mode} enabled={enabled} {mats:?}", names.get(&go).cloned().unwrap_or_default());
                }
                if mode.contains('3') {
                    let go = p.get("m_GameObject").and_then(|g| g.as_object()).and_then(|g| g.get("m_PathID")).and_then(|v| v.as_i64()).unwrap_or(0);
                    println!("shadows-only: {}", names.get(&go).cloned().unwrap_or_default());
                }
            } }
        });
        println!("{modes:?}");
    }
}

#[cfg(test)]
mod emission_probe {
    /// Parts of a library map (OK_EMIT_MODEL / OK_EMIT_PREFAB, default the live stage) whose material emits (_EMISSION),
    /// and how many of them carry their _EmissionMap.
    #[test]
    #[ignore = "OK base library: emissive parts of a map"]
    fn map_emissive_parts() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let model = std::env::var("OK_EMIT_MODEL").unwrap_or_else(|_| "6eb3180200000000fb5732f44f4b4331".into());
        let prefab = std::env::var("OK_EMIT_PREFAB").unwrap_or_else(|_| "LiveStage".into());
        let id = crate::oklib::id_from_hex_pub(&model).unwrap();
        let session = crate::okmodel_view::with_library(&lib, || crate::okmodel_view::ModelView::open(&lib, id)?.prefab_session(&prefab, false)).unwrap();
        let (mut emits, mut maps) = (0, 0);
        for p in &session.parts {
            let Some(m) = p.source_material.as_ref() else { continue };
            if m.keywords.iter().any(|k| k == "_EMISSION") {
                emits += 1;
                if p.shader_textures.contains_key("_EmissionMap") { maps += 1; }
                if emits <= std::env::var("OK_EMIT_LIST").ok().and_then(|v| v.parse().ok()).unwrap_or(5) { println!("{} mat={} shader={} color={:?} flags={:?} map={}", p.name, m.name, m.shader, m.color("_EmissionColor"), m.float("_OKLightmapFlags"), p.shader_textures.contains_key("_EmissionMap")); }
            }
        }
        let lit = session.parts.iter().filter(|p| p.source_material.as_ref().is_some_and(|m| m.floats.contains_key("_OKLightmapIndex"))).count();
        let carriers = session.parts.iter().filter(|p| p.shader_textures.contains_key("_OKLightmap")).count();
        let reflected = session.parts.iter().filter(|p| p.source_material.as_ref().is_some_and(|m| m.floats.contains_key("_OKReflectionIndex"))).count();
        let reflection_carriers = session.parts.iter().filter(|p| p.shader_textures.contains_key("_OKReflection")).count();
        println!("reflected={reflected} reflection carriers={reflection_carriers}");
        let mut per = std::collections::BTreeMap::<i64, usize>::new();
        for p in &session.parts { if let Some(i) = p.source_material.as_ref().and_then(|m| m.floats.get("_OKReflectionIndex")) { *per.entry(*i as i64).or_default() += 1; } }
        println!("parts per reflection probe {per:?}");
        println!("parts={} emissive={emits} with map={maps} lightmapped={lit} lightmap carriers={carriers}", session.parts.len());
        let mut shaders = std::collections::BTreeMap::<String, usize>::new();
        for p in &session.parts { if let Some(m) = p.source_material.as_ref() { *shaders.entry(format!("{} {} lm={}", m.shader, m.name, m.floats.contains_key("_OKLightmapIndex"))).or_default() += 1; } }
        println!("shaders {shaders:?}");
        if let Ok(want) = std::env::var("OK_EMIT_MAT") {
            for p in session.parts.iter().filter(|p| p.source_material.as_ref().is_some_and(|m| m.name == want)).take(1) {
                let m = p.source_material.as_ref().unwrap();
                println!("{} colors={:?} floats={:?} keywords={:?} slots={:?}", m.name, m.colors, m.floats, m.keywords, m.tex_slots);
                for (k, png) in &p.shader_textures { let Ok(img) = crate::teximg::load_from_memory(png) else { continue }; let img = img.to_rgba8(); let n = (img.width() * img.height()) as f64; let mut s = [0f64; 4]; for px in img.pixels() { for c in 0..4 { s[c] += px[c] as f64; } } println!("  {k} {}x{} mean {:.0} {:.0} {:.0} {:.0}", img.width(), img.height(), s[0] / n, s[1] / n, s[2] / n, s[3] / n); }
            }
        }
    }
}

#[cfg(test)]
mod eyeset_probe {
    /// The eye sets (cha_eyeset, category 2) of the OK base library's lists.
    #[test]
    #[ignore = "OK base library: eye set list"]
    fn eyeset_entries() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        crate::okmodel_view::with_library(&lib, || {
            let cat = crate::list::load_characustom_dir_shared(&lib.join("abdata/list/characustom"));
            let mut n = 0;
            for e in cat.entries().filter(|e| e.category_no == 2) { n += 1; if n <= 40 { println!("{:?}", e); } }
            println!("eyesets={n}");
            Ok::<_, String>(())
        }).unwrap();
    }
}

#[cfg(test)]
mod render_texture_probe {
    /// Render-to-texture cameras of a map read from the installed game (OK_RT_BUNDLE / OK_RT_PREFAB, default the live
    /// stage): the stage's `Camera` draws `mainmoniter`, sampled by the screens' `moniter` materials.
    #[test]
    #[ignore = "installed game: render-to-texture cameras of a map"]
    fn map_render_texture_cameras() {
        let path = std::env::var("OK_RT_BUNDLE").unwrap_or_else(|_| crate::test_support::game_path("/abdata/studio/map/livestage_s.unity3d").to_string_lossy().into_owned());
        let prefab = std::env::var("OK_RT_PREFAB").unwrap_or_else(|_| "Map".into());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(&path).unwrap());
        let rt = parsed.prefab_render_textures(&prefab);
        let h = parsed.prefab_hierarchy(&prefab);
        for c in &rt.cameras { println!("camera {} {:?}", h.nodes.get(c.node as usize).map(|n| n.name.as_str()).unwrap_or("?"), c); }
        for u in &rt.users { println!("user {} {:?}", h.nodes.get(u.0 as usize).map(|n| n.name.as_str()).unwrap_or("?"), u); }
        // Each user's and camera's node chain up to the root (local position, rotation, scale).
        for start in rt.users.iter().map(|u| u.0).chain(rt.cameras.iter().map(|c| c.node)) {
            let mut i = start;
            while let Some(n) = h.nodes.get(i as usize) { println!("  chain {} pos={:?} rot={:?} scale={:?}", n.name, n.pos, n.rot, n.scale); i = n.parent; }
        }
        // OK_RT_ANIMATORS=1: Animators / Animations of the bundle and the GameObjects they sit on.
        if std::env::var("OK_RT_ANIMATORS").is_ok() {
            use unity_asset_binary::object::UnityObject;
            parsed.with_serialized(|sers| for ser in sers {
                let names: std::collections::HashMap<i64, String> = ser.objects_of_type(1).into_iter().filter_map(|i| UnityObject::from_serialized_file(ser, i).ok().and_then(|o| o.name()).map(|n| (i.path_id, n))).collect();
                for class in [95, 111, 74] { for info in ser.objects_of_type(class) {
                    let Ok(o) = UnityObject::from_serialized_file(ser, info) else { continue };
                    let p = o.class.properties();
                    let go = p.get("m_GameObject").and_then(|g| g.as_object()).and_then(|g| g.get("m_PathID")).and_then(|v| v.as_i64()).unwrap_or(0);
                    println!("class {class} {} on {}", o.name().unwrap_or_default(), names.get(&go).cloned().unwrap_or_default());
                } }
            });
        }
        // OK_RT_ALL=1: every RenderTexture of the bundle, and every material / MonoBehaviour referring to one.
        if std::env::var("OK_RT_ALL").is_ok() {
            use unity_asset_binary::object::UnityObject;
            parsed.with_serialized(|sers| {
                let rts: Vec<(usize, i64, String)> = sers.iter().enumerate().flat_map(|(f, s)| s.objects_of_type(84).into_iter().filter_map(move |i| UnityObject::from_serialized_file(s, i).ok().map(|o| (f, i.path_id, o.name().unwrap_or_default())))).collect();
                for r in &rts { println!("rendertexture file={} id={} {}", r.0, r.1, r.2); }
                for (f, s) in sers.iter().enumerate() {
                    println!("file {f} externals {:?}", s.externals.iter().map(|e| e.path.clone()).collect::<Vec<_>>());
                    for t in [21, 114, 20] { for i in s.objects_of_type(t) {
                        let Ok(o) = UnityObject::from_serialized_file(s, i) else { continue };
                        let text = format!("{:?}", o.class.properties());
                        for r in &rts { if text.contains(&format!("m_PathID\": Integer({})", r.1)) || text.contains(&format!("\"m_PathID\": Integer({})}}", r.1)) || text.contains(&format!("Integer({})", r.1)) { println!("  file {f} class {t} id {} {} mentions {} ({})", i.path_id, o.name().unwrap_or_default(), r.2, r.1); } }
                    } }
                }
            });
        }
        assert!(!rt.cameras.is_empty() && !rt.users.is_empty());
    }

    /// The same map from the OK base library (after `studio-nodes` recorded `prefab_render_textures`): the screen parts
    /// name their camera.
    #[test]
    #[ignore = "OK base library: render-to-texture parts of a map"]
    fn library_render_texture_parts() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let model = std::env::var("OK_RT_MODEL").unwrap_or_else(|_| "6eb3180200000000fb5732f44f4b4331".into());
        let prefab = std::env::var("OK_RT_PREFAB").unwrap_or_else(|_| "Map".into());
        let id = crate::oklib::id_from_hex_pub(&model).unwrap();
        let session = crate::okmodel_view::with_library(&lib, || crate::okmodel_view::ModelView::open(&lib, id)?.prefab_session(&prefab, false)).unwrap();
        let mut n = 0;
        for p in &session.parts {
            let Some(m) = p.source_material.as_ref() else { continue };
            let Some(c) = m.floats.get("_OKRenderTexture") else { continue };
            n += 1;
            println!("{} material={} shader={} camera={c} pos={:?} fwd={:?} size={:?}", p.name, m.name, m.shader, m.color("_OKRTCamPos"), m.color("_OKRTCamFwd"), m.color("_OKRTCamSize"));
        }
        println!("parts={} render-texture parts={n}", session.parts.len());
        // Stage effects (unity::stage_effect_kind): kind, textures kept and the uv2 the colour scrolls on.
        let mut kinds = std::collections::BTreeMap::<i32, (usize, std::collections::BTreeSet<String>, usize)>::new();
        for p in &session.parts {
            let Some(k) = p.source_material.as_ref().and_then(|m| m.floats.get("_OKStageEffect")) else { continue };
            let e = kinds.entry(*k as i32).or_default();
            e.0 += 1;
            e.1.extend(p.shader_textures.keys().cloned());
            if p.uv2.len() == p.positions.len() { e.2 += 1; }
        }
        for (k, (n, t, uv2)) in &kinds { println!("stage effect {k}: {n} parts, textures {t:?}, with uv2 {uv2}"); }
        // OK_RT_TEXTURES=<material>,<dir>: that material's values, and its kept textures written as PNG.
        if let Some((want, dir)) = std::env::var("OK_RT_TEXTURES").ok().and_then(|v| v.split_once(',').map(|(a, b)| (a.to_owned(), b.to_owned()))) {
            if let Some(p) = session.parts.iter().find(|p| p.source_material.as_ref().is_some_and(|m| m.name == want)) {
                let m = p.source_material.as_ref().unwrap();
                println!("{} floats {:?} colors {:?}", p.name, m.floats.iter().collect::<std::collections::BTreeMap<_, _>>(), m.colors);
                for (k, png) in &p.shader_textures { std::fs::write(std::path::Path::new(&dir).join(format!("{want}{k}.png")), png).unwrap(); }
                if let Some(png) = &p.png { std::fs::write(std::path::Path::new(&dir).join(format!("{want}_MainTex.png")), png).unwrap(); }
            }
        }
        // OK_RT_MATERIAL=<prefix>: parts whose material name starts with it, with their bounds.
        if let Ok(want) = std::env::var("OK_RT_MATERIAL") {
            for p in &session.parts {
                let Some(m) = p.source_material.as_ref().filter(|m| m.name.starts_with(&want)) else { continue };
                let (lo, hi) = crate::pose::aabb(&p.positions);
                let (ulo, uhi) = p.uvs.iter().fold(([f32::MAX; 2], [f32::MIN; 2]), |(a, b), u| ([a[0].min(u[0]), a[1].min(u[1])], [b[0].max(u[0]), b[1].max(u[1])]));
                println!("material {} {} shader={} tex={:?} bounds={lo:?}..{hi:?} uv={ulo:?}..{uhi:?} tris={}", p.name, m.name, m.shader, m.tex_slots, p.indices.len() / 3);
            }
        }
        // OK_RT_NEAR=x,y,z: parts whose bounds come within 4 m of that point (what a camera there sees first).
        if let Ok(at) = std::env::var("OK_RT_NEAR") {
            let c: Vec<f32> = at.split(',').filter_map(|v| v.parse().ok()).collect();
            for p in &session.parts {
                let (lo, hi) = crate::pose::aabb(&p.positions);
                let d: f32 = (0..3).map(|a| { let v = (lo[a] - c[a]).max(c[a] - hi[a]).max(0.); v * v }).sum::<f32>().sqrt();
                if d < 4. && std::env::var("OK_RT_RAY").is_err() { println!("near {} d={d:.2} material={:?} blend={} bounds={lo:?}..{hi:?}", p.name, p.source_material.as_ref().map(|m| (&m.name, &m.shader)), p.blend); }
            }
            // OK_RT_RAY=dx,dy,dz: triangles the ray from OK_RT_NEAR hits first (what the camera's centre sees).
            if let Ok(dir) = std::env::var("OK_RT_RAY") {
                let d: Vec<f32> = dir.split(',').filter_map(|v| v.parse().ok()).collect();
                let sub = |a: [f32; 3], b: [f32; 3]| [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
                let cross = |a: [f32; 3], b: [f32; 3]| [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
                let dot = |a: [f32; 3], b: [f32; 3]| a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
                let (o, d) = ([c[0], c[1], c[2]], [d[0], d[1], d[2]]);
                let mut hits: Vec<(f32, String, String)> = Vec::new();
                for p in &session.parts {
                    let mut best = f32::MAX;
                    for t in p.indices.chunks_exact(3) {
                        let [a, b, e] = [t[0], t[1], t[2]].map(|i| p.positions[i as usize]);
                        let (e1, e2) = (sub(b, a), sub(e, a));
                        let h = cross(d, e2);
                        let det = dot(e1, h);
                        if det.abs() < 1e-9 { continue; }
                        let s = sub(o, a);
                        let u = dot(s, h) / det;
                        if !(0. ..=1.).contains(&u) { continue; }
                        let q = cross(s, e1);
                        let v = dot(d, q) / det;
                        if v < 0. || u + v > 1. { continue; }
                        let dist = dot(e2, q) / det;
                        if dist > 0.01 && dist < best { best = dist; }
                    }
                    if best < f32::MAX { hits.push((best, p.name.clone(), format!("{:?} {}", p.source_material.as_ref().map(|m| (&m.name, &m.shader)), p.blend))); }
                }
                hits.sort_by(|a, b| a.0.total_cmp(&b.0));
                for h in hits.iter().take(12) { println!("ray {:.2} {} {}", h.0, h.1, h.2); }
            }
        }
        assert!(n > 0);
    }
}

#[cfg(test)]
mod head_reference_probe {
    /// Renderers below a head's eye reference nodes (n_eyebase, n_hitomi) and the gag eye objects (OK_HEAD_BUNDLE).
    #[test]
    #[ignore = "installed game: head eye reference nodes"]
    fn eye_reference_children() {
        use unity_asset_binary::object::UnityObject;
        let path = std::env::var("OK_HEAD_BUNDLE").unwrap_or_else(|_| crate::test_support::game_path("/abdata/chara/bo_head_00.unity3d").to_string_lossy().into_owned());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(&path).unwrap());
        parsed.with_serialized(|sers| for ser in sers {
            let mut names = std::collections::HashMap::new();
            let mut parent = std::collections::HashMap::new();
            let mut tf_go = std::collections::HashMap::new();
            for i in ser.objects_of_type(1) { if let Ok(o) = UnityObject::from_serialized_file(ser, &i) { names.insert(i.path_id, o.name().unwrap_or_default()); } }
            for i in ser.objects_of_type(4) { if let Ok(o) = UnityObject::from_serialized_file(ser, &i) { let p = o.class.properties();
                let go = p.get("m_GameObject").and_then(|g| g.as_object()).and_then(|g| g.get("m_PathID")).and_then(|v| v.as_i64()).unwrap_or(0);
                let fa = p.get("m_Father").and_then(|g| g.as_object()).and_then(|g| g.get("m_PathID")).and_then(|v| v.as_i64()).unwrap_or(0);
                tf_go.insert(i.path_id, go); parent.insert(go, fa); } }
            let go_of_tf = |tf: i64| tf_go.get(&tf).copied().unwrap_or(0);
            for t in [137, 23] { for i in ser.objects_of_type(t) { if let Ok(o) = UnityObject::from_serialized_file(ser, &i) {
                let go = o.class.properties().get("m_GameObject").and_then(|g| g.as_object()).and_then(|g| g.get("m_PathID")).and_then(|v| v.as_i64()).unwrap_or(0);
                let mut chain = vec![names.get(&go).cloned().unwrap_or_default()];
                let mut g = go;
                for _ in 0..12 { let Some(&tf) = parent.get(&g) else { break }; if tf == 0 { break } g = go_of_tf(tf); chain.push(names.get(&g).cloned().unwrap_or_default()); }
                let joined = chain.join(" < ");
                if joined.contains("n_eyebase") || joined.contains("n_hitomi") || joined.contains("gag") || joined.to_ascii_lowercase().contains("eye") { println!("{joined}"); }
            } } }
        });
    }
}

#[cfg(test)]
mod gag_texture_probe {
    /// The gag eye textures (cha_eyeset MainTex) and pupil expressions (EpsTex) decode from the OK base library.
    #[test]
    #[ignore = "OK base library: gag eye textures"]
    fn gag_textures_from_library() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        crate::okmodel_view::with_library(&lib, || {
            for name in ["cf_t_gageye_00", "cf_t_gageye_01", "cf_t_gageye_02", "cf_t_gageye_09", "cf_t_expression_00"] {
                let png = crate::png_from_bundle(&lib, "chara/mt_eye_00.unity3d", None, &[name], &[], &[]);
                if let (Some(p), Ok(dir)) = (png.as_ref(), std::env::var("OK_GAG_OUT")) { std::fs::write(std::path::Path::new(&dir).join(format!("{name}.png")), p).unwrap(); }
                println!("{name}: {:?}", png.map(|p| p.len()));
            }
            Ok::<_, String>(())
        }).unwrap();
    }
}

#[cfg(test)]
mod scene_camera_probe {
    /// The main Studio camera of a scene (OK_SCENE, default the live stage sample 1901): world position and view
    /// direction in map space (for `render_texture_probe`'s OK_RT_NEAR / OK_RT_RAY), and the characters' positions.
    #[test]
    #[ignore = "OK base library: a sample scene's Studio camera"]
    fn scene_camera_ray() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let path = std::env::var("OK_SCENE").map(std::path::PathBuf::from).unwrap_or_else(|_| lib.join("studio/samples/koikatu_cs0001901.png"));
        let scene = crate::studio_scene::read(&std::fs::read(&path).unwrap()).unwrap();
        let cam = scene.camera.clone().unwrap();
        let q = crate::pose::euler_deg_to_quat(cam.rotate);
        let mut v = [cam.distance, [0., 0., 1.]];
        crate::pose::apply_mat(&mut v, &crate::pose::trs([0.; 3], q, [1.; 3]));
        let at = [v[0][0] + cam.pos[0], v[0][1] + cam.pos[1], v[0][2] + cam.pos[2]];
        println!("map={} camera at={},{},{} dir={},{},{} fov={}", scene.map, at[0], at[1], at[2], v[1][0], v[1][1], v[1][2], cam.fov);
        if let Some(e) = &scene.effects { println!("map transform pos={:?} rot={:?} sun={}", e.map_pos, e.map_rot, e.sun_light_type); }
        let json = serde_json::to_value(&scene.objects).unwrap();
        fn walk(v: &serde_json::Value) { match v {
            serde_json::Value::Object(m) => { if m.get("kind").and_then(|k| k.as_str()) == Some("Character") { println!("character change={:?} anime={:?}", m.get("change"), m.get("anime")); } for (_, x) in m { walk(x); } }
            serde_json::Value::Array(a) => for x in a { walk(x); },
            _ => {}
        } }
        walk(&json);
    }
}

#[cfg(test)]
mod lut_probe {
    /// What a Studio filter LUT (OK_LUT, default 昼 Lut_Amount) maps some colours to (sRGB 0..255), for both row
    /// orders of the 1024x32 Amplify Color layout.
    #[test]
    #[ignore = "OK base library: filter LUT mapping"]
    fn filter_lut_mapping() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let name = std::env::var("OK_LUT").unwrap_or_else(|_| "Lut_Amount".into());
        let png = crate::okmodel_view::with_library(&lib, || crate::studio_items::texture_png(&lib, "abdata/studio/filter/00.unity3d", &name).ok_or_else(|| "no lut".to_string())).unwrap();
        let img = crate::teximg::load_from_memory(&png).unwrap().to_rgba8();
        println!("lut {}x{}", img.width(), img.height());
        for c in [[149u8, 156, 146], [139, 180, 222], [231, 219, 61], [128, 128, 128], [255, 255, 255], [0, 0, 0]] {
            let q = |v: u8| ((v as f32 / 255.) * 31.).round() as u32;
            let (r, g, b) = (q(c[0]), q(c[1]), q(c[2]));
            let top = img.get_pixel(r + 32 * b, g).0;
            let bottom = img.get_pixel(r + 32 * b, 31 - g).0;
            println!("{c:?} -> g from top {:?}  g from bottom {:?}", &top[..3], &bottom[..3]);
        }
    }
}

#[cfg(test)]
mod lightmap_probe {
    #[test]
    #[ignore = "installed source map lighting input diagnostics"]
    fn dark_scene_lighting_inputs() {
        let lib=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let out=lib.join("../../../Saved/OKStudio/LightingLut20261021/MapInputs");std::fs::create_dir_all(&out).unwrap();
        for (label,model) in [("gymnasium","f69bcc0100000000c9ccf38a4f4b4331"),("lockerroom","c6fe700000000000a318018f4f4b4331")] {
            crate::okmodel_view::with_library(&lib,||{
                let view=crate::okmodel_view::ModelView::open(&lib,crate::oklib::id_from_hex_pub(model).unwrap())?;
                let store=crate::oklib::active().unwrap();
                let missing_before=store.misses.load(std::sync::atomic::Ordering::Relaxed);
                let parsed=crate::unity::parse_bundle_cached(crate::oklib::token(&crate::oklib::id_from_hex_pub(model).unwrap()));
                assert!(parsed.texture_sampling_named("__absent_source_sampler__").is_none());
                assert_eq!(store.misses.load(std::sync::atomic::Ordering::Relaxed),missing_before,"proven-absent sampler must not fabricate a dependency query");
                let session=view.prefab_session("Map",false)?;
                assert!(session.parts.iter().any(|p|p.shader_textures.contains_key("_BumpMap")),"{label}: source normals absent");
                for p in &session.parts {
                    if let Some(m)=p.source_material.as_ref().filter(|m|m.shader.starts_with("Standard")) {
                        for slot in ["_MainTex","_BumpMap"] {
                            if m.slot(slot).is_some() {
                                let sampler=p.runtime_textures.get(slot).unwrap_or_else(||panic!("{label} {} {slot}: source sampler absent",p.name));
                                assert_eq!(sampler[4],0,"static map textures must not generate encoded RenderTexture mips");
                            }
                        }
                    }
                }
                println!("{label}: Standard bump textures {}",session.parts.iter().filter(|p|p.shader_textures.contains_key("_BumpMap")).count());
                let mut rows=Vec::new();
                for (i,p) in session.parts.iter().enumerate(){
                    let m=p.source_material.as_ref();
                    if let Some(png)=&p.png {std::fs::write(out.join(format!("{label}-{i}-main.png")),png).unwrap();}
                    for slot in ["_OKLightmap","_OKLightmapDirection"] {if let Some(png)=p.shader_textures.get(slot){let index=m.and_then(|m|m.floats.get("_OKLightmapIndex")).unwrap();std::fs::write(out.join(format!("{label}-{index}-{slot}.png")),png).unwrap();}}
                    rows.push(serde_json::json!({"part":i,"name":p.name,"uv0":p.uvs,"uv4":p.uv4,"normals":p.normals,"material":m,"runtime_textures":p.runtime_textures}));
                }
                std::fs::write(out.join(format!("{label}.json")),serde_json::to_vec(&rows).unwrap()).unwrap();
                Ok::<_,String>(())
            }).unwrap();
        }
    }
    #[test]
    #[ignore = "installed map and OK library: renderer shadow policy provenance"]
    fn studio_renderer_shadow_policy_matches_source() {
        let game = crate::test_support::game_path("");
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let bytes = std::fs::read(game.join("abdata/studio/map/bushitu.unity3d")).unwrap();
        let id = crate::oklib::content_id(&bytes);
        let parsed = crate::unity::parse_bundle_cached(bytes);
        parsed.configure_game_references(&game);
        let expected = parsed.prefab_renderer_settings("Map");
        assert!(expected.iter().any(|r| r.1 == 3), "fixture has a ShadowsOnly occluder");
        assert!(expected.iter().any(|r| r.1 == 0), "fixture also has a non-casting wall");
        crate::okmodel_view::with_library(&lib, || {
            let view = crate::okmodel_view::ModelView::open(&lib, id)?;
            assert_eq!(view.prefab_renderer_settings("Map"), expected);
            let session = view.prefab_session("Map", false)?;
            let mode = |name: &str| session.parts.iter().find(|p| p.name == name)
                .and_then(|p| p.source_material.as_ref()).and_then(|m| m.floats.get("_OKRendererShadowMode")).copied();
            assert_eq!(mode("mo_koi_stu_map49_shadow00"), Some(3.));
            assert_eq!(mode("mo_koi_stu_map49_wall00_00"), Some(0.));
            println!("Renderer shadow policy source/library exact: {} renderers, ShadowsOnly preserved", expected.len());
            Ok::<_, String>(())
        }).unwrap();
    }

    #[test]
    #[ignore = "installed map and OK library: directional lightmap provenance"]
    fn studio_directional_lightmap_matches_source() {
        let game = crate::test_support::game_path("");
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let bytes = std::fs::read(game.join("abdata/studio/map/1ftoilet.unity3d")).unwrap();
        let id = crate::oklib::content_id(&bytes);
        let parsed = crate::unity::parse_bundle_cached(bytes);
        parsed.configure_game_references(&game);
        let expected = parsed.prefab_lightmap_directions("Map");
        assert_eq!(expected.textures.len(), 1);
        assert!(!expected.textures[0].is_empty());
        crate::okmodel_view::with_library(&lib, || {
            let view = crate::okmodel_view::ModelView::open(&lib, id)?;
            let session = view.prefab_session("Map", false)?;
            let carried: Vec<_> = session.parts.iter().filter_map(|p| p.shader_textures.get("_OKLightmapDirection")).collect();
            assert_eq!(carried.len(), 1, "one shared direction atlas, not one copy per renderer");
            let normalized = crate::flip_png_v(&expected.textures[0]).unwrap();
            assert!(*carried[0] == normalized, "library direction atlas differs from installed source after rendering row normalization");
            println!("Directional lightmap source/library exact: {} bytes", carried[0].len());
            Ok::<_, String>(())
        }).unwrap();
    }

    /// Alpha and colour statistics of a library map's lightmaps (OK_LM_MODEL / OK_LM_PREFAB, default the 1F toilet):
    /// RGBM keeps a varying alpha (colour = 5·a·rgb), dLDR a constant one (colour = 2·rgb).
    #[test]
    #[ignore = "OK base library: lightmap encoding"]
    fn lightmap_alpha() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let model = std::env::var("OK_LM_MODEL").unwrap_or_else(|_| "24f1460000000000ee5f45764f4b4331".into());
        let prefab = std::env::var("OK_LM_PREFAB").unwrap_or_else(|_| "Map".into());
        let id = crate::oklib::id_from_hex_pub(&model).unwrap();
        let (lm, directions) = crate::okmodel_view::with_library(&lib, || {
            let view = crate::okmodel_view::ModelView::open(&lib, id)?;
            Ok::<_, String>((view.prefab_lightmaps(&prefab), view.prefab_lightmap_directions(&prefab)))
        }).unwrap();
        for (i, png) in lm.textures.iter().enumerate() {
            let Ok(img) = crate::teximg::load_from_memory(png) else { continue };
            let img = img.to_rgba8();
            let (mut amin, mut amax, mut asum, mut rgbmax) = (255u8, 0u8, 0u64, 0u8);
            for p in img.pixels() { amin = amin.min(p[3]); amax = amax.max(p[3]); asum += p[3] as u64; rgbmax = rgbmax.max(p[0].max(p[1]).max(p[2])); }
            println!("lightmap {i}: {}x{} alpha min {amin} max {amax} mean {} rgb max {rgbmax}", img.width(), img.height(), asum / (img.width() as u64 * img.height() as u64));
            // OK_LM_NATIVE=<probe output prefix> (run_scene_visual_audit --lightmaps): the GPU-sampled native lightmap
            // <prefix>.lightmap<i>.color.rgba32f (sRGB colour decoded, RGBM alpha as stored); mean linear RGBM decode
            // (34.49·a^2.2·rgb) of both, and of the stored colour and alpha alone.
            if let Ok(prefix) = std::env::var("OK_LM_NATIVE") {
                if let Ok(raw) = std::fs::read(format!("{prefix}.lightmap{i}.color.rgba32f")) {
                    let f: Vec<f32> = raw.chunks_exact(4).map(|b| f32::from_le_bytes([b[0], b[1], b[2], b[3]])).collect();
                    let lin = |c: f32| if c <= 0.04045 { c / 12.92 } else { ((c + 0.055) / 1.055).powf(2.4) };
                    let (mut ours, mut native, mut oa, mut na, mut orgb, mut nrgb) = ([0f64; 3], [0f64; 3], 0f64, 0f64, 0f64, 0f64);
                    for p in img.pixels() {
                        let a = p[3] as f32 / 255.;
                        for c in 0..3 { ours[c] += (34.493242 * a.powf(2.2) * lin(p[c] as f32 / 255.)) as f64; orgb += lin(p[c] as f32 / 255.) as f64 / 3.; }
                        oa += a as f64;
                    }
                    for p in f.chunks_exact(4) {
                        for c in 0..3 { native[c] += (34.493242 * p[3].powf(2.2) * p[c]) as f64; nrgb += p[c] as f64 / 3.; }
                        na += p[3] as f64;
                    }
                    // Which way round the colour atlas is against the native rows (as the direction atlas below).
                    let (w, h) = (img.width() as usize, img.height() as usize);
                    let (mut same, mut flip) = (0f64, 0f64);
                    if f.len() == w * h * 4 { for y in 0..h { for x in 0..w {
                        let p = img.get_pixel(x as u32, y as u32);
                        let a = p[3] as f64 / 255.;
                        same += (a - f[(y * w + x) * 4 + 3] as f64).abs(); flip += (a - f[((h - 1 - y) * w + x) * 4 + 3] as f64).abs();
                    } } }
                    println!("  colour alpha mean |diff| as stored {:.4}, rows flipped {:.4}", same / (w * h) as f64, flip / (w * h) as f64);
                    let (n, m) = ((img.width() * img.height()) as f64, (f.len() / 4) as f64);
                    println!("  decoded mean ours {:?} native {:?}; alpha {:.4} vs {:.4}; linear rgb {:.4} vs {:.4} ({} vs {} texels)",
                        ours.map(|v| (v / n * 1000.).round() / 1000.), native.map(|v| (v / m * 1000.).round() / 1000.), oa / n, na / m, orgb / n, nrgb / m, n, m);
                }
            }
            // And the direction texture (linear): mean rgb / alpha, and the mean texel difference as stored and with
            // the rows flipped (which way round the two are).
            if let (Ok(prefix), Some(dir_png)) = (std::env::var("OK_LM_NATIVE"), directions.textures.get(i)) {
                if let (Ok(raw), Ok(ours)) = (std::fs::read(format!("{prefix}.lightmap{i}.direction.rgba32f")), crate::teximg::load_from_memory(dir_png)) {
                    let ours = ours.to_rgba8();
                    let f: Vec<f32> = raw.chunks_exact(4).map(|b| f32::from_le_bytes([b[0], b[1], b[2], b[3]])).collect();
                    let (w, h) = (ours.width() as usize, ours.height() as usize);
                    let (mut om, mut nm, mut same, mut flip) = ([0f64; 4], [0f64; 4], 0f64, 0f64);
                    for y in 0..h { for x in 0..w {
                        let p = ours.get_pixel(x as u32, y as u32);
                        let n = &f[(y * w + x) * 4..][..4];
                        let nf = &f[((h - 1 - y) * w + x) * 4..][..4];
                        for c in 0..4 {
                            let o = p[c] as f64 / 255.;
                            om[c] += o; nm[c] += n[c] as f64;
                            same += (o - n[c] as f64).abs(); flip += (o - nf[c] as f64).abs();
                        }
                    } }
                    let n = (w * h) as f64;
                    println!("  direction mean ours {:?} native {:?}; mean |diff| as stored {:.4}, rows flipped {:.4}",
                        om.map(|v| (v / n * 1000.).round() / 1000.), nm.map(|v| (v / n * 1000.).round() / 1000.), same / n / 4., flip / n / 4.);
                }
            }
            // OK_LM_PIXELS=<uv picture>,<normal picture>,<OK white picture>,<native white picture> (CompareKStudio pictures
            // with OK_LIGHTMAP_DIAG=uv / normal / whitenoenv, and the native probe's white-noreflect): at sample points of
            // this lightmap, the lightmap term computed from the textures (34.49·a^2.2·rgb·halfLambert/w, no albedo
            // or reflectivity) beside the two pictures, all linear.
            if let (Ok(list), Some(dir_png)) = (std::env::var("OK_LM_PIXELS"), directions.textures.get(i)) {
                let paths: Vec<&str> = list.split(',').collect();
                let half = |p: &str| { let m = crate::teximg::load_from_memory(&std::fs::read(p).unwrap()).unwrap().to_rgb8(); let w = m.width() / 2; image::imageops::crop_imm(&m, w, 0, w, m.height()).to_image() };
                let (uv, normal, ok) = (half(paths[0]), half(paths[1]), half(paths[2]));
                let native = image::imageops::resize(&crate::teximg::load_from_memory(&std::fs::read(paths[3]).unwrap()).unwrap().to_rgb8(), uv.width(), uv.height(), image::imageops::FilterType::Triangle);
                let dir_img = crate::teximg::load_from_memory(dir_png).unwrap().to_rgba8();
                let lin = |c: f32| if c <= 0.04045 { c / 12.92 } else { ((c + 0.055) / 1.055).powf(2.4) };
                let (w, h) = (img.width() as f32, img.height() as f32);
                // Every pixel of this lightmap: quantiles of OK / native (linear green), and the mean by native brightness.
                let mut ratios = Vec::new();
                let mut bands = [(0f64, 0usize); 5];
                for y in 0..uv.height() { for x in 0..uv.width() {
                    let c = uv.get_pixel(x, y).0.map(|v| lin(v as f32 / 255.));
                    if (c[2] - 0.25 * (i as f32 + 1.)).abs() > 0.03 { continue; }
                    let (o, nt) = (lin(ok.get_pixel(x, y).0[1] as f32 / 255.), lin(native.get_pixel(x, y).0[1] as f32 / 255.));
                    if nt < 0.01 { continue; }
                    ratios.push(o / nt);
                    let b = ((nt * 5.) as usize).min(4);
                    bands[b].0 += (o / nt) as f64; bands[b].1 += 1;
                } }
                ratios.sort_by(|a, b| a.partial_cmp(b).unwrap());
                let q = |f: f32| ratios.get(((ratios.len() as f32 - 1.) * f) as usize).copied().unwrap_or(0.);
                println!("  OK/native over {} pixels: 10% {:.3} 25% {:.3} median {:.3} 75% {:.3} 90% {:.3}; by native linear fifths {:?}",
                    ratios.len(), q(0.1), q(0.25), q(0.5), q(0.75), q(0.9), bands.map(|(s, n)| if n > 0 { ((s / n as f64) * 1000.).round() / 1000. } else { 0. }));
                for sy in 1..9 { for sx in 1..9 {
                    let (x, y) = (uv.width() * sx / 9, uv.height() * sy / 9);
                    let c = uv.get_pixel(x, y).0.map(|v| lin(v as f32 / 255.));
                    if (c[2] - 0.25 * (i as f32 + 1.)).abs() > 0.03 { continue; }
                    let v = if std::env::var("OK_LM_FLIP").is_ok() { 1. - c[1] } else { c[1] };
                    let (tx, ty) = (((c[0] * w) as u32).min(img.width() - 1), ((v * h) as u32).min(img.height() - 1));
                    let (p, d) = (img.get_pixel(tx, ty).0, dir_img.get_pixel(tx, ty).0);
                    let n = normal.get_pixel(x, y).0.map(|v| lin(v as f32 / 255.) * 2. - 1.);
                    let half_lambert = (0..3).map(|k| n[k] * (d[k] as f32 / 255. - 0.5)).sum::<f32>() + 0.5;
                    let term = (0..3).map(|k| 34.493242 * (p[3] as f32 / 255.).powf(2.2) * lin(p[k] as f32 / 255.) * half_lambert / (d[3] as f32 / 255.).max(1e-4)).collect::<Vec<_>>();
                    let (o, nt) = (ok.get_pixel(x, y).0.map(|v| lin(v as f32 / 255.)), native.get_pixel(x, y).0.map(|v| lin(v as f32 / 255.)));
                    println!("  ({x},{y}) texel ({tx},{ty}) normal {:?} term {:.3} ok {:.3} native {:.3}  ok/term {:.3} native/term {:.3}",
                        n.map(|v| (v * 100.).round() / 100.), term[1], o[1], nt[1], o[1] / term[1], nt[1] / term[1]);
                } }
            }
            if let Ok(dir) = std::env::var("OK_LM_OUT") { let mut d = img.clone(); for p in d.pixels_mut() { let a = p[3] as f32 / 255. * 5.; for c in 0..3 { p[c] = ((p[c] as f32 / 255. * a).min(1.) * 255.) as u8; } p[3] = 255; } d.save(std::path::Path::new(&dir).join(format!("lightmap_{i}.png"))).unwrap(); }
        }
    }
}

#[cfg(test)]
mod raw_lightmap_probe {
    /// A game map bundle's renderers' lightmap fields and its MonoBehaviours' fields (OK_RAW_BUNDLE, default the
    /// 1F toilet of an installed game under OK_GAME_ROOT): where Studio keeps a map's lightmap atlas placement.
    #[test]
    #[ignore = "installed game: raw map bundle lightmap fields"]
    fn raw_map_lightmap_fields() {
        let root = std::path::PathBuf::from(std::env::var("OK_GAME_ROOT").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned()));
        let bundle = std::env::var("OK_RAW_BUNDLE").unwrap_or_else(|_| "abdata/studio/map/1ftoilet.unity3d".into());
        let bytes = std::fs::read(root.join(&bundle)).unwrap();
        let parsed = crate::unity::parse_bundle_cached(bytes);
        parsed.with_serialized(|sers| {
            for (si, ser) in sers.iter().enumerate() {
                let (mut shown, mut types) = (0, std::collections::BTreeMap::<i32, usize>::new());
                let mut usage = std::collections::BTreeMap::<String, usize>::new();
                for t in [1, 4, 21, 23, 28, 104, 114, 115, 157, 108] { types.insert(t, ser.objects_of_type(t).len()); }
                println!("file {si}: types {types:?}");
                for info in ser.objects_of_type(23) {
                    let Ok(obj) = unity_asset_decode::object::UnityObject::from_serialized_file(ser, info) else { continue };
                    let p = obj.class.properties();
                    *usage.entry(format!("{:?}", p.get("m_ReflectionProbeUsage"))).or_insert(0) += 1;
                    shown += 1;
                }
                if !usage.is_empty() { println!("  reflection probe usage {usage:?}"); }
                for t in [104, 215, 89] { for info in ser.objects_of_type(t).into_iter().take(3) { let Ok(obj) = unity_asset_decode::object::UnityObject::from_serialized_file(ser, info) else { continue }; let p = obj.class.properties(); let mut keys: Vec<String> = p.iter().map(|(k, v)| { let s = format!("{v:?}"); format!("{k}={}", &s[..s.len().min(160)]) }).collect(); keys.sort(); println!("  class {t}: {}", keys.join(" | ")); } }
                for info in ser.objects_of_type(114).into_iter().take(40) {
                    let Ok(obj) = unity_asset_decode::object::UnityObject::from_serialized_file(ser, info) else { continue };
                    let p = obj.class.properties();
                    let keys: Vec<&String> = p.keys().collect();
                    let s = format!("{keys:?}");
                    println!("  mono {:?}: {}", obj.name(), &s[..s.len().min(400)]);
                    if let Some(v) = p.get("lightmapParameters") { let s = format!("{v:?}"); println!("  lightmapParameters ({} chars): {}", s.len(), &s[..s.len().min(2500)]); }
                }
            }
        });
    }
}


#[cfg(test)]
mod map_animator_probe {
    /// Every Animator of a map bundle (OK_ANIM_BUNDLE, default the live stage): its GameObject, the controller's
    /// default clip, its duration, and the Transform / activation curves at t=0 and mid-clip (paths by CRC under the
    /// Animator's GameObject).
    #[test]
    #[ignore = "installed game: a map's Animator default states"]
    fn map_animator_defaults() {
        use unity_asset_binary::object::UnityObject;
        let path = std::env::var("OK_ANIM_BUNDLE").unwrap_or_else(|_| crate::test_support::game_path("/abdata/studio/map/livestage_s.unity3d").to_string_lossy().into_owned());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(&path).unwrap());
        parsed.with_serialized(|sers| {
            let find = |class: i32, id: i64| sers.iter().find_map(|s| s.objects_of_type(class).into_iter().find(|i| i.path_id == id).and_then(|i| UnityObject::from_serialized_file(s, i).ok()));
            let pid = |v: &unity_asset_core::UnityValue| v.as_object().and_then(|o| o.get("m_PathID")).and_then(|v| v.as_i64()).unwrap_or(0);
            for ser in sers {
                // GameObject names and each Transform's children, to name CRC paths under an Animator.
                let names: std::collections::HashMap<i64, String> = ser.objects_of_type(1).into_iter().filter_map(|i| UnityObject::from_serialized_file(ser, i).ok().and_then(|o| o.name()).map(|n| (i.path_id, n))).collect();
                let mut tf_go = std::collections::HashMap::new(); let mut go_tf = std::collections::HashMap::new(); let mut kids: std::collections::HashMap<i64, Vec<i64>> = Default::default();
                for i in ser.objects_of_type(4) { if let Ok(o) = UnityObject::from_serialized_file(ser, i) { let p = o.class.properties();
                    let go = p.get("m_GameObject").map(pid).unwrap_or(0); tf_go.insert(i.path_id, go); go_tf.insert(go, i.path_id);
                    if let Some(unity_asset_core::UnityValue::Array(c)) = p.get("m_Children") { kids.insert(i.path_id, c.iter().map(pid).collect()); } } }
                // OK_ANIM_LAYERS=1: renderers (MeshRenderer, SkinnedMeshRenderer) per GameObject layer.
                if std::env::var("OK_ANIM_LAYERS").is_ok() {
                    let layers: std::collections::HashMap<i64, i64> = ser.objects_of_type(1).into_iter().filter_map(|i| UnityObject::from_serialized_file(ser, i).ok().map(|o| (i.path_id, o.class.properties().get("m_Layer").and_then(|v| v.as_i64()).unwrap_or(-1)))).collect();
                    let mut hist = std::collections::BTreeMap::<i64, usize>::new();
                    for class in [23, 137] { for i in ser.objects_of_type(class) { if let Ok(o) = UnityObject::from_serialized_file(ser, i) {
                        let go = o.class.properties().get("m_GameObject").map(pid).unwrap_or(0);
                        *hist.entry(layers.get(&go).copied().unwrap_or(-1)).or_default() += 1; } } }
                    if !hist.is_empty() { println!("renderer layers {hist:?}"); }
                }
                // OK_ANIM_GO=<name>: that GameObject's Transform chain (local pos / rot / scale, active) and its MeshFilter mesh.
                if let Ok(want) = std::env::var("OK_ANIM_GO") {
                    for (go, name) in names.iter().filter(|(_, n)| **n == want) {
                        let mut tf = go_tf.get(go).copied().unwrap_or(0);
                        while tf != 0 {
                            let Some(o) = ser.objects_of_type(4).into_iter().find(|i| i.path_id == tf).and_then(|i| UnityObject::from_serialized_file(ser, i).ok()) else { break };
                            let p = o.class.properties();
                            let g = tf_go.get(&tf).copied().unwrap_or(0);
                            let active = ser.objects_of_type(1).into_iter().find(|i| i.path_id == g).and_then(|i| UnityObject::from_serialized_file(ser, i).ok()).map(|o| format!("{:?}", o.class.properties().get("m_IsActive"))).unwrap_or_default();
                            println!("go {name}: chain {} pos={:?} rot={:?} scale={:?} active={active}", names.get(&g).cloned().unwrap_or_default(), p.get("m_LocalPosition"), p.get("m_LocalRotation"), p.get("m_LocalScale"));
                            tf = p.get("m_Father").map(pid).unwrap_or(0);
                        }
                        for i in ser.objects_of_type(33) { if let Ok(o) = UnityObject::from_serialized_file(ser, i) { let p = o.class.properties();
                            if p.get("m_GameObject").map(pid) == Some(*go) { println!("go {name}: mesh {:?}", p.get("m_Mesh")); } } }
                    }
                }
                for info in ser.objects_of_type(95) {
                    let Ok(a) = UnityObject::from_serialized_file(ser, info) else { continue };
                    let p = a.class.properties();
                    let go = p.get("m_GameObject").map(pid).unwrap_or(0);
                    let Some(ctrl) = p.get("m_Controller").map(pid).and_then(|id| find(91, id)) else { println!("animator {} no controller", names.get(&go).cloned().unwrap_or_default()); continue };
                    let mut paths = std::collections::HashMap::new();
                    let mut stack = vec![(go_tf.get(&go).copied().unwrap_or(0), String::new())];
                    while let Some((tf, prefix)) = stack.pop() { paths.insert(crate::anim::unity_string_to_hash(&prefix) as u32, prefix.clone());
                        for k in kids.get(&tf).into_iter().flatten() { let n = names.get(tf_go.get(k).unwrap_or(&0)).cloned().unwrap_or_default(); stack.push((*k, if prefix.is_empty() { n } else { format!("{prefix}/{n}") })); } }
                    let defaults = crate::anim::controller_default_clips(&ctrl);
                    if defaults.is_empty() {
                        let cp = ctrl.class.properties();
                        let all: Vec<String> = match cp.get("m_AnimationClips") { Some(unity_asset_core::UnityValue::Array(a)) => a.iter().filter_map(|c| find(74, pid(c)).and_then(|o| o.name())).collect(), _ => vec![] };
                        let sm = format!("{:?}", cp.get("m_Controller").and_then(|c| c.as_object()).and_then(|c| c.get("m_StateMachineArray")));
                        println!("animator on {} controller {} no default clip; clips {all:?}; state machines {}", names.get(&go).cloned().unwrap_or_default(), ctrl.name().unwrap_or_default(), &sm[..sm.len().min(600)]);
                    }
                    for clip in defaults {
                        let Some(c) = find(74, pid(&clip)) else { continue };
                        let (v0, dur) = crate::anim::generic_clip_values(&c, 0.0);
                        let (vm, _) = crate::anim::generic_clip_values(&c, dur * 0.5);
                        println!("animator on {} controller {} default clip {} duration {dur:.2} bindings {}", names.get(&go).cloned().unwrap_or_default(), ctrl.name().unwrap_or_default(), c.name().unwrap_or_default(), v0.len());
                        // OK_ANIM_CLIP=<clip>: every binding of that clip at ten even times.
                        if std::env::var("OK_ANIM_CLIP").is_ok_and(|w| c.name().is_some_and(|n| n == w)) {
                            for k in 0..10 {
                                let t = dur * k as f32 / 10.;
                                let (v, _) = crate::anim::generic_clip_values(&c, t);
                                println!("   t={t:.1} {}", v.iter().map(|(p, a, ty, x)| format!("{}:{a:#x}/{ty}={x:.2?}", paths.get(p).cloned().unwrap_or_else(|| format!("#{p:08x}")))).collect::<Vec<_>>().join(" "));
                            }
                        }
                        for ((path, attr, ty, a), (_, _, _, b)) in v0.iter().zip(&vm) {
                            if !(*ty == 4 && (1..=4).contains(attr)) && *ty != 1 && std::env::var_os("OK_ANIM_ALL").is_none() { continue; }
                            let moved = a.iter().zip(b).any(|(x, y)| (x - y).abs() > 1e-4);
                            println!("   {} attr={attr} type={ty} t0={a:?}{}", paths.get(path).cloned().unwrap_or_else(|| format!("#{path:08x}")), if moved { format!(" mid={b:?}") } else { String::new() });
                        }
                    }
                }
            }
        });
    }
}

#[cfg(test)]
mod item_open_timing_probe {
    #[test]
    #[ignore = "installed game and OK library: MDK renderer inputs must match"]
    fn studio_mdk_library_matches_source_inputs() {
        let game = crate::test_support::game_path("");
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let prefab = "p_koi_stu_senpu01_00";
        let bytes = std::fs::read(game.join("abdata/studio/00.unity3d")).unwrap();
        let id = crate::oklib::content_id(&bytes);
        let parsed = crate::unity::parse_bundle_cached(bytes);
        parsed.configure_game_references(&game);
        let meshes = parsed.meshes_for_maindata(prefab).0;
        let raw = crate::preview_session_parts(&game, "part", meshes, None).unwrap();
        assert_eq!(raw.len(), 2);
        crate::okmodel_view::with_library(&lib, || {
            let view = crate::okmodel_view::ModelView::open(&lib, id)?;
            let session = view.prefab_session_fk(prefab, false, &[])?;
            assert_eq!(session.parts.len(), raw.len());
            for expected in &raw {
                let material = &expected.source_material.as_ref().unwrap().name;
                let actual = session.parts.iter().find(|p| p.source_material.as_ref().is_some_and(|m| &m.name == material)).unwrap();
                assert_eq!(actual.png, expected.png, "{material} MainTex");
                assert_eq!(actual.shader_textures, expected.shader_textures, "{material} native slots");
                assert_eq!(actual.runtime_textures, expected.runtime_textures, "{material} sampler provenance");
                println!("MDK source/library exact: {material}, {} native slots", actual.shader_textures.len());
            }
            Ok::<_, String>(())
        }).unwrap();
    }

    /// How long opening the same Studio item (OK_ITEM_MODEL / OK_ITEM_PREFAB, default the plain cube) as a preview
    /// session takes, first and repeated (a scene of a thousand copies opens it a thousand times).
    #[test]
    #[ignore = "OK base library: item session open timing"]
    fn item_open_timing() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let model = std::env::var("OK_ITEM_MODEL").unwrap_or_else(|_| "1496300600000000d8e3d4984f4b4331".into());
        let prefab = std::env::var("OK_ITEM_PREFAB").unwrap_or_else(|_| "p_koi_stu_cube01_02".into());
        let id = crate::oklib::id_from_hex_pub(&model).unwrap();
        crate::okmodel_view::with_library(&lib, || Ok::<_, String>(())).unwrap();
        for round in 0..5 {
            let t = std::time::Instant::now();
            let view = crate::okmodel_view::with_library(&lib, || crate::okmodel_view::ModelView::open(&lib, id)).unwrap();
            let t_open = t.elapsed();
            let s = view.prefab_session_fk(&prefab, false, &[]).unwrap();
            println!("round {round}: view {:.1} ms, session {:.1} ms, parts {}", t_open.as_secs_f64() * 1e3, t.elapsed().as_secs_f64() * 1e3, s.parts.len());
            if round == 0 {
                if let Ok(directory) = std::env::var("OK_ITEM_GEOMETRY_OUT") {
                    let directory = std::path::Path::new(&directory);
                    std::fs::create_dir_all(directory).unwrap();
                    for (index, part) in s.parts.iter().enumerate() {
                        let data = serde_json::json!({"name":part.name,"positions":part.positions,"uvs":part.uvs,"indices":part.indices,"material":part.source_material});
                        std::fs::write(directory.join(format!("part{index}.json")), serde_json::to_vec(&data).unwrap()).unwrap();
                        if let Some(png) = &part.png { std::fs::write(directory.join(format!("part{index}.png")), png).unwrap(); }
                    }
                }
            }
            // OK_ITEM_MATERIALS=1: each part's source material (shader, keywords, queue) as the UE recipe choice sees it.
            if round == 0 && std::env::var_os("OK_ITEM_MATERIALS").is_some() {
                for p in &s.parts {
                    println!("  {} blend={} material={:?}", p.name, p.blend, p.source_material);
                    println!("    delivered textures: {:?}", p.shader_textures.keys().collect::<Vec<_>>());
                    if let Some(mat) = p.source_material.as_ref().filter(|m| m.shader == "Shader Forge/main_StandardMDK_studio") {
                        for property in ["_BumpMap", "_ParallaxMap", "_MetallicGlossMap", "_EmissionMap"] {
                            if mat.slot(property).is_some() {
                                assert!(p.shader_textures.contains_key(property), "{} lost authored {property}", mat.name);
                            }
                        }
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod reflection_probe_probe {
    /// `prefab_reflection_probes` of a game map bundle (OK_RAW_BUNDLE / OK_RAW_PREFAB): the probes and their atlases.
    #[test]
    #[ignore = "installed game: map reflection probe atlases"]
    fn map_reflection_probe_atlas() {
        let root = std::path::PathBuf::from(std::env::var("OK_GAME_ROOT").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned()));
        let bundle = std::env::var("OK_RAW_BUNDLE").unwrap_or_else(|_| "abdata/studio/map/1ftoilet.unity3d".into());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join(&bundle)).unwrap());
        let probes = parsed.prefab_reflection_probes(&std::env::var("OK_RAW_PREFAB").unwrap_or_else(|_| "Map".into()));
        for (i, p) in probes.probes.iter().enumerate() {
            println!("probe {i}: node {} box {:?} offset {:?} importance {} intensity {} size {} mips {} png {} bytes", p.node, p.box_size, p.box_offset, p.importance, p.intensity, p.size, p.mips, p.atlas.len());
            if let Ok(dir) = std::env::var("OK_RP_OUT") { std::fs::write(std::path::Path::new(&dir).join(format!("probe_atlas_{i}.png")), &p.atlas).unwrap(); }
        }
        assert!(!probes.probes.is_empty());
    }

    /// A game map bundle's baked reflection probe cubemaps (class 89, BC6H): each face's mean colour (8-bit, clamped)
    /// and face PNGs under OK_RP_OUT. OK_GAME_ROOT / OK_RAW_BUNDLE as `raw_map_lightmap_fields`.
    #[test]
    #[ignore = "installed game: map reflection probe colours"]
    fn map_reflection_probes() {
        let root = std::path::PathBuf::from(std::env::var("OK_GAME_ROOT").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned()));
        let bundle = std::env::var("OK_RAW_BUNDLE").unwrap_or_else(|_| "abdata/studio/map/1ftoilet.unity3d".into());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join(&bundle)).unwrap());
        parsed.with_serialized(|sers| {
            for ser in sers {
                for info in ser.objects_of_type(89) {
                    let Ok(obj) = unity_asset_decode::object::UnityObject::from_serialized_file(ser, info) else { continue };
                    let p = obj.class.properties();
                    let int = |k: &str| match p.get(k) { Some(unity_asset_core::UnityValue::Integer(i)) => *i as usize, _ => 0 };
                    let (w, h, face_bytes) = (int("m_Width"), int("m_Height"), int("m_CompleteImageSize"));
                    let Some(unity_asset_core::UnityValue::Bytes(data)) = p.get("image data") else { println!("{:?}: no inline data", obj.name()); continue };
                    for face in 0..6 {
                        let Some(src) = data.get(face * face_bytes..face * face_bytes + w * h) else { break };
                        let mut px = vec![0u32; w * h];
                        if texture2ddecoder::decode_bc6(src, w, h, &mut px, false).is_err() { println!("face {face}: decode failed"); continue; }
                        let mut s = [0f64; 3];
                        for v in &px { s[0] += ((v >> 16) & 255) as f64; s[1] += ((v >> 8) & 255) as f64; s[2] += (v & 255) as f64; }
                        let n = px.len() as f64;
                        println!("{:?} face {face}: mean rgb {:.0} {:.0} {:.0}", obj.name(), s[0] / n, s[1] / n, s[2] / n);
                        if let Ok(dir) = std::env::var("OK_RP_OUT") {
                            let img = image::RgbaImage::from_fn(w as u32, h as u32, |x, y| { let v = px[y as usize * w + x as usize]; image::Rgba([(v >> 16) as u8, (v >> 8) as u8, v as u8, 255]) });
                            img.save(std::path::Path::new(&dir).join(format!("{}_{face}.png", obj.name().unwrap_or_default()))).unwrap();
                        }
                    }
                }
            }
        });
    }
}

#[cfg(test)]
mod scene_roots_probe {
    /// The root GameObjects of a game map scene bundle (OK_RAW_BUNDLE): Studio loads the whole scene
    /// (Map.LoadMap -> Scene.LoadBaseScene), not only its "Map" root, so every root here is drawn.
    #[test]
    #[ignore = "installed game: map scene roots"]
    fn map_scene_roots() {
        use unity_asset_decode::object::UnityObject;
        let root = std::path::PathBuf::from(std::env::var("OK_GAME_ROOT").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned()));
        let bundle = std::env::var("OK_RAW_BUNDLE").unwrap_or_else(|_| "abdata/studio/map/livestage_s.unity3d".into());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join(&bundle)).unwrap());
        parsed.with_serialized(|sers| {
            for ser in sers {
                let mut names = std::collections::HashMap::new();
                let mut active = std::collections::HashMap::new();
                for info in ser.objects_of_type(1) { if let Ok(o) = UnityObject::from_serialized_file(ser, info) {
                    names.insert(info.path_id, o.name().unwrap_or_default());
                    active.insert(info.path_id, format!("{:?}", o.class.properties().get("m_IsActive")));
                } }
                for class in [4, 224] { for info in ser.objects_of_type(class) {
                    let Ok(o) = UnityObject::from_serialized_file(ser, info) else { continue };
                    let p = o.class.properties();
                    let father = p.get("m_Father").map(|v| format!("{v:?}")).unwrap_or_default();
                    if !father.contains("Integer(0)") || father.matches("Integer(0)").count() < 2 { continue; }
                    let go = match p.get("m_GameObject") { Some(unity_asset_core::UnityValue::Object(m)) => match m.get("m_PathID") { Some(unity_asset_core::UnityValue::Integer(i)) => *i, _ => 0 }, _ => 0 };
                    let children = match p.get("m_Children") { Some(unity_asset_core::UnityValue::Array(a)) => a.len(), _ => 0 };
                    println!("root {:?} active {} children {} pos {:?}", names.get(&go), active.get(&go).cloned().unwrap_or_default(), children, p.get("m_LocalPosition"));
                } }
            }
        });
    }
}

#[cfg(test)]
mod sun_light_info_probe {
    /// A game map scene's SunLightInfo (OK_RAW_BUNDLE): per time of day its 2D background texture, the objects it
    /// shows, the sun light and ambient colours; and the BackGroundParam canvas state.
    #[test]
    #[ignore = "installed game: map SunLightInfo"]
    fn map_sun_light_info() {
        use unity_asset_decode::object::UnityObject;
        let root = std::path::PathBuf::from(std::env::var("OK_GAME_ROOT").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned()));
        let bundle = std::env::var("OK_RAW_BUNDLE").unwrap_or_else(|_| "abdata/studio/map/livestage_s.unity3d".into());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join(&bundle)).unwrap());
        parsed.with_serialized(|sers| {
            let name_of = |id: i64| sers.iter().find_map(|s| s.objects_of_type(28).into_iter().find(|i| i.path_id == id).and_then(|i| UnityObject::from_serialized_file(s, i).ok()).and_then(|o| o.name()));
            for ser in sers {
                for info in ser.objects_of_type(114) {
                    let Ok(obj) = UnityObject::from_serialized_file(ser, info) else { continue };
                    let p = obj.class.properties();
                    if let Some(infos) = p.get("_infos") {
                        println!("SunLightInfo targetLight={:?} bgParam={:?}", p.get("_targetLight"), p.get("_bgParam"));
                        // The target Light component: type, mode (m_Lightmapping 4 realtime, 1 mixed, 2 baked), culling, enabled.
                        let light_id = match p.get("_targetLight") { Some(unity_asset_core::UnityValue::Object(o)) => match o.get("m_PathID") { Some(unity_asset_core::UnityValue::Integer(x)) => *x, _ => 0 }, _ => 0 };
                        if let Some(l) = sers.iter().find_map(|s| s.objects_of_type(108).into_iter().find(|i| i.path_id == light_id).and_then(|i| UnityObject::from_serialized_file(s, i).ok())) {
                            let lp = l.class.properties();
                            println!("  light type={:?} lightmapping={:?} bakingOutput={:?} cull={:?} enabled={:?} shadows={:?}", lp.get("m_Type"), lp.get("m_Lightmapping"), lp.get("m_BakingOutput"), lp.get("m_CullingMask"), lp.get("m_Enabled"), lp.get("m_Shadows").and_then(|s| match s { unity_asset_core::UnityValue::Object(o) => o.get("m_Type").cloned(), _ => None }));
                        }
                        let unity_asset_core::UnityValue::Array(list) = infos else { continue };
                        for i in list {
                            let unity_asset_core::UnityValue::Object(m) = i else { continue };
                            let pid = |k: &str| match m.get(k) { Some(unity_asset_core::UnityValue::Object(o)) => match o.get("m_PathID") { Some(unity_asset_core::UnityValue::Integer(x)) => *x, _ => 0 }, _ => 0 };
                            let vis = match m.get("visibleList") { Some(unity_asset_core::UnityValue::Array(a)) => a.len(), _ => 0 };
                            if std::env::var("OK_SUN_VISIBLE").is_ok() { println!("  visibleList {:?}", m.get("visibleList")); }
                            println!("  type {:?} back {:?} visible {} angle {:?} color {:?} intensity {:?} sky {:?}", m.get("type"), name_of(pid("backTexture")), vis, m.get("angle"), m.get("color"), m.get("intensity"), m.get("ambientSkyColor"));
                        }
                    }
                    if p.contains_key("_canvas") && p.contains_key("_image") { println!("BackGroundParam canvas={:?} camera={:?} image={:?}", p.get("_canvas"), p.get("_camera"), p.get("_image")); }
                }
                for info in ser.objects_of_type(223) { if let Ok(o) = UnityObject::from_serialized_file(ser, info) { let p = o.class.properties(); println!("Canvas enabled {:?} mode {:?} camera {:?}", p.get("m_Enabled"), p.get("m_RenderMode"), p.get("m_Camera")); } }
            }
        });
    }
}

#[cfg(test)]
mod material_binding_hash_probe {
    #[test]
    fn material_binding_hash() {
        for n in ["_EmissionColor", "_Color", "_EmissionPower"] {
            let h = crate::anim::unity_string_to_hash(n) as u32;
            println!("{n} {h:#010x} low28 {:#09x}", h & 0x0FFF_FFFF);
        }
    }
}

#[cfg(test)]
mod animated_materials_probe {
    /// `prefab_animated_materials` of a game map bundle (OK_RAW_BUNDLE / OK_RAW_PREFAB, default the live stage).
    #[test]
    #[ignore = "installed game: map Animator material values"]
    fn map_animated_materials() {
        let root = std::path::PathBuf::from(std::env::var("OK_GAME_ROOT").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned()));
        let bundle = std::env::var("OK_RAW_BUNDLE").unwrap_or_else(|_| "abdata/studio/map/livestage_s.unity3d".into());
        let prefab = std::env::var("OK_RAW_PREFAB").unwrap_or_else(|_| "Map".into());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join(&bundle)).unwrap());
        let h = parsed.prefab_hierarchy(&prefab);
        let a = parsed.prefab_animated_materials(&prefab);
        let mut nodes = std::collections::BTreeMap::<String, usize>::new();
        for (node, prop, comp, v) in &a.values { *nodes.entry(format!("{} prop {prop:#09x} comp {comp} = {v}", h.nodes.get(*node as usize).map(|n| n.name.clone()).unwrap_or_default())).or_default() += 1; }
        for (k, n) in nodes.iter().take(40) { println!("{n} x {k}"); }
        println!("values {}", a.values.len());
    }
}

#[cfg(test)]
mod controller_states_probe {
    /// The states of a map bundle's AnimatorController (OK_RAW_BUNDLE, OK_CTRL name, default ElectricLight): each
    /// state's clip ids and transitions.
    #[test]
    #[ignore = "installed game: AnimatorController states"]
    fn controller_states() {
        use unity_asset_decode::object::UnityObject;
        let root = std::path::PathBuf::from(std::env::var("OK_GAME_ROOT").unwrap_or_else(|_| crate::test_support::game_path("").to_string_lossy().into_owned()));
        let bundle = std::env::var("OK_RAW_BUNDLE").unwrap_or_else(|_| "abdata/studio/map/livestage_s.unity3d".into());
        let want = std::env::var("OK_CTRL").unwrap_or_else(|_| "ElectricLight".into());
        let parsed = crate::unity::parse_bundle_cached(std::fs::read(root.join(&bundle)).unwrap());
        parsed.with_serialized(|sers| for ser in sers { for info in ser.objects_of_type(91) {
            let Ok(o) = UnityObject::from_serialized_file(ser, info) else { continue };
            if o.name().as_deref() != Some(want.as_str()) { continue; }
            let p = o.class.properties();
            let s = format!("{:?}", p.get("m_Controller"));
            // Print the state-related keys compactly.
            for key in ["m_DefaultState", "m_StateConstantArray", "m_AnyStateTransitionConstantArray", "m_ClipID", "m_DestinationState", "m_HasExitTime", "m_ExitTime", "m_BlendTreeConstantArray", "m_NodeArray"] {
                let mut i = 0; let mut n = 0;
                while let Some(k) = s[i..].find(key) { n += 1; i += k + key.len(); if n <= 6 { println!("{key} @{}: {}", i, &s[i..(i + 90).min(s.len())]); } }
            }
            println!("clips {:?}", p.get("m_AnimationClips").map(|c| format!("{c:?}").chars().take(300).collect::<String>()));
            break;
        } });
    }
}

#[cfg(test)]
mod prefab_nodes_probe {
    /// A library model prefab's hierarchy (OK_ITEM_MODEL / OK_ITEM_PREFAB): every node's local position, rotation,
    /// scale and parent, and the mesh nodes.
    #[test]
    #[ignore = "OK base library: prefab nodes"]
    fn prefab_nodes() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let model = std::env::var("OK_ITEM_MODEL").unwrap_or_else(|_| "1496300600000000d8e3d4984f4b4331".into());
        let prefab = std::env::var("OK_ITEM_PREFAB").unwrap_or_else(|_| "p_koi_stu_pentagon01_00_02".into());
        let id = crate::oklib::id_from_hex_pub(&model).unwrap();
        let view = crate::okmodel_view::with_library(&lib, || crate::okmodel_view::ModelView::open(&lib, id)).unwrap();
        let h = view.hierarchy(&prefab);
        for (i, n) in h.nodes.iter().enumerate() { println!("node {i} {} parent {} pos {:?} rot {:?} scale {:?}", n.name, n.parent, n.pos, n.rot, n.scale); }
        println!("mesh nodes {:?}", h.mesh_nodes);
        let s = view.prefab_session(&prefab, false).unwrap();
        for p in &s.parts { println!("part {} tris {}", p.name, p.indices.len() / 3); }
    }
}

#[cfg(test)]
mod scene_tree_probe {
    /// A scene's object tree (OK_SCENE): kind, name / item no, transform, nesting.
    #[test]
    #[ignore = "OK base library: a sample scene's object tree"]
    fn scene_object_tree() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let path = std::env::var("OK_SCENE").map(std::path::PathBuf::from).unwrap_or_else(|_| lib.join("studio/samples/koikatu_cs0006964.png"));
        let scene = crate::studio_scene::read(&std::fs::read(&path).unwrap()).unwrap();
        let json = serde_json::to_value(&scene.objects).unwrap();
        fn walk(v: &serde_json::Value, depth: usize) {
            let Some(a) = v.as_array() else { return };
            for o in a {
                let kind = o.get("kind").and_then(|k| k.as_str()).unwrap_or("?");
                let id = ["name", "no", "group", "category"].iter().filter_map(|k| o.get(*k).map(|v| format!("{k}={v}"))).collect::<Vec<_>>().join(" ");
                println!("{}{kind} {id} change={}", "  ".repeat(depth), o.get("change").map(|c| c.to_string()).unwrap_or_default());
                if kind == "Character" {
                    let f = |k: &str| o.get(k).map(|v| { let s = v.to_string(); s[..s.len().min(160)].to_string() }).unwrap_or_default();
                    println!("{}  anime={} speed={} time={} enable_fk={} active_fk={} enable_ik={} active_ik={} fk_count={} ik_count={}", "  ".repeat(depth),
                        f("anime"), f("anime_speed"), f("anime_time"), f("enable_fk"), f("active_fk"), f("enable_ik"), f("active_ik"),
                        o.get("fk").and_then(|v| v.as_array()).map_or(0, |a| a.len()), o.get("ik").and_then(|v| v.as_array()).map_or(0, |a| a.len()));
                }
                if let Some(c) = o.get("children") {
                    if c.is_array() { walk(c, depth + 1); }
                    else if let Some(m) = c.as_array().or(None) { walk(&serde_json::Value::Array(m.clone()), depth + 1); }
                    // Characters keep children per accessory slot: [[slot, [objects]], ...].
                    if let Some(slots) = c.as_array() { for s in slots { if let Some(inner) = s.as_array().and_then(|p| p.get(1)) { walk(inner, depth + 1); } } }
                }
            }
        }
        walk(&json, 0);
    }
}

#[cfg(test)]
mod map_sun_light_parts {
    /// A library map's SunLightInfo (OK_SUN_MODEL / OK_SUN_PREFAB, default the 2F corridor): its times of day and the
    /// parts each shows (`_OKSunVisible`), with the meshes the prefab has only inactive.
    #[test]
    #[ignore = "OK base library: a map's times of day"]
    fn library_map_sun_parts() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let model = std::env::var("OK_SUN_MODEL").unwrap_or_else(|_| "1624d20100000000a99759b74f4b4331".into());
        let prefab = std::env::var("OK_SUN_PREFAB").unwrap_or_else(|_| "Map".into());
        let id = crate::oklib::id_from_hex_pub(&model).unwrap();
        crate::okmodel_view::with_library(&lib, || {
            let view = crate::okmodel_view::ModelView::open(&lib, id)?;
            let sun = view.parsed_sun_light(&prefab);
            println!("sun light node={} type={} culling={:#x} lightmapping={}", sun.light_node, sun.light_type, sun.culling_mask, sun.lightmapping);
            for i in &sun.infos { println!("  time {} visible nodes {:?} angle {:?} color {:?} intensity {}", i.kind, i.visible, i.angle, i.color, i.intensity); }
            println!("meshes active {} with inactive {}", view.prefab_meshes(&prefab, false).len(), view.prefab_meshes(&prefab, true).len());
            let session = view.prefab_session(&prefab, false)?;
            let mut by_mask = std::collections::BTreeMap::<i32, Vec<String>>::new();
            for p in &session.parts { if let Some(m) = p.source_material.as_ref().and_then(|m| m.floats.get("_OKSunVisible")) { by_mask.entry(*m as i32).or_default().push(p.name.clone()); } }
            for (m, names) in &by_mask { println!("  mask {m:#b}: {} parts {:?}", names.len(), &names[..names.len().min(6)]); }
            println!("parts {}", session.parts.len());
            Ok::<(), String>(())
        }).unwrap();
    }
}

#[cfg(test)]
mod flare_material_probe {
    /// The Studio lens flare items' material (Legacy Shaders/Particles/Additive): its values and texture (OK_FLARE_OUT).
    #[test]
    #[ignore = "OK base library: lens flare material"]
    fn flare_material() {
        let lib = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1");
        let id = crate::oklib::id_from_hex_pub("1496300600000000d8e3d4984f4b4331").unwrap();
        crate::okmodel_view::with_library(&lib, || {
            let view = crate::okmodel_view::ModelView::open(&lib, id)?;
            for (prefab, mat) in [("p_koi_stu_flare00_02", "m_koi_stu_flare00_02"), ("p_koi_stu_flare01_02", "m_koi_stu_flare01_02")] {
                let m = view.material(mat);
                println!("{mat}: {:?}", m.as_ref().map(|m| (&m.shader, &m.colors, &m.tex_slots)));
                if let Ok(dir) = std::env::var("OK_FLARE_OUT") {
                    for mesh in view.prefab_meshes(prefab, false) { if let Some(png) = &mesh.texture_png { std::fs::write(std::path::Path::new(&dir).join(format!("{prefab}.png")), png).unwrap(); } }
                }
            }
            Ok::<(), String>(())
        }).unwrap();
    }
}
