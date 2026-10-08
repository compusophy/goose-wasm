#!/usr/bin/env python3
"""Goose vs Rust on wasm32-unknown-unknown.

Builds every benchmark in the Goose repo's own suite (goose-lang/bench) to
WebAssembly twice -- the Goose rows through Goose -> C -> clang, the Rust rows
through rustc -- runs both in the same engine (Node's V8, optimizing tier
only), checks that every implementation prints the same checksums, and writes
results.md and results.json.

The benchmark programs and their sizes are the suite's, unmodified: the
manifest is imported from goose-lang/bench/run_bench.py. The only additions
are the wasm plumbing: Goose links rt/goose_wasm_rt.c in place of the native
runtime, and Rust gets rt/rust_prelude.rs so println! reaches the host.

  python bench.py                        # every benchmark, small size
  python bench.py --only sum,tree        # a subset
  python bench.py --sizes small,medium   # bigger inputs
  python bench.py --reps 7               # best of 7 (default 5)
  python bench.py --simd                 # both toolchains with +simd128
"""

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
GOOSE_REPO = Path(os.environ.get("GOOSE_REPO", HERE.parent / "goose-lang")).resolve()
BENCH = GOOSE_REPO / "bench"
RT = HERE / "rt"
GEN = HERE / "gen"

sys.path.insert(0, str(BENCH))
import run_bench as suite  # noqa: E402  (the manifest, write_sized, the size patterns)

EXE = ".exe" if os.name == "nt" else ""
GOOSE = GOOSE_REPO / "build" / "Release" / ("goose" + EXE)
if not GOOSE.exists():
    GOOSE = GOOSE_REPO / "build" / ("goose" + EXE)
CLANG = shutil.which("clang") or "clang"
RUSTC = shutil.which("rustc") or "rustc"
NODE = shutil.which("node") or "node"

# Features both toolchains build for. These are clang's and rustc's current
# wasm32 defaults, spelled out so neither side gets something the other lacks.
FEATURES = ["bulk-memory", "nontrapping-fptoint", "sign-ext", "mutable-globals", "multivalue",
            "reference-types"]
STACK_SIZE = 8 << 20          # the native (shadow) call stack, both languages
PROBE_MB = [1024, 512, 256]   # probe reservations per Goose data stack, largest that fits
MIN_RESERVE_MB = 16
# V8 compiles wasm lazily by default: without --no-wasm-lazy-compilation each
# function is compiled on its first call, inside the timing, which charges
# every program for its own code size (Rust's empty program took 10 ms).
NODE_FLAGS = ["--no-liftoff", "--no-wasm-dynamic-tiering", "--no-wasm-lazy-compilation"]


def run(argv, **kw):
    r = subprocess.run([str(a) for a in argv], capture_output=True, text=True, **kw)
    return r.returncode, r.stdout, r.stderr


def build_goose(src, n, tag, simd, reserve_mb):
    gsrc = GEN / f"{tag}.goose"
    suite.write_sized(BENCH / "goose" / src, gsrc, n, suite.GOOSE_N)
    cfile, wasm = GEN / f"{tag}.c", GEN / f"{tag}.wasm"
    code, out, err = run([GOOSE, "-O2", "-o", cfile, gsrc])
    (GEN / f"{tag}.goose.log").write_text(out + err)
    if code:
        return None, "goose compile failed: " + (out + err).strip().splitlines()[-1]
    ver = re.search(r"#define GS_RUNTIME_VERSION (\w+)", cfile.read_text()).group(1)
    feats = FEATURES + (["simd128"] if simd else [])
    argv = [CLANG, "--target=wasm32-unknown-unknown", "-O2", "-flto", "-nostdlib",
            "-isystem", RT / "include", *[f"-m{f}" for f in feats],
            f"-DGS_STACK_RESERVE=({reserve_mb}ull<<20)", f"-DGS_RT_START_NAME=gs_rt_start_{ver}",
            # The program's C main becomes an ordinary exported function the
            # host calls; there is no _start on this target.
            "-Dmain=gs_c_main",
            "-Wno-everything",
            cfile, RT / "goose_wasm_rt.c",
            # clang runs a wasm-opt it finds on PATH after linking; rustc never
            # does, so neither side gets it.
            "--no-wasm-opt",
            "-Wl,--no-entry", "-Wl,--export=gs_c_main", f"-Wl,-z,stack-size={STACK_SIZE}",
            "-o", wasm]
    code, out, err = run(argv)
    (GEN / f"{tag}.cc.log").write_text(" ".join(map(str, argv)) + "\n" + out + err)
    return (wasm, None) if code == 0 else (None, "clang failed: " + err.strip().splitlines()[-1])


