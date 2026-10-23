//! Internal Headless CLI Engine (`--cli`).
//!
//! Allows the `universal-game-launcher` binary to execute background tasks,
//! platform scans, ROM archive extraction & `rom.ini` indexing, title normalization,
//! and metadata synchronization directly from the command line or as an isolated
//! worker process outputting clean JSON.
//!
//! Supported subcommands:
//! - `universal-game-launcher --cli scan [--other-drives]`
//! - `universal-game-launcher --cli scan-roms [path]`
//! - `universal-game-launcher --cli scaffold-roms <path>`
//! - `universal-game-launcher --cli normalize-title "<title>"`
//! - `universal-game-launcher --cli inspect-local "<exe_or_dir_path>"`
//! - `universal-game-launcher --cli launchers`
//! - `universal-game-launcher --cli bench [--games N] [--runs R]`
//! - `universal-game-launcher --cli help`

use std::path::Path;

use crate::bench;
use crate::metadata;
use crate::platforms::{
    detect_all_launchers, normalize_game_title, scaffold_emulation_folders, scan_emulation_roms,
    ScanConfig,
};
use crate::plugins::PluginEngine;
use crate::settings;

/// Checks command-line arguments and, if `--cli` is present, executes the requested
/// headless operation, prints JSON to stdout, and returns `true` so `main` exits immediately
/// without spinning up WebView2 or the GUI window.
pub fn try_run_cli() -> bool {
    let args: Vec<String> = std::env::args().collect();
    let Some(cli_idx) = args.iter().position(|a| a == "--cli") else {
        return false;
    };

    #[cfg(windows)]
    {
        // Attach to parent console when invoked from a terminal on Windows
        unsafe {
            extern "system" {
                fn AttachConsole(dwProcessId: u32) -> i32;
            }
            let _ = AttachConsole(u32::MAX); // ATTACH_PARENT_PROCESS
        }
    }

    let sub_args = &args[cli_idx + 1..];
    let cmd = sub_args.first().map(|s| s.as_str()).unwrap_or("help");

    match cmd {
        "scan" => {
            let include_other_drives = !sub_args.iter().any(|a| a == "--no-other-drives");
            let loaded_settings = settings::load();
            let config = ScanConfig {
                sources: loaded_settings.platform_sources,
                include_other_drives,
            };
            let engine = PluginEngine::init();
            let result = engine.scan_platforms(&config, &loaded_settings.enabled_plugins);
            if let Ok(json) = serde_json::to_string_pretty(&result) {
                println!("{json}");
            }
        }
        "scan-roms" => {
            let path = sub_args.get(1).map(|s| s.as_str()).unwrap_or("");
            match scan_emulation_roms(path, true) {
                Ok(roms) => {
                    if let Ok(json) = serde_json::to_string_pretty(&roms) {
                        println!("{json}");
                    }
                }
                Err(e) => {
                    eprintln!("{{\"error\": {:?}}}", e.to_string());
                    std::process::exit(1);
                }
            }
        }
        "scaffold-roms" => {
            let Some(path) = sub_args.get(1) else {
                eprintln!("{{\"error\": \"Missing ROM directory path for scaffold-roms\"}}");
                std::process::exit(1);
            };
            match scaffold_emulation_folders(path) {
                Ok(report) => {
                    if let Ok(json) = serde_json::to_string_pretty(&report) {
                        println!("{json}");
                    }
                }
                Err(e) => {
                    eprintln!("{{\"error\": {:?}}}", e.to_string());
                    std::process::exit(1);
                }
            }
        }
        "normalize-title" => {
            let raw = sub_args.get(1).map(|s| s.as_str()).unwrap_or("");
            let normalized = normalize_game_title(raw);
            println!(
                "{}",
                serde_json::json!({
                    "raw": raw,
                    "normalized": normalized,
                })
            );
        }
        "inspect-local" => {
            let target = sub_args.get(1).map(|s| s.as_str()).unwrap_or(".");
            let p = Path::new(target);
            let (dir, exe) = if p.is_file() {
                (p.parent(), Some(p))
            } else {
                (Some(p), None)
            };
            let meta = metadata::local::inspect_local_game(dir, exe);
            if let Ok(json) = serde_json::to_string_pretty(&meta) {
                println!("{json}");
            }
        }
        "launchers" => {
            let list = detect_all_launchers();
            if let Ok(json) = serde_json::to_string_pretty(&list) {
                println!("{json}");
            }
        }
        "bench" => {
            let flag = |name: &str, default: usize| {
                sub_args
                    .iter()
                    .position(|a| a == name)
                    .and_then(|i| sub_args.get(i + 1))
                    .and_then(|v| v.parse::<usize>().ok())
                    .unwrap_or(default)
            };
            let report = bench::run(
                flag("--games", bench::DEFAULT_GAMES),
                flag("--runs", bench::DEFAULT_RUNS),
            );
            if let Ok(json) = serde_json::to_string_pretty(&report) {
                println!("{json}");
            }
        }
        _ => {
            println!(
                "{}",
                serde_json::json!({
                    "name": "Universal Game Launcher Internal CLI",
                    "usage": "universal-game-launcher --cli <subcommand> [args]",
                    "subcommands": {
                        "scan": "Scan all platforms and ROM directories across drives and output JSON",
                        "scan-roms [path]": "Scan ROM directories, auto-extract archives, create rom.ini & saves/, and output JSON",
                        "scaffold-roms <path>": "Scaffold EmulationStation system folders and systeminfo.txt files",
                        "normalize-title <title>": "Normalize a raw game or ROM title across all platforms",
                        "inspect-local <path>": "Extract local game engine and PE executable metadata without network access",
                        "launchers": "Detect installed platform launchers across all mounted drives",
                        "bench [--games N] [--runs R]": "Benchmark library hot paths on a synthetic library against performance budgets"
                    }
                })
            );
        }
    }

    true
}
