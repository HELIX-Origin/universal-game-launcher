# Verification Gate — universal-game-launcher

> Run all commands from the repository root unless noted. All must pass with zero errors and zero
> warnings before merging any branch.

## Commands

```powershell
# 1. Rust type-check — warnings are errors
RUSTFLAGS="-D warnings" cargo check --manifest-path src-tauri/Cargo.toml

# 2. Rust test suite
cargo test --manifest-path src-tauri/Cargo.toml

# 3. Frontend type-check — 0 errors, 0 warnings required
npm run check

# 4. Production frontend build — must succeed cleanly
npm run build

# 5. Full test suite via script runner
node scripts/test.mjs check rust frontend build
```

## One-shot shortcut

```powershell
node scripts/test.mjs check rust frontend build
```

This runs all five steps above via the unified script runner. A green exit means the full gate passed.

## Failure modes

| Step | Common cause | Fix |
|---|---|---|
| `cargo check` | Unused import / variable warning | Remove or prefix with `_` |
| `cargo check` | Type mismatch / missing trait impl | Align types; check `error.rs` `From` impls |
| `cargo test` | Logic regression in adapter or metadata | Fix test; do not `#[ignore]` without justification |
| `npm run check` | Svelte/TS type error or warning | Fix the type; never use `@ts-ignore` without comment |
| `npm run build` | SCSS token missing / import error | Check `_tokens.scss`; verify `$lib/` alias |

## CI equivalent

The GitHub Actions release workflow runs the same commands in a matrix
(`windows-latest`, `macos-latest`, `ubuntu-22.04`). A local green gate is a reliable predictor
of a clean CI run. Performance budgets (cold start < 1.5 s, 60 fps grid, search < 50 ms) are
tracked as CI gates — do not regress them.
