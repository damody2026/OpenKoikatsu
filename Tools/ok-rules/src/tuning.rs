//! The tuning files (numbers designers change while the game runs). Each lives in the game data folder; when the
//! folder or a file is missing, the copy built into the DLL is used, so a staged build always has the same rules.
use crate::activities::ActivitiesTuning;
use crate::bonds::BondTuning;
use crate::catalog::Catalog;
use crate::events::EventsTuning;
use crate::heroine::ActsTuning;
use crate::needs::NeedsTuning;
use crate::places::PlacesTuning;
use crate::property::PropertyData;
use crate::region::{RegionData, Regions};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::Path;

const BONDS_BUILTIN: &str = include_str!("../../../Content/IndependentResources/StreetSocialData/rules/bonds.toml");
const NEEDS_BUILTIN: &str = include_str!("../../../Content/IndependentResources/StreetSocialData/rules/needs.toml");
const PLACES_BUILTIN: &str = include_str!("../../../Content/IndependentResources/StreetSocialData/rules/places.toml");
const ACTS_BUILTIN: &str = include_str!("../../../Content/IndependentResources/StreetSocialData/rules/acts.toml");
const EVENTS_BUILTIN: &str = include_str!("../../../Content/IndependentResources/StreetSocialData/rules/events.toml");
const ACTIVITIES_BUILTIN: &str = include_str!("../../../Content/IndependentResources/StreetSocialData/rules/activities.toml");
// The property market lives beside the rules folder (it is game data the scene also reads for its displays).
const CATALOG_BUILTIN: &str = include_str!("../../../Content/IndependentResources/StreetSocialData/catalog.json");
const PROPERTY_BUILTIN: &str = include_str!("../../../Content/IndependentResources/StreetSocialData/property.json");

pub struct Tuning {
    pub bonds: BondTuning,
    pub needs: NeedsTuning,
    pub places: PlacesTuning,
    pub acts: ActsTuning,
    /// The activities together (activities.toml), the data's own names.
    pub activities: ActivitiesTuning,
    /// Festivals and exam weeks (events.toml).
    pub events: EventsTuning,
    /// The base data in yuan (Regions/CN) and, per region, the same converted to its money (region.rs).
    pub property: PropertyData,
    pub catalog: Catalog,
    pub regions: Regions,
    /// Keyed "region|lang": amounts in the region's money, names in the language (names.toml).
    properties: HashMap<String, PropertyData>,
    catalogs: HashMap<String, Catalog>,
    places_by_lang: HashMap<String, PlacesTuning>,
    property_jsons: HashMap<String, Value>,
    acts_by_lang: HashMap<String, ActsTuning>,
    bonds_by_lang: HashMap<String, BondTuning>,
    activities_by_lang: HashMap<String, ActivitiesTuning>,
    /// The words of every language (locale.rs).
    pub texts: crate::locale::Texts,
    sources: Vec<(&'static str, String)>,
}

impl Tuning {
    pub fn load(dir: Option<&Path>) -> Result<Tuning, String> {
        let mut sources = Vec::new();
        let bonds: BondTuning = load_into(dir, "bonds", BONDS_BUILTIN, &mut sources)?;
        let needs = load_into(dir, "needs", NEEDS_BUILTIN, &mut sources)?;
        let places = load_into(dir, "places", PLACES_BUILTIN, &mut sources)?;
        let acts: ActsTuning = load_into(dir, "acts", ACTS_BUILTIN, &mut sources)?;
        let activities: ActivitiesTuning = load_into(dir, "activities", ACTIVITIES_BUILTIN, &mut sources)?;
        let events: EventsTuning = load_into(dir, "events", EVENTS_BUILTIN, &mut sources)?;
        let property_json = load_property(dir, &mut sources)?;
        let property = PropertyData::parse(&property_json);
        let catalog = Catalog::parse(&load_data(dir, "catalog.json", CATALOG_BUILTIN, &mut sources)?);
        let texts = crate::locale::Texts::load(dir)?;
        sources.push(("texts", texts.sources.join(";")));
        let regions = Regions::load(dir)?;
        sources.push(("regions", regions.sources.join(";")));
        let mut properties = HashMap::new();
        let mut catalogs = HashMap::new();
        let mut places_by_lang = HashMap::new();
        let mut property_jsons = HashMap::new();
        let mut acts_by_lang = HashMap::new();
        let mut bonds_by_lang = HashMap::new();
        let mut activities_by_lang = HashMap::new();
        for lang in crate::locale::LANGS {
            for (code, reg) in &regions.by_code {
                let json = name_property(localize_property(&property_json, reg), reg, &texts, lang);
                properties.insert(format!("{code}|{lang}"), PropertyData::parse(&json));
                property_jsons.insert(format!("{code}|{lang}"), json);
                catalogs.insert(format!("{code}|{lang}"), name_catalog(localize_catalog(&catalog, reg), &texts, lang));
            }
            places_by_lang.insert(lang.to_string(), name_places(&places, &texts, lang));
            activities_by_lang.insert(lang.to_string(), activities.named(&texts, lang));
            let mut a = acts.clone();
            for act in &mut a.act {
                for (field, value) in [("name", &mut act.name), ("wanted", &mut act.wanted), ("text", &mut act.text), ("rumor_text", &mut act.rumor_text), ("button", &mut act.button)] {
                    if let Some(n) = texts.own(lang, &format!("ui.name.act_{field}.{}", act.id)) {
                        *value = n.to_string();
                    }
                }
            }
            acts_by_lang.insert(lang.to_string(), a);
            let mut b = bonds.clone();
            if let Some(n) = texts.own(lang, "ui.name.bond.with_him_why") {
                b.together.with_him_why = n.to_string();
            }
            for (id, e) in b.events.iter_mut() {
                if let Some(n) = texts.own(lang, &format!("ui.name.bond_why.{id}")) {
                    e.why = n.to_string();
                }
            }
            bonds_by_lang.insert(lang.to_string(), b);
        }
        Ok(Tuning { bonds, needs, places, acts, activities, events, activities_by_lang, property, catalog, regions, properties, catalogs, places_by_lang, property_jsons, acts_by_lang, bonds_by_lang, texts, sources })
    }

