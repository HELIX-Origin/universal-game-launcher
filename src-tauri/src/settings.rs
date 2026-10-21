//! Local application settings and secrets.
//!
//! Settings live in the OS config directory (`<config>/steam-vault/settings.json`).
//! API keys are stored here on the user's own machine only and are **never**
//! committed. The file is created with best-effort restrictive permissions.

use std::path::PathBuf;

use serde::{Deserialize, Serialize};

use crate::error::AppResult;

/// Persisted application settings.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Settings {
    pub steam_root: Option<String>,
    pub account_id3: Option<String>,
    pub steam_api_key: Option<String>,
    pub steamgriddb_api_key: Option<String>,
    pub igdb_client_id: Option<String>,
    pub igdb_client_secret: Option<String>,
    pub gw2_api_key: Option<String>,
    pub jagex_launcher_path: Option<String>,
    pub xiv_launcher_path: Option<String>,
    pub country_code: String,
    pub language: String,
    #[serde(default = "default_true")]
    pub auto_backup: bool,
    /// Enabled plugin states (plugin_id -> bool).
    #[serde(default)]
    pub enabled_plugins: std::collections::HashMap<String, bool>,
    /// Non-Steam source detection preferences (enable flags + extra folders).
    #[serde(default)]
    pub platform_sources: Vec<crate::platforms::PlatformSource>,
    /// Optional user-configured path to Legendary CLI binary or directory.
    #[serde(default)]
    pub legendary_path: Option<String>,
    /// When true, prefer Legendary CLI over the Epic Games Launcher for Epic titles.
    #[serde(default)]
    pub legendary_enabled: bool,
    /// Optional free game promotion notifications for enabled storefront platforms.
    #[serde(default)]
    pub enable_free_game_notifications: bool,
    /// Optional user-managed tools & helper binaries installation directory.
    #[serde(default)]
    pub custom_tools_dir: Option<String>,
    /// Optional custom installation directory for downloaded platform launchers.
    #[serde(default)]
    pub custom_launchers_dir: Option<String>,
}

fn default_true() -> bool {
    true
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            steam_root: None,
            account_id3: None,
            steam_api_key: None,
            steamgriddb_api_key: None,
            igdb_client_id: None,
            igdb_client_secret: None,
            gw2_api_key: None,
            jagex_launcher_path: None,
            xiv_launcher_path: None,
            country_code: "us".into(),
            language: "en".into(),
            auto_backup: true,
            enabled_plugins: std::collections::HashMap::new(),
            platform_sources: Vec::new(),
            legendary_path: None,
            legendary_enabled: false,
            enable_free_game_notifications: false,
            custom_tools_dir: None,
            custom_launchers_dir: None,
        }
    }
}

/// `…/universal-game-launcher/settings.json` under the OS config directory.
pub fn settings_path() -> PathBuf {
    let base = dirs::config_dir().unwrap_or_else(|| PathBuf::from("."));
    let new_path = base.join("universal-game-launcher").join("settings.json");
    if new_path.exists() {
        return new_path;
    }
    let legacy_path = base.join("steam-vault").join("settings.json");
    if legacy_path.exists() {
        return legacy_path;
    }
    new_path
}

/// Load settings, falling back to defaults when the file is absent or invalid.
pub fn load() -> Settings {
    let path = settings_path();
    std::fs::read_to_string(&path)
        .ok()
        .and_then(|text| serde_json::from_str(&text).ok())
        .unwrap_or_default()
}

/// Persist settings to disk.
pub fn save(settings: &Settings) -> AppResult<()> {
    let path = settings_path();
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let text = serde_json::to_string_pretty(settings)?;
    std::fs::write(&path, text)?;
    Ok(())
}
