//! Tauri command handlers: the typed IPC surface for the Svelte frontend.

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use serde::{Deserialize, Serialize};
use tauri::State;

use crate::cache::MetadataCache;
use crate::error::{AppError, AppResult};
use crate::metadata::{self, providers::ProviderInfo, steam_web, steam_web::OwnedGame};
use crate::platforms::{self, Platform, PlatformGame, PlatformScan};
use crate::plugins::PluginEngine;
use crate::settings::{self, Settings};
use crate::steam::models::{AppMeta, Collection, Game, GameKind, SteamInstall};
use crate::steam::owned::{self, OwnedGameRecord, OwnedLibrary};
use crate::steam::storage::StorageReport;
use crate::steam::{collections, library, paths, process, shortcuts};

/// Shared application state managed by Tauri.
pub struct AppState {
    pub settings: Mutex<Settings>,
    pub cache: Mutex<MetadataCache>,
    pub plugins: Mutex<PluginEngine>,
}

impl AppState {
    pub fn new() -> Self {
        let cache = MetadataCache::open();
        let plugins = PluginEngine::init();
        AppState {
            settings: Mutex::new(settings::load()),
            cache: Mutex::new(cache),
            plugins: Mutex::new(plugins),
        }
    }

    pub fn settings_snapshot(&self) -> Settings {
        self.settings
            .lock()
            .expect("settings lock poisoned")
            .clone()
    }
}

impl Default for AppState {
    fn default() -> Self {
        Self::new()
    }
}

fn steam_root(state: &AppState) -> AppResult<PathBuf> {
    let settings = state.settings_snapshot();
    if let Some(root) = settings.steam_root.filter(|root| !root.is_empty()) {
        return Ok(PathBuf::from(root));
    }
    paths::discover_steam_root().ok_or_else(|| {
        AppError::NotFound("Steam installation not found. Set the Steam folder in Settings.".into())
    })
}

fn resolve_account(root: &Path, state: &AppState, account: Option<String>) -> Option<String> {
    let settings = state.settings_snapshot();
    account
        .filter(|value| !value.is_empty())
        .or(settings.account_id3)
        .or_else(|| paths::read_accounts(root).first().map(|a| a.id3.clone()))
}


// ---------------------------------------------------------------------------
// Discovery & settings
// ---------------------------------------------------------------------------

#[tauri::command]
pub fn get_providers() -> Vec<ProviderInfo> {
    metadata::providers::all()
}

#[tauri::command]
pub fn detect_steam() -> Option<SteamInstall> {
    let root = paths::discover_steam_root()?;
    paths::load_install(&root).ok()
}

#[tauri::command]
pub fn get_settings(state: State<'_, AppState>) -> Settings {
    state.settings_snapshot()
}

#[tauri::command]
pub fn save_settings(settings: Settings, state: State<'_, AppState>) -> AppResult<Settings> {
    settings::save(&settings)?;
    *state.settings.lock().expect("settings lock poisoned") = settings.clone();
    Ok(settings)
}

#[tauri::command]
pub fn is_steam_running() -> bool {
    process::is_steam_running()
}

// ---------------------------------------------------------------------------
// Library & collections
// ---------------------------------------------------------------------------

#[tauri::command]
pub async fn load_library(
    account_id3: Option<String>,
    force_reload: Option<bool>,
    state: State<'_, AppState>,
) -> AppResult<Vec<Game>> {
    if let Some(n) = crate::bench::mock_library_size() {
        return Ok(crate::bench::mock_games(n));
    }
    let cache_key = format!("library:{}", account_id3.as_deref().unwrap_or("default"));
    if force_reload != Some(true) {
        let cache = state.cache.lock().expect("cache lock poisoned");
        if let Some(cached) = cache.get_kv_json::<Vec<Game>>(&cache_key) {
            return Ok(cached);
        }
    }

    let root_opt = steam_root(&state).ok();
    let root = root_opt.unwrap_or_else(|| PathBuf::from("."));
    let account = resolve_account(&root, &state, account_id3);

    // Load Steam VDF manifests and play stats on a background blocking thread so IPC never blocks
    let root_clone = root.clone();
    let account_clone = account.clone();
    let mut games = tauri::async_runtime::spawn_blocking(move || -> AppResult<Vec<Game>> {
        let mut loaded = library::load_games(&root_clone).unwrap_or_default();
        if let Some(ref id3) = account_clone {
            let stats = library::load_app_stats(&root_clone, id3);
            for game in loaded.iter_mut() {
                if let Some((last_played, playtime)) = stats.get(&game.app_id) {
                    game.last_played = *last_played;
                    game.playtime_minutes = *playtime;
                }
            }
        }
        Ok(loaded)
    })
    .await
    .map_err(|e| AppError::Io(std::io::Error::new(std::io::ErrorKind::Other, e.to_string())))??;

    finalize_library(&mut games, &state);

    {
        let cache = state.cache.lock().expect("cache lock poisoned");
        // Cache library in Redis KV fast-path with permanent TTL (None) for instant tab switching
        let _ = cache.set_kv_json(&cache_key, &games, None);
    }

    Ok(games)
}

/// Assemble the full library for organization.
fn collect_library(root: &Path, account: Option<&str>, state: &AppState) -> AppResult<Vec<Game>> {
    let mut games = library::load_games(root).unwrap_or_default();

    if let Some(id3) = account {
        let stats = library::load_app_stats(root, id3);
        for game in games.iter_mut() {
            if let Some((last_played, playtime)) = stats.get(&game.app_id) {
                game.last_played = *last_played;
                game.playtime_minutes = *playtime;
            }
        }
    }

    finalize_library(&mut games, state);
    Ok(games)
}

fn finalize_library(games: &mut Vec<Game>, state: &AppState) {
    // Merge custom imported platform games persisted in local SQLite app_kv,
    // automatically pruning any launcher clients that were previously imported by mistake,
    // inspecting local manifests (e.g. RPG Maker System.json / itch receipt),
    // normalizing game titles across all platforms, and deduplicating by path & canonical title.
    {
        let cache = state.cache.lock().expect("cache lock poisoned");
        if let Some(mut custom_games) = cache.get_kv_json::<Vec<Game>>("library:custom_games") {
            let orig_len = custom_games.len();
            let mut modified = false;

            custom_games.retain_mut(|cg| {
                let n = cg.name.trim().to_lowercase();
                let is_launcher = [
                    "ubisoft game launcher",
                    "ubisoft connect",
                    "gog galaxy",
                    "ea desktop",
                    "origin",
                    "epic games launcher",
                    "launcher",
                    "legendary",
                    "games",
                    "broth",
                    "rockstar games launcher",
                    "rockstar games social club",
                    "battle.net",
                ]
                .contains(&n.as_str());
                if is_launcher {
                    return false;
                }

                // Also prune if install_dir or exe points to Launcher / Legendary / itch/games parent
                if let Some(ref dir_str) = cg.install_dir {
                    let dl = dir_str.replace('/', "\\").to_lowercase();
                    if dl.ends_with("\\epic games\\launcher")
                        || dl.ends_with("\\epic games\\legendary")
                        || dl.ends_with("\\itch\\games")
                    {
                        return false;
                    }
                }
                if let Some(ref exe_str) = cg.exe {
                    let el = exe_str.replace('/', "\\").to_lowercase();
                    if el.ends_with("\\legendary.exe")
                        || el.ends_with("\\epiconlineservicesinstaller.exe")
                    {
                        return false;
                    }
                }

                // If non-emulated game has an install_dir, check if local manifest has a cleaner title
                // (e.g. relicts-auf-aeson -> Relicts of Aeson from RPG Maker System.json)
                if cg.platform != Platform::EmulationStation && cg.console.is_none() {
                    let dir_path = cg.install_dir.as_deref().map(Path::new);
                    let exe_path = cg.exe.as_deref().map(Path::new);
                    if let Some(local_meta) =
                        crate::metadata::local::inspect_local_game(dir_path, exe_path)
                    {
                        if let Some(local_title) = local_meta.name.filter(|t| !t.trim().is_empty()) {
                            if local_title != cg.name {
                                cg.name = local_title;
                                modified = true;
                            }
                        }
                    }
                }

                let normalized = crate::platforms::normalize_game_title(&cg.name);
                if !normalized.is_empty() && normalized != cg.name {
                    cg.name = normalized;
                    modified = true;
                }
                true
            });

            // Heal legacy uninstalled games that stored cmd.exe / xdg-open as exe
            for cg in &mut custom_games {
                if !cg.installed && cg.exe.as_deref().map_or(false, |e| e.ends_with("cmd.exe") || e.ends_with("xdg-open")) {
                    cg.exe = None;
                    cg.install_dir = None;
                    modified = true;
                }
            }

            // Sort custom_games so installed games come before uninstalled games during deduplication
            custom_games.sort_by_key(|g| (!g.installed, g.name.clone()));

            // Deduplicate custom_games by install_dir/exe and by (platform, console, canonical_title_key)
            let mut deduped: Vec<Game> = Vec::with_capacity(custom_games.len());
            let mut seen_paths = BTreeSet::new();
            let mut seen_titles = BTreeSet::new();
            for cg in custom_games {
                let path_key = cg
                    .exe
                    .as_deref()
                    .or(cg.install_dir.as_deref())
                    .unwrap_or("")
                    .replace('/', "\\")
                    .to_lowercase();
                if !path_key.is_empty() && !seen_paths.insert(path_key) {
                    modified = true;
                    continue;
                }
                let title_key = format!(
                    "{}:{}:{}",
                    cg.platform.id(),
                    cg.console.as_deref().unwrap_or("").to_lowercase(),
                    crate::plugins::title_normalizer::canonical_title_key(&cg.name)
                );
                if !seen_titles.insert(title_key) {
                    modified = true;
                    continue;
                }
                deduped.push(cg);
            }

            if deduped.len() != orig_len || modified {
                let _ = cache.set_kv_json("library:custom_games", &deduped, None);
            }
            for cg in deduped {
                if !games.iter().any(|g| g.app_id == cg.app_id) {
                    games.push(cg);
                }
            }
        }
    }

    merge_owned(games, &owned::load());

    // Normalize titles across all games in the library
    for game in games.iter_mut() {
        let normalized = crate::platforms::normalize_game_title(&game.name);
        if !normalized.is_empty() {
            game.name = normalized;
        }
    }

    // Apply collection membership from local SQLite collections
    {
        let cache = state.cache.lock().expect("cache lock poisoned");
        let cols = cache.load_collections_from_db();
        apply_membership(games, &cols);
    }

    // Populate cached store artwork URLs (capsule cover, hero banner, logo)
    {
        let cache = state.cache.lock().expect("cache lock poisoned");

        for game in games.iter_mut() {
            let meta = cache
                .get_kv_json::<AppMeta>(&format!("game_meta:{}", game.app_id))
                .or_else(|| cache.get("shortcut-meta", game.app_id, None))
                .or_else(|| cache.get("steam-store", game.app_id, None));
            if let Some(m) = meta {
                if game.capsule_image.is_none() {
                    game.capsule_image = m.capsule_image.or(m.header_image);
                }
                if game.hero_image.is_none() {
                    game.hero_image = m.hero_image;
                }
            }
        }
    }
}