    /// The catalog / property market in a region's money (the request's region: locale::region).
    pub fn catalog_in(&self, region: &str, lang: &str) -> &Catalog {
        self.catalogs.get(&format!("{region}|{lang}")).unwrap_or(&self.catalog)
    }

    pub fn property_in(&self, region: &str, lang: &str) -> &PropertyData {
        self.properties.get(&format!("{region}|{lang}")).unwrap_or(&self.property)
    }

    /// property.json as the scene reads it, in a region's money and a language (rule property_data).
    pub fn property_json_in(&self, region: &str, lang: &str) -> Value {
        self.property_jsons.get(&format!("{region}|{lang}")).cloned().unwrap_or(Value::Null)
    }

    /// The interactions (acts.toml) and the girls' bond events (bonds.toml) with their words in the language.
    pub fn acts_in(&self, lang: &str) -> &ActsTuning {
        self.acts_by_lang.get(lang).unwrap_or(&self.acts)
    }

    /// The activities with their names in the language.
    pub fn activities_in(&self, lang: &str) -> &ActivitiesTuning {
        self.activities_by_lang.get(lang).unwrap_or(&self.activities)
    }

    pub fn bonds_in(&self, lang: &str) -> &BondTuning {
        self.bonds_by_lang.get(lang).unwrap_or(&self.bonds)
    }

    /// The places' norms with their descriptions (told to the model) in the language.
    pub fn places_in(&self, lang: &str) -> &PlacesTuning {
        self.places_by_lang.get(lang).unwrap_or(&self.places)
    }

    pub fn builtin() -> Tuning {
        Tuning::load(None).expect("built-in tuning parses")
    }

    pub fn sources(&self) -> Value {
        Value::Object(self.sources.iter().map(|(k, v)| (k.to_string(), json!(v))).collect())
    }
}

fn load_into<T: serde::de::DeserializeOwned>(dir: Option<&Path>, name: &'static str, builtin: &str, sources: &mut Vec<(&'static str, String)>) -> Result<T, String> {
    let (value, source) = load_file(dir, &format!("{name}.toml"), builtin)?;
    sources.push((name, source));
    Ok(value)
}

fn load_file<T: serde::de::DeserializeOwned>(dir: Option<&Path>, name: &str, builtin: &str) -> Result<(T, String), String> {
    if let Some(path) = dir.map(|d| d.join(name)).filter(|p| p.is_file()) {
        let text = std::fs::read_to_string(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        let value = toml::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))?;
        return Ok((value, path.display().to_string()));
    }
    let value = toml::from_str(builtin).map_err(|e| format!("built-in {name}: {e}"))?;
    Ok((value, format!("builtin:{name}")))
}

fn load_property(dir: Option<&Path>, sources: &mut Vec<(&'static str, String)>) -> Result<Value, String> {
    if let Some(path) = dir.and_then(Path::parent).map(|d| d.join("property.json")).filter(|p| p.is_file()) {
        let text = std::fs::read_to_string(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        let value: serde_json::Value = serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))?;
        sources.push(("property", path.display().to_string()));
        return Ok(value);
    }
    let value: serde_json::Value = serde_json::from_str(PROPERTY_BUILTIN).map_err(|e| format!("built-in property.json: {e}"))?;
    sources.push(("property", "builtin:property.json".into()));
    Ok(value)
}

/// Names in a language (names.toml, ui.name.<kind>.<id>); the data's own (Traditional Chinese) where it has none.
fn name_catalog(mut c: Catalog, texts: &crate::locale::Texts, lang: &str) -> Catalog {
    for i in &mut c.items {
        if let Some(n) = texts.own(lang, &format!("ui.name.item.{}", i.id)) {
            i.name = n.to_string();
        }
    }
    c
}

