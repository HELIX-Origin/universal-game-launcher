#!/usr/bin/env node
// Library performance benchmark runner.
//
// Builds the Rust core in release mode and runs the headless `--cli bench`
// subcommand against a deterministic synthetic library, recording a timestamped
// transcript in `logs/`. The JSON report lists median timings and pass/fail
// against the performance budgets.
//
// Usage:
//   node scripts/bench.mjs [--games N] [--runs R] [--debug]
//
// UI profiling against a large library: set `UGL_MOCK_LIBRARY=<N>` before
// `npm run tauri:dev` so `load_library` returns N synthetic games.

import path from "node:path";
import { runLogged } from "./lib/runner.mjs";
import { ROOT } from "./lib/logger.mjs";

const argv = process.argv.slice(2);
const debug = argv.includes("--debug");
const benchArgs = argv.filter((a) => a !== "--debug");

const cargoArgs = [
  "run",
  "--quiet",
  "--manifest-path",
  path.join(ROOT, "src-tauri", "Cargo.toml"),
  ...(debug ? [] : ["--release"]),
  "--",
  "--cli",
  "bench",
  ...benchArgs
];

process.exitCode = await runLogged({
  type: "bench",
  command: "cargo",
  args: cargoArgs,
  stdin: "ignore",
  timeoutMs: 30 * 60 * 1000
});