/// Fold the cached owned library into the local game list. Games already known
/// from disk keep their install data; owned-only games are added as
/// not-installed Steam entries.
fn merge_owned(games: &mut Vec<Game>, owned: &OwnedLibrary) {
    let mut index: BTreeMap<u32, usize> = games
        .iter()
        .enumerate()
        .map(|(position, game)| (game.app_id, position))
        .collect();

    for record in &owned.games {
        match index.get(&record.app_id) {
            Some(&position) => {
                let game = &mut games[position];
                if game.name.is_empty() {
                    game.name = record.name.clone();
                }
                if game.playtime_minutes.is_none() {
                    game.playtime_minutes = Some(record.playtime_minutes);
                }
            }
            None => {
                index.insert(record.app_id, games.len());
                games.push(Game {
                    app_id: record.app_id,
                    name: record.name.clone(),
                    kind: GameKind::Steam,
                    installed: false,
                    playtime_minutes: Some(record.playtime_minutes),
                    ..Default::default()
                });
            }
        }
    }
}

fn apply_membership(games: &mut [Game], collections_list: &[Collection]) {
    let mut membership: BTreeMap<u32, Vec<String>> = BTreeMap::new();
    let mut favorites: BTreeSet<u32> = BTreeSet::new();
    let mut hidden: BTreeSet<u32> = BTreeSet::new();

    for collection in collections_list {
        for app_id in &collection.added {
            membership
                .entry(*app_id)
                .or_default()
                .push(collection.name.clone());
            if collection.favorite {
                favorites.insert(*app_id);
            }
            if collection.hidden {
                hidden.insert(*app_id);
            }
        }
    }

    for game in games.iter_mut() {
        if let Some(names) = membership.get(&game.app_id) {
            game.collections = names.clone();
        }
        game.favorite = favorites.contains(&game.app_id);
        game.hidden = hidden.contains(&game.app_id);
    }
}

