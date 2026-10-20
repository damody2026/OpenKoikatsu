//! Where tests find their data. The default run needs no game installation: it reads the project's OK shared
//! resources (`Content/IndependentResources`), the OK base library (`base_v1`) and the raw bundles kept with the paired
//! animations. Tests that compare against the original game itself (its bundles, cards, mesh dumps) are `#[ignore]`d
//! and read the installation from `OK_TEST_GAME_ROOT`; nothing names a machine's install path.
use std::path::{Path, PathBuf};

/// The project's OK shared resources.
pub(crate) fn resources() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources")
}

/// The OK base library, also made the configured one (`OK_BASE_LIBRARY`) for code that looks it up.
pub(crate) fn library() -> PathBuf {
    let lib = resources().join("base_v1");
    if std::env::var_os("OK_BASE_LIBRARY").is_none() { std::env::set_var("OK_BASE_LIBRARY", &lib); }
    lib
}

/// A root holding the original bundles the shared resources keep as files (`abdata/studio/anime`, `abdata/h`,
/// `abdata/chara/oo_base.unity3d`, ...): enough for animation sources without an installation.
pub(crate) fn shared_bundles() -> PathBuf {
    resources().join("PairedAnimations")
}

/// The original game installation (`OK_TEST_GAME_ROOT`) for the ignored game-oracle tests. Unset, this names a
/// directory that does not exist, so a test that skips without the game skips and one that needs it fails saying so.
pub(crate) fn game() -> PathBuf {
    std::env::var_os("OK_TEST_GAME_ROOT").map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("OK_TEST_GAME_ROOT is not set (needed only by ignored game-oracle tests)"))
}

/// `rest` (a path inside the installation, either separator) under `game()`. Without `OK_TEST_GAME_ROOT`, an original
/// file the shared resources keep (`shared_bundles()`, e.g. `abdata/chara/oo_base.unity3d`) is that copy instead.
pub(crate) fn game_path(rest: &str) -> PathBuf {
    let rest = rest.trim_start_matches(['/', '\\']);
    if rest.is_empty() { return game(); }
    if std::env::var_os("OK_TEST_GAME_ROOT").is_none() {
        let kept = shared_bundles().join(rest.replace('\\', "/"));
        if kept.is_file() { return kept; }
    }
    game().join(rest)
}

/// Whether `game_path(rest)` names a file the default run has (an installation, or a copy in the shared resources).
#[allow(dead_code)]
pub(crate) fn has_game_file(rest: &str) -> bool { game_path(rest).is_file() }

/// `game_path` as a `&'static Path` for call sites that borrow one (`Path::new(...)` spanning a statement). Tests
/// only: each call leaks its few bytes.
pub(crate) fn game_file(rest: &str) -> &'static Path {
    Box::leak(game_path(rest).into_boxed_path())
}

/// `game()` as a `/`-separated prefix ending in `/`, to strip from installation paths recorded in exported files.
pub(crate) fn game_prefix() -> String {
    format!("{}/", game().to_string_lossy().replace('\\', "/").trim_end_matches('/'))
}

/// The OK base library, active: reads of `abdata/...` and `mods/...` under it are served from the library
/// (gamefs replay) as from an installation. Paths outside the library still read the file system.
pub(crate) fn active_library() -> PathBuf {
    let lib = library();
    crate::oklib::ensure_library(&lib).expect("OK base library");
    lib
}

/// A game root for tests that only read the original bundles the shared resources keep (animations, `oo_base`):
/// the installation when `OK_TEST_GAME_ROOT` is set, otherwise `shared_bundles()`.
#[allow(dead_code)]
pub(crate) fn bundles_root() -> PathBuf {
    if std::env::var_os("OK_TEST_GAME_ROOT").is_some() { game() } else { shared_bundles() }
}
