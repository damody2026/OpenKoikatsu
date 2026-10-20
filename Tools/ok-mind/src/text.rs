//! ok-mind's own instructions to the model in the player's language (Docs/STREET_I18N_PLAN_20261014.md, P3): the
//! messages of Localization/<lang>/mind_text.toml, read with the rules' text tables (ok-rules locale.rs, ICU
//! MessageFormat; built into the DLL as the fallback). The language is the job's (`mind::job_lang`).
use ok_rules::locale::Texts;
use std::path::Path;
use std::sync::{OnceLock, RwLock};

static TEXTS: OnceLock<RwLock<Texts>> = OnceLock::new();

fn texts() -> &'static RwLock<Texts> {
    TEXTS.get_or_init(|| RwLock::new(Texts::builtin()))
}

/// Read the tables beside the game's data (data_dir = Content/IndependentResources); again whenever the mind is opened,
/// so a reload picks up edited tables.
pub fn init(data_dir: Option<&Path>) {
    let rules = data_dir.map(|d| d.join("StreetSocialData").join("Rules"));
    let loaded = Texts::load(rules.as_deref()).unwrap_or_else(|_| Texts::builtin());
    *texts().write().unwrap_or_else(|e| e.into_inner()) = loaded;
}

/// The message key ("mind.compact_prompt") in lang with its {arguments}.
pub fn t(lang: &str, key: &str, args: &[(&str, String)]) -> String {
    texts().read().unwrap_or_else(|e| e.into_inner()).t(lang, key, args)
}