#[tauri::command]
pub fn load_collections(
    account_id3: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<Vec<Collection>> {
    let cache = state.cache.lock().expect("cache lock poisoned");
    // 1. Check in-memory KV tier first (<0.1ms)
    if let Some(cols) = cache.get_kv_json::<Vec<Collection>>("collections:default") {
        if !cols.is_empty() {
            return Ok(cols);
        }
    }

    // 2. Load from local SQLite collections table
    let cols = cache.load_collections_from_db();
    if !cols.is_empty() {
        let _ = cache.set_kv_json("collections:default", &cols, None);
        return Ok(cols);
    }

    // 3. Fallback: on initial run, if SQLite collections table is empty, seed from Steam collections read-only
    drop(cache);
    if let Ok(root) = steam_root(&state) {
        if let Some(id3) = resolve_account(&root, &state, account_id3) {
            if let Ok(Some(file)) = collections::load_collections_file(&root, &id3) {
                let steam_cols = file.collections();
                if !steam_cols.is_empty() {
                    let cache = state.cache.lock().expect("cache lock poisoned");
                    let _ = cache.save_collections_to_db(&steam_cols);
                    return Ok(steam_cols);
                }
            }
        }
    }

    Ok(Vec::new())
}


#[tauri::command]
pub fn update_collection(
    collection_id: String,
    name: Option<String>,
    add_app_ids: Option<Vec<u32>>,
    remove_app_ids: Option<Vec<u32>>,
    _account_id3: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<Collection> {
    let cache = state.cache.lock().expect("cache lock poisoned");
    let cols = cache.load_collections_from_db();

    let mut collection = cols
        .into_iter()
        .find(|c| c.id == collection_id)
        .unwrap_or(Collection {
            id: collection_id.clone(),
            name: collection_id.clone(),
            favorite: collection_id == "favorite",
            hidden: collection_id == "hidden",
            dynamic: false,
            added: Vec::new(),
            removed: Vec::new(),
            filter_spec: None,
        });

    if let Some(name) = name {
        collection.name = name;
    }
    if let Some(add) = add_app_ids {
        for app_id in add {
            if !collection.added.contains(&app_id) {
                collection.added.push(app_id);
            }
            collection.removed.retain(|id| *id != app_id);
        }
    }
    if let Some(remove) = remove_app_ids {
        for app_id in remove {
            collection.added.retain(|id| *id != app_id);
            if !collection.removed.contains(&app_id) {
                collection.removed.push(app_id);
            }
        }
    }

    cache.upsert_collection_in_db(&collection)?;

    // Synchronize active in-memory library
    if let Some(mut games) = cache.get_kv_json::<Vec<Game>>("library:default") {
        apply_membership(&mut games, &cache.load_collections_from_db());
        let _ = cache.set_kv_json("library:default", &games, None);
    }

    Ok(collection)
}

#[tauri::command]
pub fn create_collection(
    name: String,
    _account_id3: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<Collection> {
    let nanos = chrono::Utc::now().timestamp_nanos_opt().unwrap_or_default();
    let collection = Collection {
        id: format!("uc-{nanos:x}"),
        name,
        favorite: false,
        hidden: false,
        dynamic: false,
        added: Vec::new(),
        removed: Vec::new(),
        filter_spec: None,
    };

    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.upsert_collection_in_db(&collection)?;
    Ok(collection)
}

#[tauri::command]
pub fn delete_collection(
    collection_id: String,
    _account_id3: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<()> {
    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.delete_collection_from_db(&collection_id)?;
    if let Some(mut games) = cache.get_kv_json::<Vec<Game>>("library:default") {
        apply_membership(&mut games, &cache.load_collections_from_db());
        let _ = cache.set_kv_json("library:default", &games, None);
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

const CACHE_MAX_AGE_SECS: i64 = 60 * 60 * 24 * 7;

#[tauri::command]
pub async fn fetch_steam_store_metadata(
    app_ids: Vec<u32>,
    state: State<'_, AppState>,
) -> AppResult<Vec<AppMeta>> {
    let client = metadata::http_client();
    let settings = state.settings_snapshot();

    let mut result = Vec::new();
    let mut missing = Vec::new();
    {
        let cache = state.cache.lock().expect("cache lock poisoned");
        for app_id in &app_ids {
            match cache.get("steam-store", *app_id, Some(CACHE_MAX_AGE_SECS)) {
                Some(meta) => result.push(meta),
                None => missing.push(*app_id),
            }
        }
    }

    if !missing.is_empty() {
        let fetched = metadata::steam_store::fetch_app_details(
            &client,
            &missing,
            &settings.country_code,
            &settings.language,
        )
        .await?;
        let mut cache = state.cache.lock().expect("cache lock poisoned");
        for (app_id, meta) in fetched {
            cache.put("steam-store", app_id, &meta);
            result.push(meta);
        }
        let _ = cache.save();
    }

    Ok(result)
}

/// Maximum app ids per `appdetails` request (the endpoint accepts a CSV list).
const METADATA_BATCH_SIZE: usize = 50;

/// Summary of a metadata sync pass, surfaced to the UI.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MetadataSyncResult {
    /// Distinct app ids considered.
    pub requested: usize,
    /// Entries newly downloaded from the Steam Store.
    pub fetched: usize,
    /// Community tag lists newly downloaded.
    pub tags_fetched: usize,
    /// Entries already fresh in the local cache.
    pub cached: usize,
    /// Ids the store could not answer for (not a store app, delisted, or a
    /// transient network failure).
    pub unavailable: usize,
}

/// Ensure the metadata cache holds fresh entries for every id in `app_ids`.
///
/// Steam's `appdetails` endpoint only returns data for real store apps, so
/// callers pass the ids they actually intend to categorize; unknown ids simply
/// count as [`MetadataSyncResult::unavailable`]. Entries younger than
/// [`CACHE_MAX_AGE_SECS`] are reused. Network failures are tolerated so that
/// categorization still works offline for kinds that do not need metadata.
///
/// When `want_tags` is set, Steam community tags are fetched too (one request
/// per app) and cached under the `steam-tags` provider key.
pub async fn ensure_steam_metadata(
    state: &AppState,
    app_ids: &[u32],
    want_tags: bool,
    force: bool,
) -> MetadataSyncResult {
    let mut unique: Vec<u32> = app_ids.iter().copied().filter(|id| *id != 0).collect();
    unique.sort_unstable();
    unique.dedup();

    // Split into entries we can reuse and entries we must fetch.
    let missing: Vec<u32> = if force {
        unique.clone()
    } else {
        let cache = state.cache.lock().expect("cache lock poisoned");
        unique
            .iter()
            .copied()
            .filter(|id| {
                cache
                    .get("steam-store", *id, Some(CACHE_MAX_AGE_SECS))
                    .is_none()
            })
            .collect()
    };

    let requested = unique.len();
    let cached = requested - missing.len();
    let mut fetched = 0usize;

    if !missing.is_empty() {
        let settings = state.settings_snapshot();
        let client = metadata::http_client();
        for chunk in missing.chunks(METADATA_BATCH_SIZE) {
            if let Ok(map) = metadata::steam_store::fetch_app_details(
                &client,
                chunk,
                &settings.country_code,
                &settings.language,
            )
            .await
            {
                let mut cache = state.cache.lock().expect("cache lock poisoned");
                for (app_id, meta) in &map {
                    cache.put("steam-store", *app_id, meta);
                    let kv_key = format!("game_meta:{}", app_id);
                    let existing_kv = cache.get_kv_json::<AppMeta>(&kv_key);
                    if force
                        || existing_kv.as_ref().map_or(true, |m| {
                            m.user_enhanced != Some(true) || is_placeholder_meta(m)
                        })
                    {
                        let _ = cache.set_kv_json(&kv_key, meta, None);
                    }
                }
                fetched += map.len();
            }
        }
    }

    // Community tags come from a separate (per-app) endpoint, so they are only
    // fetched when a caller actually needs them.
    let mut tags_fetched = 0usize;
    if want_tags {
        let needing: Vec<u32> = {
            let cache = state.cache.lock().expect("cache lock poisoned");
            unique
                .iter()
                .copied()
                .filter(|id| {
                    cache
                        .get("steam-tags", *id, Some(CACHE_MAX_AGE_SECS))
                        .is_none()
                })
                .collect()
        };
        if !needing.is_empty() {
            let settings = state.settings_snapshot();
            let client = metadata::http_client();
            let tags = metadata::steam_store::fetch_tags(
                &client,
                &needing,
                &settings.country_code,
                &settings.language,
            )
            .await;
            let mut cache = state.cache.lock().expect("cache lock poisoned");
            for (app_id, names) in &tags {
                let meta = AppMeta {
                    app_id: *app_id,
                    tags: names.clone(),
                    ..Default::default()
                };
                cache.put("steam-tags", *app_id, &meta);
            }
            let _ = cache.save();
            tags_fetched = tags.len();
        }
    }

    MetadataSyncResult {
        requested,
        fetched,
        tags_fetched,
        cached,
        unavailable: requested.saturating_sub(cached + fetched),
    }
}

/// Ensure metadata is populated for a list of games (both Steam and Non-Steam shortcuts).
pub async fn ensure_game_metadata(
    state: &AppState,
    games: &[&Game],
    want_tags: bool,
    force: bool,
) -> MetadataSyncResult {
    let steam_ids: Vec<u32> = games
        .iter()
        .filter(|game| game.kind == GameKind::Steam && game.app_id != 0)
        .map(|game| game.app_id)
        .collect();

    let mut result = ensure_steam_metadata(state, &steam_ids, want_tags, force).await;


    // Resolve non-Steam shortcuts across universal metadata providers
    let client = metadata::http_client();
    let settings = state.settings_snapshot();

    for game in games {
        if game.kind != GameKind::Shortcut {
            continue;
        }

        let is_cached = if force {
            false
        } else {
            let cache = state.cache.lock().expect("cache lock poisoned");
            cache
                .get("shortcut-meta", game.app_id, Some(CACHE_MAX_AGE_SECS))
                .filter(|m| !is_placeholder_meta(m))
                .is_some()
        };

        if is_cached {
            result.cached += 1;
            continue;
        }

        result.requested += 1;
        let final_meta = metadata::universal::resolve_game_metadata(
            &client,
            game,
            &settings.country_code,
            &settings.language,
        )
        .await;

        let mut cache = state.cache.lock().expect("cache lock poisoned");
        cache.put("shortcut-meta", game.app_id, &final_meta);
        cache.put("steam-store", game.app_id, &final_meta);
        // Also populate game_meta:<appId> if not user-enhanced
        let kv_key = format!("game_meta:{}", game.app_id);
        let existing_kv = cache.get_kv_json::<AppMeta>(&kv_key);
        if force
            || existing_kv.as_ref().map_or(true, |m| {
                m.user_enhanced != Some(true) || is_placeholder_meta(m)
            })
        {
            let _ = cache.set_kv_json(&kv_key, &final_meta, None);
        }
        result.fetched += 1;
    }

    result
}

fn is_placeholder_meta(m: &AppMeta) -> bool {
    m.short_description.as_deref().map_or(false, |d| {
        d.starts_with("Emulated title for ") || d == "Local standalone executable game."
    })
}

async fn get_library_fast(account_id3: Option<String>, state: &AppState) -> AppResult<Vec<Game>> {
    let cache_key = format!("library:{}", account_id3.as_deref().unwrap_or("default"));
    {
        let cache = state.cache.lock().expect("cache lock poisoned");
        if let Some(cached) = cache.get_kv_json::<Vec<Game>>(&cache_key) {
            return Ok(cached);
        }
    }

    let root = steam_root(state).unwrap_or_else(|_| PathBuf::from("."));
    let account = resolve_account(&root, state, account_id3);
    let root_clone = root.clone();
    let account_clone = account.clone();
    let mut games = tauri::async_runtime::spawn_blocking(move || -> AppResult<Vec<Game>> {
        let mut loaded = library::load_games(&root_clone).unwrap_or_default();
        if let Some(ref id3) = account_clone {
            let stats = library::load_app_stats(&root_clone, id3);
            for game in loaded.iter_mut() {
                if let Some((last_played, playtime)) = stats.get(&game.app_id) {
                    game.last_played = *last_played;
                    game.playtime_minutes = *playtime;
                }
            }
        }
        Ok(loaded)
    })
    .await
    .map_err(|e| AppError::Io(std::io::Error::new(std::io::ErrorKind::Other, e.to_string())))??;

    finalize_library(&mut games, state);
    Ok(games)
}

/// Warm (or refresh) store metadata for the whole local library across all supported providers.
#[tauri::command]
pub async fn sync_library_metadata(
    account_id3: Option<String>,
    force: Option<bool>,
    _auto_artwork: Option<bool>,
    state: State<'_, AppState>,
) -> AppResult<MetadataSyncResult> {
    let games = get_library_fast(account_id3, &state).await?;
    let refs: Vec<&Game> = games.iter().collect();
    let is_force = force.unwrap_or(false);
    let result = ensure_game_metadata(&state, &refs, false, is_force).await;

    Ok(result)
}

/// Refresh metadata and artwork references for an individual game across universal metadata providers.
#[tauri::command]
pub async fn refresh_game_metadata(
    app_id: u32,
    _force: Option<bool>,
    account_id3: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<Option<AppMeta>> {
    let games = get_library_fast(account_id3, &state).await?;
    let Some(game) = games.into_iter().find(|g| g.app_id == app_id) else {
        return Ok(None);
    };

    let client = metadata::http_client();
    let settings = state.settings_snapshot();
    let mut final_meta = metadata::universal::resolve_game_metadata(
        &client,
        &game,
        &settings.country_code,
        &settings.language,
    )
    .await;

    let existing_cached = {
        let cache = state.cache.lock().expect("cache lock poisoned");
        cache
            .get_kv_json::<AppMeta>(&format!("game_meta:{app_id}"))
            .or_else(|| cache.get("shortcut-meta", app_id, None))
            .or_else(|| cache.get("steam-store", app_id, None))
            .filter(|m| !is_placeholder_meta(m))
    };

    let user_enhanced = existing_cached
        .as_ref()
        .and_then(|m| m.user_enhanced)
        .unwrap_or(false);
    let is_cache_reset = _force.unwrap_or(false);

    // Invariant: Preserve user customizations (`user_enhanced: true`) unless resetting cache.
    if user_enhanced && !is_cache_reset {
        if let Some(existing) = existing_cached {
            if existing.name.is_some() {
                final_meta.name = existing.name;
            }
            if existing.short_description.is_some() {
                final_meta.short_description = existing.short_description;
            }
            if !existing.developers.is_empty() {
                final_meta.developers = existing.developers;
            }
            if !existing.publishers.is_empty() {
                final_meta.publishers = existing.publishers;
            }
            if !existing.genres.is_empty() {
                final_meta.genres = existing.genres;
            }
            if !existing.tags.is_empty() {
                final_meta.tags = existing.tags;
            }
            if existing.release_date.is_some() {
                final_meta.release_date = existing.release_date;
            }
            if existing.release_year.is_some() {
                final_meta.release_year = existing.release_year;
            }
            if existing.capsule_image.is_some() {
                final_meta.capsule_image = existing.capsule_image;
            }
            if existing.hero_image.is_some() {
                final_meta.hero_image = existing.hero_image;
            }
            if existing.logo_image.is_some() {
                final_meta.logo_image = existing.logo_image;
            }
            if existing.user_score.is_some() {
                final_meta.user_score = existing.user_score;
            }
            final_meta.user_enhanced = Some(true);
            final_meta.enhanced_by = existing.enhanced_by;
        }
    }

    let mut cache = state.cache.lock().expect("cache lock poisoned");
    cache.put("shortcut-meta", game.app_id, &final_meta);
    cache.put("steam-store", game.app_id, &final_meta);
    let _ = cache.set_kv_json(&format!("game_meta:{}", game.app_id), &final_meta, None);
    Ok(Some(final_meta))
}

#[tauri::command]
pub async fn fetch_owned_games(state: State<'_, AppState>) -> AppResult<Vec<OwnedGame>> {
    let settings = state.settings_snapshot();
    let key = settings
        .steam_api_key
        .filter(|key| !key.is_empty())
        .ok_or_else(|| AppError::msg("No Steam Web API key configured."))?;
    let steamid64 = settings
        .account_id3
        .as_deref()
        .and_then(paths::id3_to_steamid64)
        .map(|v| v.to_string())
        .ok_or_else(|| AppError::msg("No SteamID64 available for the selected account."))?;
    let client = metadata::http_client();
    steam_web::get_owned_games(&client, &key, &steamid64, true).await
}

#[tauri::command]
pub async fn steam_store_search(
    term: String,
    state: State<'_, AppState>,
) -> AppResult<Vec<metadata::steam_search::SteamSearchResult>> {
    let client = metadata::http_client();
    let settings = state.settings_snapshot();
    metadata::steam_search::search_steam_store(
        &client,
        &term,
        &settings.country_code,
        &settings.language,
    )
    .await
}

// ---------------------------------------------------------------------------
// External platforms & the full owned library
// ---------------------------------------------------------------------------

/// The outcome of importing platform games as non-Steam shortcuts.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportResult {
    pub added: Vec<Game>,
    pub skipped: Vec<String>,
}

/// Scan the machine for games installed across platforms.
#[tauri::command]
pub async fn detect_platforms(
    include_other_drives: Option<bool>,
    state: State<'_, AppState>,
) -> AppResult<PlatformScan> {
    scan_all_platforms(include_other_drives, state).await
}

/// Existing library folders for one platform, used to autofill Settings.
#[tauri::command]
pub fn find_platform_folders(platform: String, state: State<'_, AppState>) -> AppResult<Vec<String>> {
    let settings = state.settings_snapshot();
    let roots = settings
        .platform_sources
        .into_iter()
        .filter(|s| s.id == platform)
        .flat_map(|s| s.roots)
        .collect();
    Ok(roots)
}

/// Import selected platform games as local games without modifying Steam files.
#[tauri::command]
pub async fn import_platform_games(
    games: Vec<PlatformGame>,
    _account_id3: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<ImportResult> {
    let mut skipped = Vec::new();
    let mut added_games = Vec::new();

    for game in games {
        let (exe, _launch_options) = match game.exe.clone().filter(|value| !value.is_empty()) {
            Some(raw_exe) => {
                let ext = Path::new(&raw_exe)
                    .extension()
                    .and_then(|s| s.to_str())
                    .unwrap_or("")
                    .to_lowercase();
                if ext == "html" || ext == "htm" {
                    #[cfg(windows)]
                    {
                        (
                            "cmd.exe".to_string(),
                            format!("/c start \"\" \"{}\"", raw_exe),
                        )
                    }
                    #[cfg(not(windows))]
                    {
                        ("xdg-open".to_string(), format!("\"{}\"", raw_exe))
                    }
                } else if ext == "jar" {
                    ("java.exe".to_string(), format!("-jar \"{}\"", raw_exe))
                } else if ext == "love" {
                    ("love.exe".to_string(), format!("\"{}\"", raw_exe))
                } else if ext == "py" {
                    ("pythonw.exe".to_string(), format!("\"{}\"", raw_exe))
                } else if ext == "bat" || ext == "cmd" {
                    #[cfg(windows)]
                    {
                        ("cmd.exe".to_string(), format!("/c \"{}\"", raw_exe))
                    }
                    #[cfg(not(windows))]
                    {
                        (raw_exe, game.launch_uri.clone().unwrap_or_default())
                    }
                } else if game.platform == Platform::Xbox
                    && game
                        .launch_uri
                        .as_ref()
                        .map(|u| u.starts_with("shell:AppsFolder"))
                        .unwrap_or(false)
                {
                    #[cfg(windows)]
                    {
                        (
                            "C:\\Windows\\explorer.exe".to_string(),
                            game.launch_uri.clone().unwrap_or_default(),
                        )
                    }
                    #[cfg(not(windows))]
                    {
                        (raw_exe, game.launch_uri.clone().unwrap_or_default())
                    }
                } else {
                    (raw_exe.clone(), String::new())
                }
            }
            None => {
                if let Some(ref uri) = game.launch_uri {
                    #[cfg(windows)]
                    {
                        if uri.starts_with("shell:AppsFolder") {
                            ("C:\\Windows\\explorer.exe".to_string(), uri.clone())
                        } else {
                            ("cmd.exe".to_string(), format!("/c start \"\" \"{}\"", uri))
                        }
                    }
                    #[cfg(not(windows))]
                    {
                        ("xdg-open".to_string(), uri.clone())
                    }
                } else {
                    skipped.push(format!(
                        "{} (no executable or launch target found)",
                        game.name
                    ));
                    continue;
                }
            }
        };

        let start_dir = Path::new(&exe)
            .parent()
            .map(|parent| parent.display().to_string())
            .or_else(|| game.install_dir.clone())
            .unwrap_or_default();

        let local_meta = metadata::local::inspect_local_game(
            Some(Path::new(&start_dir)),
            Some(Path::new(&exe)),
        );

        let raw_name = local_meta
            .as_ref()
            .and_then(|m| m.name.as_deref())
            .filter(|n| !n.trim().is_empty())
            .unwrap_or(&game.name);

        let normalized = crate::platforms::normalize_game_title(raw_name);
        let final_name = if normalized.is_empty() {
            raw_name.to_string()
        } else {
            normalized
        };

        let mut tags = Vec::new();
        if let Some(ref console) = game.console {
            tags.push(console.clone());
        }

        let seed = if game.installed {
            &exe
        } else if let Some(ref pid) = game.platform_id {
            pid
        } else {
            &exe
        };
        let app_id = shortcuts::shortcut_app_id(seed, &final_name);

        if let Some(mut lm) = local_meta {
            lm.app_id = app_id;
            if lm.name.as_deref().unwrap_or("").trim().is_empty() {
                lm.name = Some(final_name.clone());
            }
            let mut cache = state.cache.lock().expect("cache lock poisoned");
            cache.put("shortcut-meta", app_id, &lm);
        }

        added_games.push(Game {
            app_id,
            name: final_name,
            exe: if game.installed { Some(exe) } else { None },
            install_dir: if game.installed && !start_dir.is_empty() { Some(start_dir) } else { None },
            launch_uri: game.launch_uri.clone(),
            install_uri: game.install_uri.clone().or_else(|| game.launch_uri.clone()),
            uninstall_uri: game.uninstall_uri.clone(),
            platform_id: game.platform_id.clone(),
            platform: game.platform,
            kind: GameKind::Shortcut,
            installed: game.installed,
            collections: tags,
            console: game.console,
            size_on_disk: game.size_on_disk.unwrap_or(0),
            ..Default::default()
        });
    }

    if !added_games.is_empty() {
        let pairs: Vec<(u32, Platform)> = added_games
            .iter()
            .map(|game| (game.app_id, game.platform))
            .collect();
        let _ = platforms::record(&pairs);

        let cache = state.cache.lock().expect("cache lock poisoned");
        let mut custom_games = cache
            .get_kv_json::<Vec<Game>>("library:custom_games")
            .unwrap_or_default();
        for g in &added_games {
            if let Some(pos) = custom_games.iter().position(|cg| cg.app_id == g.app_id) {
                custom_games[pos] = g.clone();
            } else {
                custom_games.push(g.clone());
            }
        }
        let _ = cache.set_kv_json("library:custom_games", &custom_games, None);

        // Update in-memory library tier
        if let Some(mut mem_lib) = cache.get_kv_json::<Vec<Game>>("library:default") {
            for g in &added_games {
                if let Some(pos) = mem_lib.iter().position(|ml| ml.app_id == g.app_id) {
                    mem_lib[pos] = g.clone();
                } else {
                    mem_lib.push(g.clone());
                }
            }
            let cols = cache.load_collections_from_db();
            apply_membership(&mut mem_lib, &cols);
            let _ = cache.set_kv_json("library:default", &mem_lib, None);
        }
    }

    Ok(ImportResult {
        added: added_games,
        skipped,
    })
}

/// Set or clear the platform attribution for a game (usually a non-Steam shortcut).
#[tauri::command]
pub fn set_game_platform(app_id: u32, platform: Option<String>) -> AppResult<()> {
    let target = match platform {
        Some(ref id) if !id.is_empty() && id != "none" => {
            let p = Platform::from_id(id)
                .ok_or_else(|| AppError::Invalid(format!("unknown platform '{id}'")))?;
            Some(p)
        }
        _ => None,
    };
    platforms::set_override(app_id, target)
}

/// Return the cached owned Steam library (may be empty before a refresh).
#[tauri::command]
pub fn get_owned_library() -> OwnedLibrary {
    owned::load()
}

/// Fetch the user's **entire** owned Steam library via the Steam Web API and
/// cache it, so uninstalled games can be organized too.
#[tauri::command]
pub async fn refresh_owned_games(state: State<'_, AppState>) -> AppResult<OwnedLibrary> {
    let settings = state.settings_snapshot();
    let key = settings
        .steam_api_key
        .filter(|value| !value.is_empty())
        .ok_or_else(|| AppError::msg("No Steam Web API key configured."))?;
    let steamid64 = settings
        .account_id3
        .as_deref()
        .and_then(paths::id3_to_steamid64)
        .map(|value| value.to_string())
        .ok_or_else(|| AppError::msg("No SteamID64 available for the selected account."))?;

    let client = metadata::http_client();
    let owned_games = steam_web::get_owned_games(&client, &key, &steamid64, true).await?;

    let library = OwnedLibrary {
        fetched_at: Some(chrono::Utc::now().timestamp()),
        steam_id64: Some(steamid64),
        games: owned_games
            .into_iter()
            .map(|game| OwnedGameRecord {
                app_id: game.appid,
                name: game.name.unwrap_or_else(|| format!("App {}", game.appid)),
                playtime_minutes: game.playtime_forever.unwrap_or(0),
                playtime_two_weeks: game.playtime_2weeks.unwrap_or(0),
                icon_url: game
                    .img_icon_url
                    .map(|hash| owned::icon_url(game.appid, &hash)),
            })
            .collect(),
    };
    owned::save(&library)?;
    Ok(library)
}

// ---------------------------------------------------------------------------
// Storage report (Read-only drive footprint analysis)
// ---------------------------------------------------------------------------

/// Inspect registered library folders, their games, and mounted disks (read-only).
/// Offloads the filesystem scan to a background thread pool so IPC never hangs.
#[tauri::command]
pub async fn get_storage_report(
    account_id3: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<StorageReport> {
    let root = steam_root(&state)?;
    let account = resolve_account(&root, &state, account_id3);
    tauri::async_runtime::spawn_blocking(move || {
        crate::steam::storage::report(&root, account.as_deref())
    })
    .await
    .map_err(|e| AppError::Io(std::io::Error::new(std::io::ErrorKind::Other, e.to_string())))?
}

// ---------------------------------------------------------------------------
// Dedicated Plugin Engine (Self-contained drop-in & GitHub hosted plugins)
// ---------------------------------------------------------------------------

#[tauri::command]
pub fn get_installed_plugins(state: State<'_, AppState>) -> Vec<crate::plugins::PluginInfo> {
    let settings = state.settings_snapshot();
    let plugins = state.plugins.lock().expect("plugins lock poisoned");
    plugins.list_plugins(&settings.enabled_plugins)
}

#[tauri::command]
pub fn toggle_plugin(id: String, enabled: bool, state: State<'_, AppState>) -> AppResult<()> {
    let mut settings = state.settings.lock().expect("settings lock poisoned");
    settings.enabled_plugins.insert(id, enabled);
    crate::settings::save(&settings)?;
    Ok(())
}

#[tauri::command]
pub async fn scan_all_platforms(
    include_other_drives: Option<bool>,
    state: State<'_, AppState>,
) -> AppResult<crate::platforms::PlatformScan> {
    let settings = state.settings_snapshot();
    let config = crate::platforms::ScanConfig {
        sources: settings.platform_sources,
        include_other_drives: include_other_drives.unwrap_or(true),
    };
    let enabled_plugins = settings.enabled_plugins;
    let plugins_engine = state.plugins.lock().expect("plugins lock poisoned").clone();

    tauri::async_runtime::spawn_blocking(move || {
        plugins_engine.scan_platforms(&config, &enabled_plugins)
    })
    .await
    .map_err(|e| AppError::Io(std::io::Error::new(std::io::ErrorKind::Other, e.to_string())))
}

/// Statistics on the persistent SQLite metadata database, artwork cache, and serverless Redis KV layer.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CacheStats {
    pub total_metadata_cached: usize,
    pub total_cached_artworks: usize,
    pub total_kv_entries: usize,
}

#[tauri::command]
pub fn get_cache_stats(state: State<'_, AppState>) -> CacheStats {
    let cache = state.cache.lock().expect("cache lock poisoned");
    CacheStats {
        total_metadata_cached: cache.count(),
        total_cached_artworks: cache.artwork_count(),
        total_kv_entries: cache.keys_kv(None).len(),
    }
}

/// Clear all metadata, artwork, and key-value caches.
/// Optionally triggers an immediate library metadata and artwork re-synchronization.
#[tauri::command]
pub async fn clear_metadata_cache(
    account_id3: Option<String>,
    redownload: Option<bool>,
    state: State<'_, AppState>,
) -> AppResult<crate::cache::CacheResetReport> {
    let mut report = {
        let mut cache = state.cache.lock().expect("cache lock poisoned");
        cache.clear_all_caches()?
    };

    if redownload.unwrap_or(true) {
        // Refresh library metadata and artwork
        let _ = sync_library_metadata(account_id3, Some(true), Some(true), state.clone()).await;

        let cache = state.cache.lock().expect("cache lock poisoned");
        report.total_metadata_records = cache.count();
        report.message = format!(
            "Cache reset completed. Purged all stale entries and cached {} fresh metadata records.",
            report.total_metadata_records
        );
    }

    Ok(report)
}

/// Retrieve a cached artwork data URL for a specific app ID and slot.
#[tauri::command]
pub fn get_cached_artwork(
    app_id: u32,
    slot: String,
    state: State<'_, AppState>,
) -> Option<String> {
    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.get_cached_artwork(app_id, &slot)
}

/// Batch retrieve cached artwork data URLs for all requested game app IDs.
#[tauri::command]
pub fn get_cached_artwork_batch(
    app_ids: Vec<u32>,
    slot: Option<String>,
    state: State<'_, AppState>,
) -> BTreeMap<u32, String> {
    let slot_key = slot.unwrap_or_else(|| "gridPortrait".to_string());
    let cache = state.cache.lock().expect("cache lock poisoned");
    let mut map = BTreeMap::new();
    for app_id in app_ids {
        if let Some(data_url) = cache.get_cached_artwork(app_id, &slot_key) {
            map.insert(app_id, data_url);
        }
    }
    map
}

/// Download an artwork image from a remote URL into the local disk cache.
#[tauri::command]
pub async fn cache_artwork_image(
    app_id: u32,
    slot: String,
    url: String,
    state: State<'_, AppState>,
) -> AppResult<Option<String>> {
    let trimmed = url.trim();
    if trimmed.is_empty() {
        return Ok(None);
    }

    // Check if already in cache
    {
        let cache = state.cache.lock().expect("cache lock poisoned");
        if let Some(existing) = cache.get_cached_artwork(app_id, &slot) {
            return Ok(Some(existing));
        }
    }

    let client = metadata::http_client();
    let res = client.get(trimmed).send().await?;
    if !res.status().is_success() {
        return Ok(None);
    }
    let bytes = res.bytes().await?;
    let ext = if trimmed.ends_with(".png") {
        "png"
    } else if trimmed.ends_with(".webp") {
        "webp"
    } else if trimmed.ends_with(".ico") {
        "ico"
    } else {
        "jpg"
    };

    let cache = state.cache.lock().expect("cache lock poisoned");
    let data_url = cache.cache_artwork_bytes(app_id, &slot, &bytes, ext, Some(trimmed))?;
    Ok(Some(data_url))
}



// ---------------------------------------------------------------------------
// Game launching & local actions
// ---------------------------------------------------------------------------

fn find_main_executable_in_dir(dir: &Path) -> Option<PathBuf> {
    if !dir.is_dir() {
        return None;
    }

    let mut candidate_dirs = vec![dir.to_path_buf()];
    for sub in &["bin", "Binaries/Win64", "bin/x64", "bin/win64", "bin/x86"] {
        let sub_path = dir.join(sub);
        if sub_path.is_dir() {
            candidate_dirs.push(sub_path);
        }
    }

    let mut candidates: Vec<PathBuf> = Vec::new();

    for check_dir in candidate_dirs {
        if let Ok(entries) = std::fs::read_dir(&check_dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_file() {
                    #[cfg(windows)]
                    let is_exec = path
                        .extension()
                        .and_then(|s| s.to_str())
                        .map(|ext| ext.eq_ignore_ascii_case("exe"))
                        .unwrap_or(false);

                    #[cfg(not(windows))]
                    let is_exec = {
                        use std::os::unix::fs::PermissionsExt;
                        entry
                            .metadata()
                            .map(|m| m.permissions().mode() & 0o111 != 0)
                            .unwrap_or(false)
                    };

                    if is_exec {
                        let name = path
                            .file_name()
                            .and_then(|n| n.to_str())
                            .unwrap_or("")
                            .to_lowercase();
                        let is_unwanted = name.starts_with("unins")
                            || name.starts_with("setup")
                            || name.starts_with("dxsetup")
                            || name.starts_with("vcredist")
                            || name.contains("crashhandler")
                            || name.contains("crashreporter")
                            || name.contains("easyanticheat");
                        if !is_unwanted {
                            candidates.push(path);
                        }
                    }
                }
            }
        }
    }

    candidates.sort_by_key(|p| p.metadata().map(|m| m.len()).unwrap_or(0));
    candidates.pop()
}

#[tauri::command]
pub fn launch_game(
    app_id: u32,
    launch_uri: Option<String>,
    exe: Option<String>,
    platform: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<bool> {
    let cleaned_exe = exe.as_deref().and_then(|raw| {
        let trimmed = raw.trim().trim_matches('"').trim();
        if trimmed.is_empty() {
            None
        } else {
            Some(trimmed.to_string())
        }
    });

    let is_wrapper = cleaned_exe.as_deref().map_or(false, |p| {
        let p_lower = p.to_lowercase();
        p_lower.ends_with("explorer.exe")
            || p_lower.ends_with("cmd.exe")
            || p_lower.ends_with("legendary.exe")
            || p_lower.ends_with("legendary")
    });

    // Platforms whose games MUST be launched through their store/launcher protocol.
    // itch.io and GOG (and standalone/local titles) may be launched directly.
    let requires_store_launch = matches!(
        platform.as_deref().map(|p| p.to_lowercase()).as_deref(),
        Some("steam")
            | Some("epic")
            | Some("battlenet")
            | Some("ubisoft")
            | Some("ea")
            | Some("rockstar")
            | Some("amazon")
            | Some("xbox")
    );

    // Direct executable launch is only attempted FIRST for titles that do not
    // require a store launcher (standalone, itch.io, GOG). Platform-distributed
    // games must go through their launcher so DRM/overlay/account injection works.
    if !requires_store_launch {
        if let Some(ref path_str) = cleaned_exe {
            if !is_wrapper {
                let p = Path::new(path_str);
                if p.is_file() {
                    let mut cmd = std::process::Command::new(p);
                    if let Some(parent) = p.parent() {
                        cmd.current_dir(parent);
                    }
                    if cmd.spawn().is_ok() {
                        return Ok(true);
                    }
                }
            }
        }

        // Direct launch for installed Steam games is not applicable here (Steam
        // requires store launch), but keep the binary scan as a fallback for
        // local titles without an exe path.
        if app_id > 0 && app_id < 0x80000000 && cleaned_exe.is_none() && !requires_store_launch {
            if let Ok(root) = steam_root(&state) {
                let libraries = crate::steam::paths::read_library_folders(&root);
                for lib in &libraries {
                    let manifest_path = Path::new(&lib.path)
                        .join("steamapps")
                        .join(format!("appmanifest_{app_id}.acf"));
                    if manifest_path.is_file() {
                        if let Ok(text) = std::fs::read_to_string(&manifest_path) {
                            if let Ok(doc) = crate::steam::vdf::parse(&text) {
                                if let Some(state_node) = doc.get("AppState") {
                                    if let Some(installdir) = state_node.get_str("installdir") {
                                        let game_dir = Path::new(&lib.path)
                                            .join("steamapps")
                                            .join("common")
                                            .join(installdir);
                                        if let Some(direct_exe) = find_main_executable_in_dir(&game_dir) {
                                            let mut cmd = std::process::Command::new(&direct_exe);
                                            if let Some(parent) = direct_exe.parent() {
                                                cmd.current_dir(parent);
                                            }
                                            if cmd.spawn().is_ok() {
                                                return Ok(true);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. Epic Games via Official Epic Games Launcher protocol
    let is_epic = platform.as_deref() == Some("epic")
        || launch_uri.as_ref().map_or(false, |u| {
            u.starts_with("com.epicgames.launcher://apps/") || u.starts_with("legendary://")
        });

    if is_epic {
        let app_name = launch_uri.as_deref().and_then(|u| {
            if u.starts_with("com.epicgames.launcher://apps/") {
                let after = &u["com.epicgames.launcher://apps/".len()..];
                Some(after.split('?').next().unwrap_or(after))
            } else if u.starts_with("legendary://") {
                Some(&u["legendary://".len()..])
            } else {
                None
            }
        });

        if let Some(name) = app_name {
            let epic_uri = format!("com.epicgames.launcher://apps/{name}?action=launch");
            if open_external_target(&epic_uri).is_ok() {
                return Ok(true);
            }
        }
    }

    // 3. itch.io launcher support (handles itch-setup.exe with --prefer-launch --appname itch)
    let is_itch = platform.as_deref() == Some("itch")
        || launch_uri.as_ref().map_or(false, |u| u.starts_with("itch://"));
    if is_itch {
        if let Some(itch_bin) = crate::platforms::check_binary_installed(
            &["itch-setup.exe".into(), "itch.exe".into(), "itch".into()],
            None,
        ) {
            let mut cmd = std::process::Command::new(&itch_bin);
            if itch_bin.to_lowercase().ends_with("itch-setup.exe") {
                cmd.args(["--prefer-launch", "--appname", "itch"]);
            }
            if let Some(ref uri) = launch_uri {
                if uri.starts_with("itch://") {
                    cmd.arg(uri);
                }
            }
            if cmd.spawn().is_ok() {
                return Ok(true);
            }
        }
    }

    // 4. Storefront official launcher URI (e.g. goggalaxy://, uplay://, battlenet://, shell:AppsFolder)
    if let Some(uri) = launch_uri.filter(|u| !u.is_empty()) {
        if open_external_target(&uri).is_ok() {
            return Ok(true);
        }
    }

    // 5. Steam launch for native Steam games. Invoke Steam's own executable so the
    //    account/session is passed to the game (cmd /c start steam://... bypasses
    //    Steam's injection and breaks titles like RuneScape/Jagex). Fall back to
    //    the protocol handler only if the executable cannot be located.
    if app_id > 0 && app_id < 0x80000000 {
        if let Ok(root) = steam_root(&state) {
            #[cfg(target_os = "windows")]
            let steam_exe = root.join("steam.exe");
            #[cfg(target_os = "macos")]
            let steam_exe = root.join("steam");
            #[cfg(target_os = "linux")]
            let steam_exe = root.join("steam.sh");
            if steam_exe.is_file() {
                let mut cmd = std::process::Command::new(&steam_exe);
                cmd.arg(format!("steam://run/{app_id}"));
                if cmd.spawn().is_ok() {
                    return Ok(true);
                }
            }
        }
        let uri = format!("steam://run/{app_id}");
        if open_external_target(&uri).is_ok() {
            return Ok(true);
        }
    }

    // 6. Last-resort direct executable launch for platform games whose store
    //    protocol did not resolve (e.g. launcher not installed). Only attempt
    //    when a concrete executable is known on disk.
    if requires_store_launch {
        if let Some(ref path_str) = cleaned_exe {
            if !is_wrapper {
                let p = Path::new(path_str);
                if p.is_file() {
                    let mut cmd = std::process::Command::new(p);
                    if let Some(parent) = p.parent() {
                        cmd.current_dir(parent);
                    }
                    if cmd.spawn().is_ok() {
                        return Ok(true);
                    }
                }
            }
        }
    }

    Err(AppError::msg("No valid launch method found for game."))
}

/// Retrieve locally cached game metadata without making any network requests.
#[tauri::command]
pub fn get_cached_game_metadata(
    app_id: u32,
    _name: Option<String>,
    _exe: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<Option<AppMeta>> {
    let cache = state.cache.lock().expect("cache lock poisoned");

    // 1. Check persistent KV store
    if let Some(meta) = cache
        .get_kv_json::<AppMeta>(&format!("game_meta:{app_id}"))
        .filter(|m| !is_placeholder_meta(m))
    {
        return Ok(Some(meta));
    }

    // 2. Check shortcut-meta
    if let Some(mut meta) = cache
        .get("shortcut-meta", app_id, None)
        .filter(|m| !is_placeholder_meta(m))
    {
        if let Some(tags) = cache.get("steam-tags", app_id, None) {
            meta.tags = tags.tags;
        }
        return Ok(Some(meta));
    }

    // 3. Check steam-store
    if let Some(mut meta) = cache
        .get("steam-store", app_id, None)
        .filter(|m| !is_placeholder_meta(m))
    {
        if let Some(tags) = cache.get("steam-tags", app_id, None) {
            meta.tags = tags.tags;
        }
        return Ok(Some(meta));
    }

    // 4. Primary internal fallback: Check developer hand-rolled metadata database
    if let Some(ref name) = _name {
        if let Some(mut meta) = crate::metadata::developers::backup_metadata_for_title(name) {
            meta.app_id = app_id;
            meta.user_enhanced = Some(true);
            if meta.enhanced_by.is_empty() {
                meta.enhanced_by = vec!["Developer Hand-Rolled Database".to_string()];
            }
            let _ = cache.set_kv_json(&format!("game_meta:{app_id}"), &meta, None);
            return Ok(Some(meta));
        }
    }

    Ok(None)
}

#[tauri::command]
pub fn install_game(
    app_id: u32,
    install_uri: Option<String>,
) -> AppResult<bool> {
    if let Some(uri) = install_uri.filter(|u| !u.is_empty()) {
        open_external_target(&uri)?;
        return Ok(true);
    }

    if app_id > 0 {
        let uri = format!("steam://install/{app_id}");
        open_external_target(&uri)?;
        return Ok(true);
    }

    Err(AppError::msg("No installation URI available for game."))
}

#[tauri::command]
pub fn uninstall_game(
    app_id: u32,
    uninstall_uri: Option<String>,
) -> AppResult<bool> {
    if let Some(uri) = uninstall_uri.filter(|u| !u.is_empty()) {
        open_external_target(&uri)?;
        return Ok(true);
    }

    if app_id > 0 {
        let uri = format!("steam://uninstall/{app_id}");
        open_external_target(&uri)?;
        return Ok(true);
    }

    Err(AppError::msg("No uninstallation URI available for game."))
}

#[tauri::command]
pub fn open_game_folder(path: String) -> AppResult<bool> {
    let p = Path::new(&path);
    if !p.exists() {
        return Err(AppError::NotFound(format!("Path does not exist: {path}")));
    }
    open_external_target(&path)?;
    Ok(true)
}

fn open_external_target(target: &str) -> AppResult<()> {
    #[cfg(target_os = "windows")]
    {
        let status = if Path::new(target).exists() {
            std::process::Command::new("explorer")
                .arg(target)
                .spawn()
        } else {
            std::process::Command::new("cmd")
                .args(["/c", "start", "", target])
                .spawn()
        };
        status.map_err(|e| AppError::msg(format!("Failed to open {target}: {e}")))?;
    }

    #[cfg(target_os = "macos")]
    {
        std::process::Command::new("open")
            .arg(target)
            .spawn()
            .map_err(|e| AppError::msg(format!("Failed to open {target}: {e}")))?;
    }

    #[cfg(target_os = "linux")]
    {
        std::process::Command::new("xdg-open")
            .arg(target)
            .spawn()
            .map_err(|e| AppError::msg(format!("Failed to open {target}: {e}")))?;
    }

    Ok(())
}

/// Detect the installed status and download URLs of all external platform launchers.
#[tauri::command]
pub async fn get_installed_launchers() -> AppResult<Vec<crate::platforms::LauncherStatus>> {
    tauri::async_runtime::spawn_blocking(crate::platforms::detect_all_launchers)
        .await
        .map_err(|e| AppError::Io(std::io::Error::new(std::io::ErrorKind::Other, e.to_string())))
}

/// Autodetect binary from PATH, custom path override, or managed directories.
#[tauri::command]
pub async fn check_binary_installed(
    binaries: Vec<String>,
    custom_path: Option<String>,
) -> Option<String> {
    tauri::async_runtime::spawn_blocking(move || {
        crate::platforms::check_binary_installed(&binaries, custom_path.as_deref())
    })
    .await
    .ok()
    .flatten()
}

/// Silently install a package via winget into a user-manageable folder.
#[tauri::command]
pub fn install_winget_package(
    package_id: String,
    location: Option<String>,
) -> AppResult<bool> {
    crate::platforms::install_winget_package(&package_id, location.as_deref())
}

/// Checks if winget is installed and available on the host PATH.
#[tauri::command]
pub fn is_winget_available() -> bool {
    crate::platforms::is_winget_available()
}

/// Retrieve user-manageable directories for tools and launchers.
#[tauri::command]
pub fn get_managed_directories() -> crate::platforms::ManagedDirectories {
    crate::platforms::ManagedDirectories {
        tools_dir: crate::platforms::managed_tools_dir().display().to_string(),
        launchers_dir: crate::platforms::managed_launchers_dir().display().to_string(),
        winget_available: crate::platforms::is_winget_available(),
    }
}

/// Update user-configured custom directories for tools and launchers.
#[tauri::command]
pub fn set_managed_directories(
    tools_dir: Option<String>,
    launchers_dir: Option<String>,
) -> AppResult<bool> {
    let mut current = crate::settings::load();
    current.custom_tools_dir = tools_dir.map(|s| s.trim().to_string()).filter(|s| !s.is_empty());
    current.custom_launchers_dir = launchers_dir.map(|s| s.trim().to_string()).filter(|s| !s.is_empty());
    crate::settings::save(&current)?;
    Ok(true)
}

/// Open the user-accessible launchers directory in Explorer/Finder/File Manager.
#[tauri::command]
pub fn open_launchers_folder() -> AppResult<bool> {
    let dir = crate::platforms::managed_launchers_dir();
    let _ = std::fs::create_dir_all(&dir);
    open_external_target(&dir.display().to_string())?;
    Ok(true)
}

/// Download and install the built-in standalone Legendary CLI and configure EOS service.
#[tauri::command]
pub async fn install_legendary() -> AppResult<crate::platforms::LauncherStatus> {
    crate::platforms::install_legendary_cli().await
}

/// Open an external URL in the user's default web browser.
#[tauri::command]
pub fn open_external_url(url: String) -> AppResult<bool> {
    open_external_target(&url)?;
    Ok(true)
}

/// Open the user-accessible tools directory in Explorer/Finder/File Manager.
#[tauri::command]
pub fn open_tools_folder() -> AppResult<bool> {
    let dir = crate::platforms::managed_tools_dir();
    let _ = std::fs::create_dir_all(&dir);
    open_external_target(&dir.display().to_string())?;
    Ok(true)
}

/// Query current Legendary Epic Games authentication status.
#[tauri::command]
pub fn get_legendary_status() -> crate::platforms::LegendaryAuthStatus {
    crate::platforms::get_legendary_auth_status()
}

/// Authenticate with Epic Games via Legendary (using browser authorization code or import from launcher).
#[tauri::command]
pub fn legendary_login(
    code: Option<String>,
    import_launcher: Option<bool>,
) -> AppResult<crate::platforms::LegendaryAuthStatus> {
    crate::platforms::legendary_authenticate(code, import_launcher.unwrap_or(false))
}

/// Log out from Epic Games in Legendary.
#[tauri::command]
pub fn legendary_logout() -> AppResult<crate::platforms::LegendaryAuthStatus> {
    crate::platforms::legendary_logout()
}

const CHROME_USER_AGENT: &str = if cfg!(target_os = "windows") {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
} else if cfg!(target_os = "macos") {
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
} else {
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
};

/// Open a native Epic Games authentication window using Chrome User Agent and automatic code interception.
#[tauri::command]
pub fn open_epic_login_window(app: tauri::AppHandle) -> AppResult<bool> {
    use tauri::Manager;

    if let Some(existing) = app.get_webview_window("epic-auth") {
        let _ = existing.set_focus();
        return Ok(true);
    }

    let auth_status = crate::platforms::get_legendary_auth_status();
    let login_url = auth_status.direct_auth_url;

    let app_handle = app.clone();
    let win_builder = tauri::WebviewWindowBuilder::new(
        &app,
        "epic-auth",
        tauri::WebviewUrl::External(
            login_url
                .parse()
                .map_err(|e| AppError::msg(format!("Invalid auth URL: {e}")))?,
        ),
    )
    .title("Sign in to Epic Games")
    .inner_size(840.0, 720.0)
    .center()
    .user_agent(CHROME_USER_AGENT)
    .on_navigation({
        let app_clone = app_handle.clone();
        move |url| {
            // 1. Intercept internal callback
            if url.host_str() == Some("legendary-callback.local") {
                if let Some(code) = url
                    .query_pairs()
                    .find(|(k, _)| k == "code")
                    .map(|(_, v)| v.to_string())
                {
                    let app_inner = app_clone.clone();
                    tauri::async_runtime::spawn(async move {
                        if let Ok(status) =
                            crate::platforms::legendary_authenticate(Some(code), false)
                        {
                            use tauri::Emitter;
                            let _ = app_inner.emit("legendary-auth-changed", status);
                        }
                        if let Some(w) = app_inner.get_webview_window("epic-auth") {
                            let _ = w.close();
                        }
                    });
                }
                return false;
            }

            // 2. Intercept code directly if redirect URL matches
            if let Some(code) = url
                .query_pairs()
                .find(|(k, _)| k == "authorizationCode" || k == "code")
                .map(|(_, v)| v.to_string())
            {
                if code.len() >= 16 {
                    let app_inner = app_clone.clone();
                    tauri::async_runtime::spawn(async move {
                        if let Ok(status) =
                            crate::platforms::legendary_authenticate(Some(code), false)
                        {
                            use tauri::Emitter;
                            let _ = app_inner.emit("legendary-auth-changed", status);
                        }
                        if let Some(w) = app_inner.get_webview_window("epic-auth") {
                            let _ = w.close();
                        }
                    });
                }
            }

            true
        }
    })
    .on_page_load(|window, payload| {
        if matches!(payload.event(), tauri::webview::PageLoadEvent::Finished) {
            let script = r#"
                (function() {
                    function extractCode() {
                        try {
                            let code = null;
                            const url = new URL(window.location.href);
                            code = url.searchParams.get("authorizationCode") || url.searchParams.get("code");
                            if (!code && window.location.hash) {
                                const hash = new URLSearchParams(window.location.hash.substring(1));
                                code = hash.get("authorizationCode") || hash.get("code");
                            }
                            if (!code && document.body) {
                                const text = document.body.innerText || document.body.textContent || "";
                                if (text.includes("authorizationCode")) {
                                    try {
                                        const json = JSON.parse(text);
                                        if (json.authorizationCode) code = json.authorizationCode;
                                    } catch(e) {
                                        const m = text.match(/"authorizationCode"\s*:\s*"([a-f0-9]+)"/i);
                                        if (m) code = m[1];
                                    }
                                }
                            }
                            if (code && code.length >= 16) {
                                window.location.href = "https://legendary-callback.local/?code=" + encodeURIComponent(code);
                            }
                        } catch(e) {}
                    }
                    extractCode();
                    setTimeout(extractCode, 300);
                    setTimeout(extractCode, 1000);
                    setTimeout(extractCode, 2500);
                })();
            "#;
            let _ = window.eval(script);
        }
    });

    win_builder
        .build()
        .map_err(|e| AppError::msg(format!("Failed to open Epic Games auth window: {e}")))?;

    Ok(true)
}

/// Query current Epic Games overlay service (EOS) status via Legendary.
#[tauri::command]
pub fn get_eos_overlay_status() -> crate::platforms::EosOverlayStatus {
    crate::platforms::get_eos_overlay_status()
}

/// Download and install Epic Games overlay service (EOS) via Legendary.
#[tauri::command]
pub fn install_eos_overlay() -> AppResult<crate::platforms::EosOverlayStatus> {
    crate::platforms::install_eos_overlay()
}

pub use crate::platforms::{FeaturedStoreItem, FreeGamePromotion, PatchNote, StoreDeal};

/// Fetch current 100% free games and giveaways from enabled storefront platforms (Steam, Epic, GOG, etc.).
#[tauri::command]
pub async fn get_free_game_promotions(
    platforms: Option<Vec<String>>,
    state: State<'_, AppState>,
) -> AppResult<Vec<FreeGamePromotion>> {
    let settings = state.settings.lock().expect("settings lock poisoned").clone();
    let enabled_platforms: std::collections::HashSet<String> = if let Some(list) = platforms {
        list.into_iter().collect()
    } else {
        settings
            .platform_sources
            .iter()
            .filter(|s| s.enabled)
            .map(|s| s.id.clone())
            .collect()
    };

    let client = crate::metadata::http_client();
    let promotions = crate::platforms::promotions::fetch_all_free_games(&client, &enabled_platforms).await;
    Ok(promotions)
}

/// Fetch highlighted deals, promotions, and discounts across enabled storefront platforms.
#[tauri::command]
pub async fn get_store_deals(
    platforms: Option<Vec<String>>,
    state: State<'_, AppState>,
) -> AppResult<Vec<StoreDeal>> {
    let settings = state.settings.lock().expect("settings lock poisoned").clone();
    let enabled_platforms: std::collections::HashSet<String> = if let Some(list) = platforms {
        list.into_iter().collect()
    } else {
        settings
            .platform_sources
            .iter()
            .filter(|s| s.enabled)
            .map(|s| s.id.clone())
            .collect()
    };

    let client = crate::metadata::http_client();
    let deals = crate::platforms::promotions::fetch_all_deals(&client, &enabled_platforms).await;
    Ok(deals)
}

/// Fetch highlighted featured storefront items across enabled platforms for the Store hero carousel.
#[tauri::command]
pub async fn get_featured_store_items(
    platforms: Option<Vec<String>>,
    state: State<'_, AppState>,
) -> AppResult<Vec<FeaturedStoreItem>> {
    let settings = state.settings.lock().expect("settings lock poisoned").clone();
    let enabled_platforms: std::collections::HashSet<String> = if let Some(list) = platforms {
        list.into_iter().collect()
    } else {
        settings
            .platform_sources
            .iter()
            .filter(|s| s.enabled)
            .map(|s| s.id.clone())
            .collect()
    };

    let client = crate::metadata::http_client();
    let items = crate::platforms::promotions::fetch_featured_items(&client, &enabled_platforms).await;
    Ok(items)
}

/// Fetch patch notes and game update announcements for candidate library games.
#[tauri::command]
pub async fn get_library_patch_notes(
    games: Option<Vec<(String, String)>>,
    state: State<'_, AppState>,
) -> AppResult<Vec<PatchNote>> {
    let candidate_games = if let Some(g) = games {
        g
    } else {
        let root = match steam_root(&state) {
            Ok(r) => r,
            Err(_) => return Ok(Vec::new()),
        };
        if let Ok(games_list) = library::load_games(&root) {
            games_list
                .into_iter()
                .filter(|g| g.installed)
                .take(12)
                .map(|g| (g.app_id.to_string(), g.name))
                .collect()
        } else {
            Vec::new()
        }
    };

    let client = crate::metadata::http_client();
    let notes = crate::platforms::promotions::fetch_library_patch_notes(&client, &candidate_games).await;
    Ok(notes)
}

/// Save custom Legendary CLI path/folder preference and return updated status.
#[tauri::command]
pub fn set_legendary_path(
    path: Option<String>,
    state: State<'_, AppState>,
) -> AppResult<crate::platforms::LegendaryAuthStatus> {
    let clean = path.map(|s| s.trim().to_string()).filter(|s| !s.is_empty());
    let mut current_settings = state.settings.lock().expect("settings lock poisoned").clone();
    current_settings.legendary_path = clean;
    settings::save(&current_settings)?;
    *state.settings.lock().expect("settings lock poisoned") = current_settings;
    Ok(crate::platforms::get_legendary_auth_status())
}

// ---------------------------------------------------------------------------
// Serverless In-Memory KV & Cache Tier (Redis-compatible)
// ---------------------------------------------------------------------------

/// Retrieve a value by key from the in-memory fast tier / SQLite persistent KV store.
#[tauri::command]
pub fn kv_get(key: String, state: State<'_, AppState>) -> Option<String> {
    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.get_kv(&key)
}

/// Store a key-value pair with optional TTL in seconds.
#[tauri::command]
pub fn kv_set(
    key: String,
    value: String,
    ttl_secs: Option<i64>,
    state: State<'_, AppState>,
) -> AppResult<bool> {
    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.set_kv(&key, &value, ttl_secs);
    Ok(true)
}

/// Delete a key from the KV store.
#[tauri::command]
pub fn kv_delete(key: String, state: State<'_, AppState>) -> AppResult<bool> {
    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.delete_kv(&key);
    Ok(true)
}

/// Check if a key exists and has not expired.
#[tauri::command]
pub fn kv_has(key: String, state: State<'_, AppState>) -> bool {
    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.has_kv(&key)
}

/// Query keys matching an optional wildcard/prefix pattern.
#[tauri::command]
pub fn kv_keys(pattern: Option<String>, state: State<'_, AppState>) -> Vec<String> {
    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.keys_kv(pattern.as_deref())
}

/// Multi-get: retrieve multiple key-value pairs in a single call.
#[tauri::command]
pub fn kv_mget(
    keys: Vec<String>,
    state: State<'_, AppState>,
) -> std::collections::HashMap<String, String> {
    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.mget_kv(&keys)
}

/// Multi-set: store multiple key-value pairs in a single transaction.
#[tauri::command]
pub fn kv_mset(
    pairs: Vec<(String, String)>,
    ttl_secs: Option<i64>,
    state: State<'_, AppState>,
) -> AppResult<bool> {
    let mut cache = state.cache.lock().expect("cache lock poisoned");
    cache.mset_kv(&pairs, ttl_secs)?;
    Ok(true)
}

/// Clean up and purge all expired keys from SQLite and in-memory cache.
#[tauri::command]
pub fn kv_clear_expired(state: State<'_, AppState>) -> usize {
    let cache = state.cache.lock().expect("cache lock poisoned");
    cache.cleanup_expired_kv()
}

// ---------------------------------------------------------------------------
// EmulationStation & ROM Directory Scaffolding and Title Normalization
// ---------------------------------------------------------------------------

/// Scaffolds standardized EmulationStation system subdirectories and necessary .txt files
/// inside the user-selected ROM folder.
#[tauri::command]
pub async fn scaffold_emulation_folders(
    roms_dir: String,
) -> AppResult<crate::platforms::ScaffoldReport> {
    tauri::async_runtime::spawn_blocking(move || {
        crate::platforms::scaffold_emulation_folders(&roms_dir)
    })
    .await
    .map_err(|e| AppError::Io(std::io::Error::new(std::io::ErrorKind::Other, e.to_string())))?
}

/// Scans the user-selected ROM directory across all scaffolded console system folders.
#[tauri::command]
pub async fn scan_emulation_roms(
    roms_dir: String,
    normalize_titles: Option<bool>,
) -> AppResult<Vec<crate::platforms::PlatformGame>> {
    let normalize = normalize_titles.unwrap_or(true);
    tauri::async_runtime::spawn_blocking(move || {
        crate::platforms::scan_emulation_roms(&roms_dir, normalize)
    })
    .await
    .map_err(|e| AppError::Io(std::io::Error::new(std::io::ErrorKind::Other, e.to_string())))?
}

/// Normalizes a raw game or ROM title across all platforms.
#[tauri::command]
pub fn normalize_rom_title_cmd(title: String) -> String {
    crate::platforms::normalize_game_title(&title)
}

// ---------------------------------------------------------------------------
// Native File & Directory Pickers for Standalone & Local Games
// ---------------------------------------------------------------------------

/// Opens a native file dialog allowing the user to pick a local game executable.
#[tauri::command]
pub async fn pick_game_executable() -> AppResult<Option<String>> {
    let file = rfd::AsyncFileDialog::new()
        .add_filter("Game & Executable Files", &["exe", "bat", "cmd", "sh", "bin", "app", "jar", "py", "love"])
        .add_filter("All Files", &["*"])
        .set_title("Select Local Game Executable")
        .pick_file()
        .await;

    Ok(file.map(|f| f.path().display().to_string()))
}

/// Opens a native folder dialog allowing the user to pick a game directory.
#[tauri::command]
pub async fn pick_game_directory() -> AppResult<Option<String>> {
    let folder = rfd::AsyncFileDialog::new()
        .set_title("Select Game Installation Directory")
        .pick_folder()
        .await;

    Ok(folder.map(|f| f.path().display().to_string()))
}

/// Opens a native file dialog allowing the user to pick a cover artwork image.
#[tauri::command]
pub async fn pick_game_artwork() -> AppResult<Option<String>> {
    let file = rfd::AsyncFileDialog::new()
        .add_filter("Image Files", &["png", "jpg", "jpeg", "webp", "bmp"])
        .add_filter("All Files", &["*"])
        .set_title("Select Cover Artwork Image")
        .pick_file()
        .await;

    Ok(file.map(|f| f.path().display().to_string()))
}

/// Fetches a JSON payload from an external HTTPS endpoint using the native Rust HTTP client
/// to avoid WebView2 CORS restrictions for in-app plugins.
#[tauri::command]
pub async fn fetch_external_json(
    url: String,
    headers: Option<std::collections::HashMap<String, String>>,
) -> AppResult<serde_json::Value> {
    let trimmed = url.trim().to_string();
    if !trimmed.starts_with("https://") && !trimmed.starts_with("http://") {
        return Err(AppError::msg("Invalid URL scheme; only HTTP/HTTPS is allowed."));
    }
    let client = metadata::http_client();
    let mut req = client.get(&trimmed);
    if let Some(hdrs) = headers {
        for (k, v) in hdrs {
            req = req.header(k, v);
        }
    }
    let resp = req
        .send()
        .await
        .map_err(|e| AppError::msg(format!("HTTP request failed: {e}")))?;
    if !resp.status().is_success() {
        return Err(AppError::msg(format!("HTTP status {}", resp.status())));
    }
    resp.json::<serde_json::Value>()
        .await
        .map_err(|e| AppError::msg(format!("Invalid JSON payload: {e}")))
}




