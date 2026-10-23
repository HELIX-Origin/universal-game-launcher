//! Library performance benchmark & synthetic mock library (`--cli bench`).
//!
//! Measures the backend cost of the library hot paths against fixed performance
//! budgets so the v2.0.0 core re-architecture can be tracked with real numbers.
//!
//! - `universal-game-launcher --cli bench [--games N] [--runs R]` prints a JSON report.
//! - Setting `UGL_MOCK_LIBRARY=N` makes `load_library` return `N` synthetic games so
//!   the UI can be profiled against a large library without real storefront data.
//!
//! The generator is deterministic (fixed-seed LCG), so runs are comparable over time.

use std::time::Instant;

use rusqlite::Connection;
use serde::Serialize;

use crate::cache::MetadataCache;
use crate::platforms::Platform;
use crate::steam::models::{Game, GameKind};

/// Default synthetic library size used by the benchmark.
pub const DEFAULT_GAMES: usize = 10_000;
/// Default number of timed repetitions per measurement (the median is reported).
pub const DEFAULT_RUNS: usize = 7;

/// Backend budget: time until the UI has the data needed to draw the first screen (one frame).
pub const BUDGET_FIRST_PAINT_MS: f64 = 16.0;
/// Backend budget: time to answer a search query.
pub const BUDGET_SEARCH_MS: f64 = 50.0;
/// Budget: IPC payload needed to draw the first screen.
pub const BUDGET_FIRST_PAINT_PAYLOAD_BYTES: usize = 256 * 1024;

const PLATFORMS: [Platform; 12] = [
    Platform::Steam,
    Platform::Steam,
    Platform::Steam,
    Platform::Gog,
    Platform::Epic,
    Platform::Itch,
    Platform::Ubisoft,
    Platform::Ea,
    Platform::BattleNet,
    Platform::Amazon,
    Platform::Xbox,
    Platform::EmulationStation,
];

const WORDS_A: [&str; 16] = [
    "Shadow", "Crimson", "Eternal", "Iron", "Starlight", "Hollow", "Neon", "Ancient",
    "Frozen", "Silent", "Golden", "Broken", "Savage", "Lost", "Arcane", "Solar",
];
const WORDS_B: [&str; 16] = [
    "Kingdom", "Protocol", "Frontier", "Legacy", "Odyssey", "Tactics", "Horizon", "Rebellion",
    "Chronicles", "Dominion", "Requiem", "Descent", "Uprising", "Voyage", "Covenant", "Paradox",
];
const SUFFIXES: [&str; 6] = ["", " II", " III", ": Remastered", " Deluxe Edition", ": Origins"];

/// Deterministic linear congruential generator (no external RNG dependency).
struct Lcg(u64);

impl Lcg {
    fn next(&mut self) -> u64 {
        self.0 = self
            .0
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        self.0 >> 33
    }

    fn pick<'a, T>(&mut self, items: &'a [T]) -> &'a T {
        &items[(self.next() as usize) % items.len()]
    }
}

/// Generate `count` deterministic synthetic games spread across storefronts.
pub fn mock_games(count: usize) -> Vec<Game> {
    let mut rng = Lcg(0x5EED_1234_ABCD_0001);
    (0..count)
        .map(|i| {
            let platform = *rng.pick(&PLATFORMS);
            let installed = rng.next() % 3 == 0;
            let name = format!(
                "{} {}{}",
                rng.pick(&WORDS_A),
                rng.pick(&WORDS_B),
                rng.pick(&SUFFIXES)
            );
            let app_id = 100_000 + i as u32;
            let install_dir = installed.then(|| format!("C:\\Games\\{}\\{name}", platform.id()));
            Game {
                app_id,
                name,
                kind: if platform == Platform::Steam {
                    GameKind::Steam
                } else {
                    GameKind::Shortcut
                },
                library_path: installed.then(|| "C:\\Games".to_string()),
                exe: install_dir.as_ref().map(|d| format!("{d}\\game.exe")),
                start_dir: install_dir.clone(),
                install_dir,
                size_on_disk: if installed { (rng.next() % 120_000) * 1_048_576 } else { 0 },
                installed,
                last_played: (rng.next() % 4 == 0).then(|| 1_700_000_000 + (rng.next() % 50_000_000) as i64),
                playtime_minutes: Some(rng.next() % 20_000),
                collections: Vec::new(),
                favorite: rng.next() % 25 == 0,
                hidden: false,
                platform,
                shortcut_id: None,
                icon: None,
                launch_uri: Some(format!("{}://launch/{app_id}", platform.id())),
                install_uri: None,
                uninstall_uri: None,
                platform_id: Some(app_id.to_string()),
                capsule_image: Some(format!("https://cdn.example.invalid/{app_id}/capsule.jpg")),
                hero_image: Some(format!("https://cdn.example.invalid/{app_id}/hero.jpg")),
                logo_image: None,
                console: (platform == Platform::EmulationStation).then(|| "gc".to_string()),
            }
        })
        .collect()
}

