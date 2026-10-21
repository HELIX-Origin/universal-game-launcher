//! Steam Vault — Tauri v2 backend entry point.

// Parts of the Steam data layer and metadata providers are intentionally exposed
// ahead of the milestones that call them (the UI wires them up incrementally).
#![allow(dead_code)]

mod backup;
mod cache;
pub mod cli;
mod commands;
mod error;
mod metadata;
mod platforms;
mod plugins;
mod settings;
mod steam;

use std::sync::atomic::{AtomicBool, Ordering};

use commands::AppState;
use tauri::Manager;

static IS_QUITTING: AtomicBool = AtomicBool::new(false);

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // Support headless internal CLI mode (`universal-game-launcher --cli <subcommand>`)
    // for isolated worker subprocesses and instant terminal automation without opening WebView2.
    if cli::try_run_cli() {
        return;
    }
    #[cfg(windows)]
    {
        // Suppress spurious Chromium / WebView2 class unregistration warnings and logging noise on Windows
        // (specifically [ERROR:ui\gfx\win\window_impl.cc:172] Failed to unregister class Chrome_WidgetWin_0. Error = 1412)
        if std::env::var("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS").is_err() {
            std::env::set_var(
                "WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                "--disable-logging --log-level=3",
            );
        }
    }

    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_process::init())
        .plugin(tauri_plugin_os::init())
        .plugin(tauri_plugin_shell::init())
        .manage(AppState::new())
        .setup(|app| {
            // Pin the taskbar/window icon to the Steam Vault brand mark so the
            // frameless window never falls back to a generic default icon.
            if let (Some(window), Some(icon)) =
                (app.get_webview_window("main"), app.default_window_icon())
            {
                let _ = window.set_icon(icon.clone());
            }

            #[cfg(desktop)]
            {
                use tauri::menu::{Menu, MenuItem, PredefinedMenuItem};
                use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};

                let show_i = MenuItem::with_id(app, "show", "Show Universal Game Launcher", true, None::<&str>)?;
                let min_i = MenuItem::with_id(app, "minimize", "Minimize to Tray", true, None::<&str>)?;
                let sep = PredefinedMenuItem::separator(app)?;
                let quit_i = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;

                let menu = Menu::with_items(app, &[&show_i, &min_i, &sep, &quit_i])?;

                let mut tray_builder = TrayIconBuilder::with_id("main")
                    .menu(&menu)
                    .show_menu_on_left_click(false)
                    .tooltip("Universal Game Launcher")
                    .on_menu_event(|app, event| match event.id().as_ref() {
                        "show" => {
                            if let Some(window) = app.get_webview_window("main") {
                                let _ = window.show();
                                let _ = window.unminimize();
                                let _ = window.set_focus();
                            }
                        }
                        "minimize" => {
                            if let Some(window) = app.get_webview_window("main") {
                                let _ = window.hide();
                            }
                        }
                        "quit" => {
                            IS_QUITTING.store(true, Ordering::SeqCst);
                            for (_, window) in app.webview_windows() {
                                let _ = window.destroy();
                            }
                            app.exit(0);
                        }
                        _ => {}
                    })
                    .on_tray_icon_event(|tray, event| {
                        if let TrayIconEvent::Click {
                            button: MouseButton::Left,
                            button_state: MouseButtonState::Up,
                            ..
                        } = event
                        {
                            let app = tray.app_handle();
                            if let Some(window) = app.get_webview_window("main") {
                                if window.is_visible().unwrap_or(false) {
                                    let _ = window.hide();
                                } else {
                                    let _ = window.show();
                                    let _ = window.unminimize();
                                    let _ = window.set_focus();
                                }
                            }
                        }
                    });

                let tray_icon =
                    tauri::image::Image::from_bytes(include_bytes!("../icons/tray-icon.png")).ok();
                if let Some(icon) = tray_icon.or_else(|| app.default_window_icon().cloned()) {
                    tray_builder = tray_builder.icon(icon);
                }

                let _ = tray_builder.build(app);
            }

            Ok(())
        })
        .on_window_event(|window, event| {
            #[cfg(desktop)]
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                if window.label() == "main" && !IS_QUITTING.load(Ordering::SeqCst) {
                    api.prevent_close();
                    let _ = window.hide();
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::get_providers,
            commands::detect_steam,
            commands::get_settings,
            commands::save_settings,
            commands::is_steam_running,
            commands::load_library,
            commands::load_collections,
            commands::update_collection,
            commands::create_collection,
            commands::delete_collection,
            commands::fetch_steam_store_metadata,
            commands::sync_library_metadata,
            commands::refresh_game_metadata,
            commands::fetch_owned_games,
            commands::steam_store_search,
            commands::get_installed_plugins,
            commands::toggle_plugin,
            commands::scan_all_platforms,
            commands::detect_platforms,
            commands::find_platform_folders,
            commands::import_platform_games,
            commands::set_game_platform,
            commands::get_owned_library,
            commands::refresh_owned_games,
            commands::get_storage_report,
            commands::get_cache_stats,
            commands::clear_metadata_cache,
            commands::get_cached_game_metadata,
            commands::get_cached_artwork,
            commands::get_cached_artwork_batch,
            commands::cache_artwork_image,
            commands::launch_game,
            commands::install_game,
            commands::uninstall_game,
            commands::open_game_folder,
            commands::get_installed_launchers,
            commands::check_binary_installed,
            commands::install_winget_package,
            commands::is_winget_available,
            commands::get_managed_directories,
            commands::set_managed_directories,
            commands::open_launchers_folder,
            commands::install_legendary,
            commands::open_external_url,
            commands::open_tools_folder,
            commands::get_legendary_status,
            commands::legendary_login,
            commands::legendary_logout,
            commands::set_legendary_path,
            commands::open_epic_login_window,
            commands::get_eos_overlay_status,
            commands::install_eos_overlay,
            commands::get_free_game_promotions,
            commands::get_store_deals,
            commands::get_featured_store_items,
            commands::get_library_patch_notes,
            commands::kv_get,
            commands::kv_set,
            commands::kv_delete,
            commands::kv_has,
            commands::kv_keys,
            commands::kv_mget,
            commands::kv_mset,
            commands::kv_clear_expired,
            commands::scaffold_emulation_folders,
            commands::scan_emulation_roms,
            commands::normalize_rom_title_cmd,
            commands::pick_game_executable,
            commands::pick_game_directory,
            commands::pick_game_artwork,
            commands::fetch_external_json,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