fn name_places(base: &PlacesTuning, texts: &crate::locale::Texts, lang: &str) -> PlacesTuning {
    let mut p = base.clone();
    if let Some(n) = texts.own(lang, "ui.name.norm.default") {
        p.default.label = n.to_string();
    }
    for (id, norms) in p.norms.iter_mut() {
        if let Some(n) = texts.own(lang, &format!("ui.name.norm.{id}")) {
            norms.label = n.to_string();
        }
    }
    // The acts' names ("吃東西"), shown in notices ("你們在咖啡廳吃東西：她很自然地配合").
    if let Some(n) = texts.own(lang, "ui.name.place_act.default") {
        p.act_default.name = n.to_string();
    }
    for (id, act) in p.acts.iter_mut() {
        if let Some(n) = texts.own(lang, &format!("ui.name.place_act.{id}")) {
            act.name = n.to_string();
        }
    }
    p
}

/// property.json's names in a language, and the names its staff are given in the region ([names] of region.toml).
fn name_property(mut v: Value, reg: &RegionData, texts: &crate::locale::Texts, lang: &str) -> Value {
    for (list, kind) in [("units", "unit"), ("lots", "unit"), ("buildings", "building"), ("roles", "role")] {
        if let Some(a) = v.get_mut(list).and_then(Value::as_array_mut) {
            for o in a {
                let id = o.get("id").and_then(Value::as_str).unwrap_or("").to_string();
                if let Some(n) = texts.own(lang, &format!("ui.name.{kind}.{id}")) {
                    o["name"] = json!(n);
                }
            }
        }
    }
    if let Some(names) = &reg.names {
        v["names"] = serde_json::to_value(names).unwrap_or(Value::Null);
    }
    v
}

/// The catalog in a region's money: shelf prices snapped to its ladder, wages by its wage factor.
fn localize_catalog(base: &Catalog, reg: &RegionData) -> Catalog {
    let mut c = base.clone();
    for i in &mut c.items {
        for p in i.price.values_mut() {
            *p = reg.price(*p as i64) as i32;
        }
    }
    for (_, wage) in c.jobs.values_mut() {
        *wage = reg.amount("wage", *wage as i64) as i32;
    }
    c
}

/// property.json in a region's money: rents, prices and building costs, salaries; and the region's own payroll and
/// taxes (its [tax], amounts in its major unit) in place of the base region's.
fn localize_property(base: &Value, reg: &RegionData) -> Value {
    let mut v = base.clone();
    if reg.base {
        return v;
    }
    let scale = |o: &mut Value, key: &str, kind: &str| {
        if let Some(n) = o.get(key).and_then(Value::as_f64) {
            o[key] = json!(reg.amount(kind, n.round() as i64));
        }
    };
    for list in ["units", "lots"] {
        if let Some(a) = v.get_mut(list).and_then(Value::as_array_mut) {
            for u in a {
                scale(u, "rent", "rent");
                scale(u, "price", "property");
            }
        }
    }
    if let Some(a) = v.get_mut("buildings").and_then(Value::as_array_mut) {
        for b in a {
            scale(b, "cost", "property");
        }
    }
    if let Some(a) = v.get_mut("roles").and_then(Value::as_array_mut) {
        for r in a {
            scale(r, "base_pay", "salary");
        }
    }
    if let Some(tax) = &reg.tax {
        let mut t: Value = serde_json::to_value(tax).unwrap_or(Value::Null);
        let minor = |x: f64| reg.minor(x) as f64;
        for key in ["base_min", "base_max", "iit_threshold_monthly", "cit_small_limit_yearly"] {
            if let Some(n) = t.get(key).and_then(Value::as_f64) {
                t[key] = json!(minor(n));
            }
        }
        if let Some(rows) = t.get_mut("iit_brackets_yearly").and_then(Value::as_array_mut) {
            for row in rows {
                if let Some(r) = row.as_array_mut().filter(|r| r.len() == 3) {
                    let (upper, deduction) = (r[0].as_f64().unwrap_or(0.0), r[2].as_f64().unwrap_or(0.0));
                    r[0] = json!(minor(upper));
                    r[2] = json!(minor(deduction));
                }
            }
        }
        v["china"] = t;
    }
    v
}

/// A JSON data file beside the rules folder (StreetSocialData), else the copy built in.
fn load_data(dir: Option<&Path>, name: &'static str, builtin: &str, sources: &mut Vec<(&'static str, String)>) -> Result<serde_json::Value, String> {
    if let Some(path) = dir.and_then(Path::parent).map(|d| d.join(name)).filter(|p| p.is_file()) {
        let text = std::fs::read_to_string(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        sources.push((name, path.display().to_string()));
        return serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()));
    }
    sources.push((name, format!("builtin:{name}")));
    serde_json::from_str(builtin).map_err(|e| format!("built-in {name}: {e}"))
}