/// Returns the mock library size requested via `UGL_MOCK_LIBRARY`, if any.
pub fn mock_library_size() -> Option<usize> {
    std::env::var("UGL_MOCK_LIBRARY")
        .ok()
        .and_then(|v| v.trim().parse::<usize>().ok())
        .filter(|n| *n > 0)
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct Metric {
    name: &'static str,
    median_ms: f64,
    note: &'static str,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct Budget {
    name: &'static str,
    actual: f64,
    limit: f64,
    unit: &'static str,
    pass: bool,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BenchReport {
    path: &'static str,
    games: usize,
    runs: usize,
    profile: &'static str,
    full_payload_bytes: usize,
    metrics: Vec<Metric>,
    budgets: Vec<Budget>,
    all_budgets_pass: bool,
}

fn median_ms<F: FnMut()>(runs: usize, mut f: F) -> f64 {
    let mut samples: Vec<f64> = (0..runs.max(1))
        .map(|_| {
            let start = Instant::now();
            f();
            start.elapsed().as_secs_f64() * 1000.0
        })
        .collect();
    samples.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let ms = samples[samples.len() / 2];
    (ms * 1000.0).round() / 1000.0
}

/// Benchmark the current (v1) library hot paths: the whole library is cached as one
/// JSON blob in the KV tier, deserialized per call, and shipped to the UI as `Game[]`.
pub fn run(games_count: usize, runs: usize) -> BenchReport {
    const KEY: &str = "library:default";
    let games = mock_games(games_count);

    let db_file = std::env::temp_dir().join(format!("ugl-bench-{}.sqlite", std::process::id()));
    let _ = std::fs::remove_file(&db_file);

    let warm = MetadataCache::with_connection(
        Connection::open(&db_file).unwrap_or_else(|_| Connection::open_in_memory().expect("sqlite")),
    );

    let kv_store = median_ms(runs, || {
        let _ = warm.set_kv_json(KEY, &games, None);
    });

    // Cold start: fresh connection, nothing in the in-memory tier yet.
    let kv_load_cold = median_ms(runs, || {
        let cold = MetadataCache::with_connection(
            Connection::open(&db_file).unwrap_or_else(|_| Connection::open_in_memory().expect("sqlite")),
        );
        let loaded: Option<Vec<Game>> = cold.get_kv_json(KEY);
        std::hint::black_box(loaded);
    });

    // Warm path used by every `load_library` / `get_library_fast` call.
    let kv_load_warm = median_ms(runs, || {
        let loaded: Option<Vec<Game>> = warm.get_kv_json(KEY);
        std::hint::black_box(loaded);
    });

    let mut payload_len = 0usize;
    let ipc_serialize = median_ms(runs, || {
        let bytes = serde_json::to_vec(&games).unwrap_or_default();
        payload_len = bytes.len();
        std::hint::black_box(bytes);
    });

    let search = median_ms(runs, || {
        let q = "crimson leg";
        let hits: Vec<&Game> = games
            .iter()
            .filter(|g| g.name.to_lowercase().contains(q) || q.split(' ').all(|t| g.name.to_lowercase().contains(t)))
            .collect();
        std::hint::black_box(hits);
    });

    let sort = median_ms(runs, || {
        let mut refs: Vec<&Game> = games.iter().collect();
        refs.sort_by_key(|g| g.name.to_lowercase());
        std::hint::black_box(refs);
    });

    drop(warm);
    let _ = std::fs::remove_file(&db_file);
    let _ = std::fs::remove_file(db_file.with_extension("sqlite-wal"));
    let _ = std::fs::remove_file(db_file.with_extension("sqlite-shm"));

    // v1: the first screen can only render after the full blob is loaded and serialized.
    let first_paint_warm = kv_load_warm + ipc_serialize;
    let first_paint_cold = kv_load_cold + ipc_serialize;

    let budgets = vec![
        Budget {
            name: "firstPaintWarm",
            actual: round3(first_paint_warm),
            limit: BUDGET_FIRST_PAINT_MS,
            unit: "ms",
            pass: first_paint_warm <= BUDGET_FIRST_PAINT_MS,
        },
        Budget {
            name: "firstPaintCold",
            actual: round3(first_paint_cold),
            limit: BUDGET_FIRST_PAINT_MS,
            unit: "ms",
            pass: first_paint_cold <= BUDGET_FIRST_PAINT_MS,
        },
        Budget {
            name: "search",
            actual: search,
            limit: BUDGET_SEARCH_MS,
            unit: "ms",
            pass: search <= BUDGET_SEARCH_MS,
        },
        Budget {
            name: "firstPaintPayload",
            actual: payload_len as f64,
            limit: BUDGET_FIRST_PAINT_PAYLOAD_BYTES as f64,
            unit: "bytes",
            pass: payload_len <= BUDGET_FIRST_PAINT_PAYLOAD_BYTES,
        },
    ];
    let all_budgets_pass = budgets.iter().all(|b| b.pass);

    BenchReport {
        path: "v1-kv-blob",
        games: games_count,
        runs,
        profile: if cfg!(debug_assertions) { "debug" } else { "release" },
        full_payload_bytes: payload_len,
        metrics: vec![
            Metric { name: "kvStore", median_ms: kv_store, note: "serialize + persist full library blob" },
            Metric { name: "kvLoadCold", median_ms: kv_load_cold, note: "fresh connection read + deserialize" },
            Metric { name: "kvLoadWarm", median_ms: kv_load_warm, note: "in-memory tier clone + deserialize" },
            Metric { name: "ipcSerialize", median_ms: ipc_serialize, note: "Game[] -> JSON for the webview" },
            Metric { name: "search", median_ms: search, note: "linear lowercase scan (Rust lower bound of the JS filter)" },
            Metric { name: "sortByName", median_ms: sort, note: "full re-sort by lowercase name" },
        ],
        budgets,
        all_budgets_pass,
    }
}

fn round3(v: f64) -> f64 {
    (v * 1000.0).round() / 1000.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mock_games_is_deterministic_and_sized() {
        let a = mock_games(500);
        let b = mock_games(500);
        assert_eq!(a.len(), 500);
        assert_eq!(a[123].name, b[123].name);
        assert!(a.iter().any(|g| g.platform != Platform::Steam));
    }

    #[test]
    fn bench_runs_on_small_library() {
        let report = run(200, 1);
        assert_eq!(report.games, 200);
        assert!(report.full_payload_bytes > 0);
        assert_eq!(report.budgets.len(), 4);
    }
}
