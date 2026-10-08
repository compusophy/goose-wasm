# Goose on bare WebAssembly

[Goose](https://github.com/aardappel/goose) is a memory-safe systems language with no heap, released
on 2026-10-08. This repository ports it to `wasm32-unknown-unknown` (no WASI, no Emscripten, no libc)
and races it against Rust on the language's own benchmark suite, compiled to the same target and run
in the same engine.

The Goose programs and the Rust programs are the ones that ship in Goose's `bench/` directory,
unmodified. Every implementation of a benchmark must print the same checksums, and all 104 builds do.

## Results

Medium inputs, best of 7 fresh Node 20 processes (V8 optimizing tier, compiled before the timer
starts). "Speedup" is the fastest Rust variant's time over the fastest Goose variant's.

| benchmark | what it stresses | Goose ms | Rust ms | speedup | Goose MB | Rust MB |
|---|---|---:|---:|---:|---:|---:|
| respond | building a nested response object | 141.5 | 284.1 | **2.01x** | 1.8 | 0.6 |
| records | variant records of different sizes | 145.2 | 234.0 | **1.61x** | 35.5 | 123.0 |
| words | hash table keyed by string slices | 656.1 | 782.0 | **1.19x** | 132.4 | 111.8 |
| push | appending while holding references | 71.1 | 81.0 | **1.14x** | 124.4 | 121.8 |
| interp | expression tree evaluation | 11.9 | 13.6 | **1.14x** | 1.9 | 6.8 |
| strlist | a list of owned strings | 128.0 | 144.0 | **1.13x** | 53.9 | 54.0 |
| tree | binary tree in a pool | 72.4 | 76.6 | **1.06x** | 24.0 | 21.7 |
| graph | adjacency lists and BFS | 289.0 | 302.6 | **1.05x** | 53.9 | 51.5 |
| scene | scene graph with child lists | 141.6 | 149.3 | **1.05x** | 28.4 | 57.1 |
| lru | LRU cache, random access | 797.9 | 821.7 | **1.03x** | 31.8 | 45.4 |
| particles | flat float kernel | 500.4 | 505.9 | 1.01x | 23.1 | 20.2 |
| calc | parser and evaluator | 203.9 | 204.8 | 1.00x | 1.1 | 0.1 |
| sexp | s-expression trees with text | 676.0 | 661.3 | 0.98x | 123.1 | 288.0 |
| blur | 3x3 image kernel | 182.6 | 175.0 | 0.96x | 8.2 | 5.7 |
| sum | control: scan a flat array | 88.2 | 83.9 | 0.95x | 61.1 | 58.9 |
| bintrees | build and discard many small trees | 215.8 | 140.3 | 0.65x | 6.4 | 5.3 |

**Geometric mean: Goose runs 1.09x the speed of the best Rust variant** at medium size (10 wins,
2 level, 4 losses), and 1.05x at small size. Goose modules are 3 to 10 KB; the Rust ones are 19 to
29 KB, both with custom sections stripped. MB is peak RSS above an empty program of the same language.

The wins line up with what Goose's design predicts: variable-size enums (`records`, 3.5x less memory),
inline nested data (`respond`, `strlist`), and references that survive growth (`push`). `sum` and
`particles` are the controls, where both languages emit the same loop. `bintrees` is the clear loss.
Upstream measures the same benchmark natively at 24% behind the Rust arena under clang and level under
MSVC, and attributes it to how LLVM compiles the recursion; on wasm, LLVM is the only backend.

Every row, both sizes, all variants: [`results.md`](results.md). Raw data: [`results.json`](results.json).

## How it works

```
benchmark.goose ──goose -O2──> benchmark.c ──clang -O2 -flto──> benchmark.wasm
                                              --target=wasm32-unknown-unknown
                                              + rt/goose_wasm_rt.c
                                              + rt/include/*.h (4 stub headers)
```

The Goose compiler emits one C file per program, with everything a hot loop touches (pushes,
indexing, varints, relative links) inline in it. What it leaves to a separate runtime object is small:
aborts, number formatting, printing, and handing out data stack regions. [`rt/goose_wasm_rt.c`](rt/goose_wasm_rt.c)
replaces that object in under 500 lines of freestanding C. A Goose module imports at most four
functions from its host (`gs_host_write`, `gs_host_exit`, `gs_host_fmt_f64`, `gs_host_now_ms`).

**Data stacks.** Natively, each data stack is a large address-space reservation whose pages commit on
first touch, followed by an unmapped guard gap. Linear memory grows only at its end and has no page
protection, so here a region is a `memory.grow` of the whole reservation. Engines map those pages
lazily, so an untouched reservation costs address space rather than RAM.

**Rust** builds with the suite's native flags retargeted (`-O -C codegen-units=1 --target
wasm32-unknown-unknown`), its default allocator (dlmalloc) and std. [`rt/rust_prelude.rs`](rt/rust_prelude.rs)
shadows `println!` so output reaches the same host import Goose uses. Both toolchains use the same wasm
features, no SIMD, and no wasm-opt.

## Three things that changed the numbers

Getting a fair comparison took more work than getting Goose to run.

1. **Power-of-two region spacing.** The first run had `blur` at 99 ms against Rust's 58, though the two
   inner loops disassembled to nearly the same instructions. Each data stack region was exactly as long
   as its reservation, so the source and destination images started exactly 1 GB apart, and every pixel
   pair agreed in every address bit the L1 and L2 caches use to pick a set. Starting each region at a
   different offset into an extra page (cache coloring) brought it to 57.5 ms. Sharing only the low 16
   address bits, plain 4K aliasing, costs 2 to 10% on its own; the power-of-two distance was the rest.
2. **Lazy compilation inside the timer.** V8 compiles wasm lazily by default, so each function was being
   compiled by the optimizing tier on its first call, inside the timing. An empty Rust program took 10 ms,
   almost all of it compiling `fmt` and the allocator. The runner now passes `--no-wasm-lazy-compilation`.
3. **Up-front reservations.** Growing linear memory costs V8 about 3 ms per GB on this machine, so a
   blanket 1 GB per stack was timing the reservation. Each Goose program now runs once to measure its
   fullest stack, and the timed build reserves twice that, rounded up to a power of two (at least 16 MB).
   Rust's allocator grows the heap as it needs to, which is the closest equivalent.

## Limits of the port

* No threads: `wasm32-unknown-unknown` has none without shared memory and a host that spawns workers.
  No benchmark in the suite needs them.
* No guard gap after a data stack region. Running past the last region traps, but running past an
  earlier one would write into the next. Reservations are sized with 2x headroom over a measured peak,
  and the checksums would catch an overrun.
* No file system, environment or stdin; `print`, program arguments and clocks work.
* Rust's `usize` is 32 bits on wasm32, while Goose lengths and indices are always 64-bit. That costs
  Goose some extra integer work in index-heavy loops and is left as is.
* Timings on this laptop vary by up to 15% run to run on the cache-bound benchmarks (`lru`, `graph`),
  for both languages. Treat differences under about 5% as level.

## Run it

You need Python 3.9+, Node 20+, clang with the wasm32 target and `wasm-ld` (any recent LLVM), and Rust
with `rustup target add wasm32-unknown-unknown`. Goose is expected next to this directory as
`goose-lang`, or wherever `GOOSE_REPO` points:

```bash
git clone --recursive https://github.com/aardappel/goose goose-lang
```

```bash
cmake -S goose-lang -B goose-lang/build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build goose-lang/build --config Release
```

```bash
python bench.py --sizes small,medium --reps 7 --web
```

That writes `results.md`, `results.json`, and `web/dist`, the browser demo: the same modules, raced in
the visitor's own browser in a Web Worker, with the Node numbers as reference. To view it locally:

```bash
python serve.py
```

| path | what |
|---|---|
| `rt/goose_wasm_rt.c` | the freestanding Goose runtime for wasm |
| `rt/include/` | the four libc headers the generated C includes, reduced to what it uses |
| `rt/rust_prelude.rs` | routes Rust's `println!` to the host, exports `run` |
| `host/` | the host imports, shared by the Node runner and the browser worker |
| `bench.py` | builds and measures everything; imports the suite's manifest from `goose-lang/bench` |
| `web/` | the browser page and worker |

Measured on Windows 11, Node 20.11.1, clang 23.1.3, rustc 1.96.0, Goose at `3536810`.

By compusophy. Goose is by Wouter van Oortmerssen and licensed Apache-2.0.