def build_rust(src, n, tag, simd):
    rs = GEN / f"{tag}.rs"
    suite.write_sized(BENCH / "rust" / src, rs, n, suite.RUST_N)
    rs.write_text((RT / "rust_prelude.rs").read_text() + rs.read_text())
    shutil.copy(BENCH / "rust" / "bench.rs", GEN / "bench.rs")
    wasm = GEN / f"{tag}.wasm"
    feats = ",".join("+" + f for f in FEATURES + (["simd128"] if simd else []))
    # The suite's native flags (-O -C codegen-units=1), retargeted.
    argv = [RUSTC, "--edition", "2021", "--target", "wasm32-unknown-unknown",
            "--crate-type", "cdylib", "-O", "-C", "codegen-units=1",
            "-C", f"target-feature={feats}", "-C", f"link-arg=-zstack-size={STACK_SIZE}",
            "-A", "warnings", "-o", wasm, rs]
    code, out, err = run(argv)
    (GEN / f"{tag}.rs.log").write_text(" ".join(map(str, argv)) + "\n" + out + err)
    return (wasm, None) if code == 0 else (None, "rustc failed, see " + f"{tag}.rs.log")


def run_once(wasm, lang, mode=""):
    code, out, err = run([NODE, *NODE_FLAGS, HERE / "host" / "run.mjs", wasm, lang, mode])
    if code:
        return {"code": code, "err": err.strip()[-300:]}
    return json.loads(out.strip().splitlines()[-1])


def probe_reserve(src, n, tag, simd):
    """How much to reserve per data stack: one run under a large reservation
    reports the fullest stack's high-water mark, and the timed build reserves
    twice that, rounded up to a power of two. Linear memory grows only by
    committing the whole reservation, which V8 charges about 3 ms per GB for,
    so a blanket 1 GB per stack would time the reservation, not the program.
    Rust's allocator grows the heap as it needs to; this is the closest
    equivalent."""
    for mb in PROBE_MB:
        wasm, note = build_goose(src, n, tag, simd, mb)
        if not wasm:
            return None, note
        r = run_once(wasm, "goose", "probe")
        if not r.get("code"):
            need = 2 * r["highwater_bytes"] / 2**20
            return max(MIN_RESERVE_MB, 1 << math.ceil(math.log2(max(need, 1)))), None
        if "cannot reserve" not in (r.get("err") or ""):
            return None, last_line(r.get("err")) or f"exit {r.get('code')}"
    return None, "no reservation fits in 4 GB of linear memory"


def measure(wasm, lang, reps):
    """Best time and worst peak RSS over `reps` fresh processes, after one
    discarded warm-up run (first execution pays for disk and code caches)."""
    r = run_once(wasm, lang)
    if r.get("code"):
        return {"ok": False, "note": last_line(r.get("err")) or f"exit {r.get('code')}"}
    best = None
    for _ in range(reps):
        r = run_once(wasm, lang)
        if r.get("code"):
            return {"ok": False, "note": last_line(r.get("err")) or f"exit {r.get('code')}"}
        if best is None or r["ms"] < best["ms"]:
            best = {**r, "peak_rss": max(r["peak_rss"], best["peak_rss"] if best else 0)}
        else:
            best["peak_rss"] = max(best["peak_rss"], r["peak_rss"])
    best["ok"] = True
    return best


