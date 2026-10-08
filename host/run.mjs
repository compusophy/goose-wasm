// Runs one benchmark module built by bench.py, once, and prints a JSON line:
//   node --no-liftoff host/run.mjs <module.wasm> goose|rust
// Both languages get the same engine, the same imports (host/imports.mjs) and
// the same clock. The timed region is the program's whole run -- Goose's C
// main (renamed gs_c_main, which runs Goose's main) or Rust's exported run()
// (which calls main, including every drop at its end) -- after the module is
// compiled and instantiated, which is reported separately.

import { readFileSync } from "node:fs";
import { performance } from "node:perf_hooks";
import { makeHost, runProgram } from "./imports.mjs";

const [file, lang, mode] = process.argv.slice(2);
const bytes = readFileSync(file);

const host = makeHost(() => performance.now());
const errors = [];

const t0 = performance.now();
const module = new WebAssembly.Module(bytes);
const instance = new WebAssembly.Instance(module, { env: host.env });
const t1 = performance.now();
host.attach(instance);

const t2 = performance.now();
const code = runProgram(instance, lang, (e) => errors.push(e));
const t3 = performance.now();

process.stdout.write(JSON.stringify({
    ms: t3 - t2,
    compile_ms: t1 - t0,
    code,
    out: host.out(),
    err: host.err() + errors.join(""),
    linear_bytes: host.memoryBytes(),
    reserved_bytes: lang === "goose" ? instance.exports.gs_wasm_reserved() : 0,
    // `probe`: the largest data stack's high-water mark, which bench.py sizes
    // the timed build's reservation from. Scanned after the timing.
    highwater_bytes: mode === "probe" ? instance.exports.gs_wasm_highwater() : undefined,
    peak_rss: process.resourceUsage().maxRSS * 1024,
}) + "\n");
