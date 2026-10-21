# Local Conventions — Tauri v2 Desktop

- Rust core lives in `src-tauri/`; frontend in `src/`.
- All Tauri commands are registered in `src-tauri/src/lib.rs` and typed in the frontend.
- Window state, tray, and updater follow the Tauri v2 plugin model.
- Never commit generated bundles; CI builds them.
- Tests: `cargo test` (Rust) + frontend unit tests; both must be green before merge.