def strip_custom(b):
    """A module's bytes without its custom sections (names, producers, debug
    info), which no engine needs to run it."""
    out, i = bytearray(b[:8]), 8
    while i < len(b):
        sid = b[i]
        j, size, shift = i + 1, 0, 0
        while True:
            byte = b[j]
            j += 1
            size |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                break
        if sid != 0:
            out += b[i:j + size]
        i = j + size
    return bytes(out)


def code_size(wasm):
    return len(strip_custom(Path(wasm).read_bytes()))


def last_line(s):
    lines = (s or "").strip().splitlines()
    return lines[-1] if lines else ""

def geomean(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--sizes", default="small")
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--simd", action="store_true")
    ap.add_argument("--reserve", default="auto",
                    help="MB per Goose data stack, or auto: twice the probed high-water mark")
    ap.add_argument("--web", action="store_true",
                    help="also export the modules and results for the browser page (web/dist)")
    ap.add_argument("--export-only", action="store_true",
                    help="rebuild web/dist from the last run's results.json and gen/, measuring nothing")
    args = ap.parse_args()
    fixed_reserve = None if args.reserve == "auto" else int(args.reserve)

    if args.export_only:
        saved = json.loads((HERE / "results.json").read_text())
        args.reps = saved["meta"]["reps"]
        floor = {k: {"peak_rss": v} for k, v in saved["meta"]["floor_rss"].items()}
        export_web(saved["results"], floor, args)
        return

    only = {s for s in args.only.split(",") if s}
    sizes = [s for s in args.sizes.split(",") if s]
    GEN.mkdir(exist_ok=True)
    shutil.copy(BENCH / "goose" / "rng.goose", GEN)   # imports resolve next to the root file

    # What an empty program of each kind costs: the engine's own memory floor,
    # subtracted in the report so the MB columns are what the program used.
    empty_g, empty_r = GEN / "_empty.goose", GEN / "_empty.rs"
    empty_g.write_text("let N = 0; // BENCH_N\nfn main() { print(N); }\n")
    empty_r.write_text("mod bench;\nconst N: i64 = 0; // BENCH_N\nfn main() { bench::emit(N); }\n")
    floor = {}
    w, _ = build_goose(empty_g, 0, "empty_goose", args.simd, 64)
    floor["goose"] = measure(w, "goose", args.reps)
    w, _ = build_rust(empty_r, 0, "empty_rust", args.simd)
    floor["rust"] = measure(w, "rust", args.reps)
    print(f"  floor: goose {fmt_row(floor['goose'])}, rust {fmt_row(floor['rust'])}")

    results = []
    for b in suite.BENCHMARKS:
        if only and b["name"] not in only:
            continue
        for size in sizes:
            n = b["sizes"][suite.SIZE_NAMES.index(size)]
            rows = []
            for impl in b["goose"]:
                tag = f"{b['name']}_{size}_{Path(impl['file']).stem}_goose"
                mb, note = fixed_reserve, None
                if mb is None:
                    mb, note = probe_reserve(impl["file"], n, tag, args.simd)
                res = {"ok": False, "note": note}
                if mb:
                    wasm, note = build_goose(impl["file"], n, tag, args.simd, mb)
                    res = measure(wasm, "goose", args.reps) if wasm else {"ok": False, "note": note}
                    if wasm:
                        res["code_bytes"] = code_size(wasm)
                    res["reserve_mb"] = mb
                rows.append({"lang": "goose", "label": impl["label"], "tag": tag, **res})
                print(f"  {b['name']:9} {size:6} goose {impl['label']:42} "
                      f"{fmt_row(rows[-1])}", flush=True)
            for impl in b["rust"]:
                tag = f"{b['name']}_{size}_{Path(impl['file']).stem}_rust"
                wasm, note = build_rust(impl["file"], n, tag, args.simd)
                res = measure(wasm, "rust", args.reps) if wasm else {"ok": False, "note": note}
                if wasm:
                    res["code_bytes"] = code_size(wasm)
                rows.append({"lang": "rust", "label": impl["label"], "tag": tag, **res})
                print(f"  {b['name']:9} {size:6} rust  {impl['label']:42} "
                      f"{fmt_row(rows[-1])}", flush=True)
            outs = {r["out"] for r in rows if r.get("ok")}
            agree = len(outs) <= 1
            if not agree:
                print(f"  !! {b['name']} {size}: implementations disagree on their checksums")
            results.append({"name": b["name"], "what": b["what"], "param": b.get("param", "N"),
                            "size": size, "n": n, "agree": agree, "rows": rows})

    write_report(results, floor, args)
    if args.web:
        export_web(results, floor, args)


def export_web(results, floor, args):
    """web/dist: the page with these results inlined as its reference, the
    worker and host imports, and every module that ran, custom sections
    stripped (a browser fetches them, and names and debug info are dead
    weight there)."""
    dist = HERE / "web" / "dist"
    if dist.exists():
        shutil.rmtree(dist)
    (dist / "wasm").mkdir(parents=True)
    benches = []
    for res in results:
        rows = []
        for row in res["rows"]:
            if not row.get("ok"):
                continue
            name = f"{row['tag']}.wasm"
            (dist / "wasm" / name).write_bytes(strip_custom((GEN / name).read_bytes()))
            rows.append({"lang": row["lang"], "label": row["label"], "wasm": f"wasm/{name}",
                         "ms": round(row["ms"], 2), "mb": round(net_mb(row, floor), 1),
                         "kb": round(row["code_bytes"] / 1024, 1)})
        expected = next((r["out"] for r in res["rows"] if r.get("ok")), "")
        benches.append({"name": res["name"], "what": res["what"], "param": res["param"],
                        "n": res["n"], "size": res["size"], "expected": expected, "rows": rows})
    _, node_v, _ = run([NODE, "--version"])
    _, rustc_v, _ = run([RUSTC, "--version"])
    ref = {"node": node_v.strip(), "rustc": rustc_v.strip().split(" (")[0],
           "date": datetime.now().strftime("%Y-%m-%d"), "reps": args.reps, "benchmarks": benches}
    page = (HERE / "web" / "page.html").read_text(encoding="utf-8")
    page = page.replace("/*REFERENCE*/null", json.dumps(ref, separators=(",", ":")))
    # page.html is the body an Artifact host wraps in its own document;
    # index.html is the same page as a whole document, for any static host.
    (dist / "page.html").write_text(page, encoding="utf-8")
    (dist / "index.html").write_text(
        '<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
        + page + "\n</html>\n", encoding="utf-8")
    shutil.copy(HERE / "web" / "worker.js", dist / "worker.js")
    shutil.copy(HERE / "host" / "imports.mjs", dist / "imports.js")
    worker = (dist / "worker.js").read_text().replace('"./imports.mjs"', '"./imports.js"')
    (dist / "worker.js").write_text(worker)
    total = sum(f.stat().st_size for f in dist.rglob("*") if f.is_file())
    print(f"wrote web/dist ({total / 1024:.0f} KB)")


def fmt_row(r):
    if not r.get("ok"):
        return "FAILED " + r.get("note", "")
    return f"{r['ms']:9.1f} ms  {r['peak_rss'] / 2**20:7.1f} MB peak RSS"


def best(rows, lang):
    ok = [r for r in rows if r["lang"] == lang and r.get("ok")]
    return min(ok, key=lambda r: r["ms"]) if ok else None


def net_mb(row, floor):
    """Peak RSS above an empty program of the same language, in MB."""
    return max(row["peak_rss"] - floor[row["lang"]]["peak_rss"], 0) / 2**20


def write_report(results, floor, args):
    stamp = datetime.now().strftime("%Y-%m-%d %H:%M")
    _, node_v, _ = run([NODE, "--version"])
    _, rustc_v, _ = run([RUSTC, "--version"])
    _, clang_v, _ = run([CLANG, "--version"])
    _, git_v, _ = run(["git", "-C", GOOSE_REPO, "rev-parse", "--short", "HEAD"])
    meta = {
        "date": stamp, "node": node_v.strip(), "rustc": rustc_v.strip(),
        "clang": clang_v.splitlines()[0].strip() if clang_v else "", "goose": git_v.strip(),
        "simd": args.simd, "reps": args.reps, "sizes": args.sizes,
    }
    meta["floor_rss"] = {k: v.get("peak_rss") for k, v in floor.items()}
    (HERE / "results.json").write_text(json.dumps({"meta": meta, "results": results}, indent=1))

    L = []
    W = L.append
    W("# Goose vs Rust on wasm32-unknown-unknown\n")
    W(f"Measured {stamp}. Engine: Node {meta['node']} (V8, `{' '.join(NODE_FLAGS)}`: "
      f"optimizing tier only, compiled before the timer starts). Goose `{meta['goose']}` -> C -> {meta['clang']} `-O2 -flto`. "
      f"{meta['rustc']} `-O -C codegen-units=1`. SIMD: {'on' if args.simd else 'off'}. "
      f"Best of {args.reps} fresh processes; time is the program's run inside the engine, "
      "compile and instantiate excluded. MB is peak RSS above an empty program of the same "
      f"language (floors: Goose {floor['goose']['peak_rss'] / 2**20:.1f} MB, "
      f"Rust {floor['rust']['peak_rss'] / 2**20:.1f} MB). KB is the module's code size "
      "with custom sections stripped.\n")
    W("| benchmark | size | best Goose (ms) | best Rust (ms) | Goose speedup | Goose MB | Rust MB | Goose KB | Rust KB |")
    W("|---|---|---:|---:|---:|---:|---:|---:|---:|")
    ratios, mems = [], []
    for res in results:
        g, r = best(res["rows"], "goose"), best(res["rows"], "rust")
        if not (g and r):
            gs = "%.1f" % g["ms"] if g else "-"
            rs = "%.1f" % r["ms"] if r else "-"
            W(f"| {res['name']} | {res['size']} | {gs} | {rs} | n/a | | | | |")
            continue
        ratio = r["ms"] / g["ms"]
        ratios.append(ratio)
        gm_mb, rs_mb = net_mb(g, floor), net_mb(r, floor)
        if gm_mb >= 1 and rs_mb >= 1:
            mems.append(rs_mb / gm_mb)
        flag = "" if res["agree"] else " (checksums differ!)"
        W(f"| {res['name']} | {res['size']} ({res['param']}={res['n']}) | {g['ms']:.1f} | "
          f"{r['ms']:.1f} | **{ratio:.2f}x**{flag} | {gm_mb:.1f} | {rs_mb:.1f} | "
          f"{g['code_bytes'] / 1024:.1f} | {r['code_bytes'] / 1024:.1f} |")
    gm, gmm = geomean(ratios), geomean(mems)
    if gm:
        W(f"\n**Geometric mean: Goose runs {gm:.2f}x the speed of the best Rust row, "
          + (f"using {gmm:.2f}x less memory** (over rows where both use at least 1 MB).\n"
             if gmm else "**\n"))
    W("## Every row\n")
    for res in results:
        W(f"### {res['name']} ({res['size']}, {res['param']}={res['n']})\n")
        W(res["what"] + "\n")
        W("| implementation | ms | MB used | code KB | Goose reserve per stack |")
        W("|---|---:|---:|---:|---:|")
        for row in res["rows"]:
            if row.get("ok"):
                W(f"| {row['lang']}: {row['label']} | {row['ms']:.1f} | "
                  f"{net_mb(row, floor):.1f} | {row['code_bytes'] / 1024:.1f} | "
                  + (f"{row['reserve_mb']} MB |" if row.get("reserve_mb") else "|"))
            else:
                W(f"| {row['lang']}: {row['label']} | failed: {row.get('note', '')} | | | |")
        W("")
    (HERE / "results.md").write_text("\n".join(L) + "\n")
    print(f"\nwrote results.md and results.json" + (f"; geomean {gm:.2f}x" if gm else ""))


if __name__ == "__main__":
    main()
