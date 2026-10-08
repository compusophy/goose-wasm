# Goose vs Rust on wasm32-unknown-unknown

Measured 2026-10-08 15:51. Engine: Node v20.11.1 (V8, `--no-liftoff --no-wasm-dynamic-tiering --no-wasm-lazy-compilation`: optimizing tier only, compiled before the timer starts). Goose `3536810` -> C -> clang version 23.1.3 (https://github.com/llvm/llvm-project 0d261d1ca552c95a8f007e061c787ac7132fbcbc) `-O2 -flto`. rustc 1.96.0 (ac68faa20 2026-05-25) `-O -C codegen-units=1`. SIMD: off. Best of 7 fresh processes; time is the program's run inside the engine, compile and instantiate excluded. MB is peak RSS above an empty program of the same language (floors: Goose 33.7 MB, Rust 39.0 MB). KB is the module's code size with custom sections stripped.

| benchmark | size | best Goose (ms) | best Rust (ms) | Goose speedup | Goose MB | Rust MB | Goose KB | Rust KB |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sum | small (N=1000000) | 5.3 | 4.8 | **0.91x** | 4.1 | 1.2 | 3.2 | 18.6 |
| sum | medium (N=16000000) | 88.2 | 83.9 | **0.95x** | 61.1 | 58.9 | 3.2 | 18.6 |
| push | small (N=1000000) | 4.8 | 4.8 | **1.00x** | 8.0 | 5.1 | 3.3 | 19.3 |
| push | medium (N=16000000) | 71.1 | 81.0 | **1.14x** | 124.4 | 121.8 | 3.3 | 19.3 |
| strlist | small (N=200000) | 13.2 | 14.3 | **1.08x** | 5.2 | 3.9 | 4.1 | 20.3 |
| strlist | medium (N=2000000) | 128.0 | 144.0 | **1.13x** | 53.9 | 54.0 | 4.1 | 20.3 |
| records | small (N=500000) | 18.3 | 29.3 | **1.60x** | 4.7 | 13.0 | 3.9 | 20.0 |
| records | medium (N=4000000) | 145.2 | 234.0 | **1.61x** | 35.5 | 123.0 | 3.9 | 20.0 |
| tree | small (depth=16) | 3.7 | 3.6 | **0.98x** | 1.6 | 0.0 | 3.5 | 19.3 |
| tree | medium (depth=20) | 72.4 | 76.6 | **1.06x** | 24.0 | 21.7 | 3.5 | 19.3 |
| interp | small (depth=16) | 1.5 | 1.5 | **0.99x** | 0.1 | 0.0 | 3.6 | 19.4 |
| interp | medium (depth=20) | 11.9 | 13.6 | **1.14x** | 1.9 | 6.8 | 3.6 | 19.4 |
| graph | small (V=100000) | 28.2 | 27.5 | **0.98x** | 11.2 | 8.0 | 4.4 | 21.0 |
| graph | medium (V=500000) | 289.0 | 302.6 | **1.05x** | 53.9 | 51.5 | 4.4 | 21.0 |
| words | small (N=500000) | 46.1 | 49.1 | **1.06x** | 16.9 | 12.2 | 5.7 | 21.5 |
| words | medium (N=4000000) | 656.1 | 782.0 | **1.19x** | 132.4 | 111.8 | 5.7 | 21.5 |
| particles | small (N=100000) | 31.7 | 32.3 | **1.02x** | 2.5 | 0.0 | 3.2 | 18.6 |
| particles | medium (N=1000000) | 500.4 | 505.9 | **1.01x** | 23.1 | 20.2 | 3.2 | 18.6 |
| sexp | small (N=20000) | 137.9 | 137.7 | **1.00x** | 24.9 | 62.5 | 8.3 | 28.9 |
| sexp | medium (N=100000) | 676.0 | 661.3 | **0.98x** | 123.1 | 288.0 | 8.3 | 28.9 |
| lru | small (N=2000000) | 105.2 | 119.7 | **1.14x** | 7.9 | 9.2 | 7.8 | 21.6 |
| lru | medium (N=8000000) | 797.9 | 821.7 | **1.03x** | 31.8 | 45.4 | 7.8 | 21.6 |
| scene | small (depth=12) | 15.3 | 15.6 | **1.02x** | 4.6 | 9.6 | 5.2 | 20.9 |
| scene | medium (depth=14) | 141.6 | 149.3 | **1.05x** | 28.4 | 57.1 | 5.2 | 20.9 |
| calc | small (N=100000) | 50.9 | 51.6 | **1.01x** | 1.1 | 0.0 | 10.0 | 22.8 |
| calc | medium (N=400000) | 203.9 | 204.8 | **1.00x** | 1.1 | 0.1 | 10.0 | 22.8 |
| bintrees | small (depth=15) | 48.1 | 30.8 | **0.64x** | 2.1 | 0.0 | 4.4 | 19.8 |
| bintrees | medium (depth=17) | 215.8 | 140.3 | **0.65x** | 6.4 | 5.3 | 4.4 | 20.0 |
| respond | small (N=100000) | 35.6 | 71.6 | **2.01x** | 1.7 | 0.4 | 7.6 | 21.6 |
| respond | medium (N=400000) | 141.5 | 284.1 | **2.01x** | 1.8 | 0.6 | 7.6 | 21.6 |
| blur | small (W=1024) | 46.0 | 43.8 | **0.95x** | 2.1 | 0.0 | 3.0 | 20.0 |
| blur | medium (W=2048) | 182.6 | 175.0 | **0.96x** | 8.2 | 5.7 | 3.0 | 20.1 |

**Geometric mean: Goose runs 1.07x the speed of the best Rust row, using 1.17x less memory** (over rows where both use at least 1 MB).

## Every row

### sum (small, N=1000000)

Control: build a flat i32 array, then scan it eight times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose grow-only array | 5.3 | 4.1 | 3.2 | 16 MB |
| rust: rust Vec, collect | 4.8 | 1.2 | 18.6 | |

### sum (medium, N=16000000)

Control: build a flat i32 array, then scan it eight times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose grow-only array | 88.2 | 61.1 | 3.2 | 128 MB |
| rust: rust Vec, collect | 83.9 | 58.9 | 18.6 | |

### push (small, N=1000000)

Push N records while keeping pointers to every 64th one.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose array, typed references | 4.8 | 8.0 | 3.3 | 16 MB |
| rust: rust Vec+reserve, indices | 4.8 | 5.1 | 19.3 | |

### push (medium, N=16000000)

Push N records while keeping pointers to every 64th one.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose array, typed references | 71.1 | 124.4 | 3.3 | 256 MB |
| rust: rust Vec+reserve, indices | 81.0 | 121.8 | 19.3 | |

### strlist (small, N=200000)

Split a text into N words, keep them as a list, scan it four times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose inline strings, owned | 13.2 | 5.2 | 4.1 | 16 MB |
| rust: rust Vec<String> | 20.3 | 7.5 | 20.5 | |
| rust: rust Vec<&str> | 14.3 | 3.9 | 20.3 | |

### strlist (medium, N=2000000)

Split a text into N words, keep them as a list, scan it four times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose inline strings, owned | 128.0 | 53.9 | 4.1 | 64 MB |
| rust: rust Vec<String> | 212.2 | 88.6 | 20.5 | |
| rust: rust Vec<&str> | 144.0 | 54.0 | 20.3 | |

### records (small, N=500000)

Build a log of N variant records, aggregate it four times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose variable enum | 18.3 | 4.7 | 3.9 | 16 MB |
| goose: goose fixed enum | 20.6 | 10.2 | 4.2 | 32 MB |
| rust: rust enum + String | 29.3 | 13.0 | 20.0 | |

### records (medium, N=4000000)

Build a log of N variant records, aggregate it four times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose variable enum | 145.2 | 35.5 | 3.9 | 128 MB |
| goose: goose fixed enum | 159.5 | 80.3 | 4.2 | 256 MB |
| rust: rust enum + String | 234.0 | 123.0 | 20.0 | |

### tree (small, depth=16)

Build a complete binary tree of the given depth, sum it eight times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose pool + typed relative refs | 3.7 | 1.6 | 3.5 | 16 MB |
| rust: rust Box nodes | 6.0 | 0.0 | 18.2 | |
| rust: rust arena + indices | 3.6 | 0.0 | 19.3 | |

### tree (medium, depth=20)

Build a complete binary tree of the given depth, sum it eight times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose pool + typed relative refs | 72.4 | 24.0 | 3.5 | 64 MB |
| rust: rust Box nodes | 119.7 | 30.1 | 18.2 | |
| rust: rust arena + indices | 76.6 | 21.7 | 19.3 | |

### interp (small, depth=16)

Build an expression tree of the given depth, evaluate it eight times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose case functions + relative refs | 1.5 | 0.1 | 3.6 | 16 MB |
| rust: rust enum + Box | 2.2 | 0.0 | 18.6 | |
| rust: rust enum + arena indices | 1.5 | 0.0 | 19.4 | |

### interp (medium, depth=20)

Build an expression tree of the given depth, evaluate it eight times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose case functions + relative refs | 11.9 | 1.9 | 3.6 | 16 MB |
| rust: rust enum + Box | 20.8 | 2.1 | 18.6 | |
| rust: rust enum + arena indices | 13.6 | 6.8 | 19.4 | |

### graph (small, V=100000)

Build adjacency for V vertices and 8V edges in one pass, then BFS from four sources.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose one-pass, typed relative refs | 123.2 | 8.0 | 4.1 | 16 MB |
| goose: goose CSR two-pass | 28.2 | 11.2 | 4.4 | 16 MB |
| rust: rust Vec<Vec> | 66.1 | 4.9 | 20.2 | |
| rust: rust CSR two-pass | 27.5 | 8.0 | 21.0 | |
| rust: rust arena + indices | 172.0 | 5.1 | 20.5 | |

### graph (medium, V=500000)

Build adjacency for V vertices and 8V edges in one pass, then BFS from four sources.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose one-pass, typed relative refs | 1076.6 | 38.5 | 4.2 | 128 MB |
| goose: goose CSR two-pass | 289.0 | 53.9 | 4.4 | 32 MB |
| rust: rust Vec<Vec> | 633.6 | 35.8 | 20.3 | |
| rust: rust CSR two-pass | 302.6 | 51.5 | 21.0 | |
| rust: rust arena + indices | 1266.5 | 36.3 | 20.5 | |

### words (small, N=500000)

Count word frequencies over a text of N words.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose slices + open addressing | 46.1 | 16.9 | 5.7 | 16 MB |
| goose: goose std dictionary | 49.9 | 14.6 | 8.5 | 16 MB |
| rust: rust HashMap<&str> | 57.9 | 10.1 | 25.2 | |
| rust: rust open addressing | 49.1 | 12.2 | 21.5 | |

### words (medium, N=4000000)

Count word frequencies over a text of N words.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose slices + open addressing | 656.1 | 132.4 | 5.7 | 128 MB |
| goose: goose std dictionary | 663.1 | 113.6 | 8.5 | 128 MB |
| rust: rust HashMap<&str> | 1036.9 | 94.7 | 25.2 | |
| rust: rust open addressing | 782.0 | 111.8 | 21.5 | |

### particles (small, N=100000)

Integrate N particles for 200 steps of f32 vector math.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose AoS, elementwise | 36.0 | 2.6 | 3.3 | 16 MB |
| goose: goose AoS, per component | 31.7 | 2.5 | 3.2 | 16 MB |
| rust: rust AoS, elementwise | 36.8 | 0.0 | 18.7 | |
| rust: rust AoS, per component | 32.3 | 0.0 | 18.6 | |

### particles (medium, N=1000000)

Integrate N particles for 200 steps of f32 vector math.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose AoS, elementwise | 573.5 | 23.1 | 3.3 | 64 MB |
| goose: goose AoS, per component | 500.4 | 23.1 | 3.2 | 64 MB |
| rust: rust AoS, elementwise | 521.3 | 20.5 | 18.7 | |
| rust: rust AoS, per component | 505.9 | 20.2 | 18.6 | |

### sexp (small, N=20000)

Generate N s-expression forms, parse them into a tree, walk it four times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose pool + refs, inline text | 137.9 | 24.9 | 8.3 | 32 MB |
| rust: rust enum + Box + String | 303.7 | 50.7 | 28.8 | |
| rust: rust arena + &str | 137.7 | 62.5 | 28.9 | |

### sexp (medium, N=100000)

Generate N s-expression forms, parse them into a tree, walk it four times.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose pool + refs, inline text | 676.0 | 123.1 | 8.3 | 256 MB |
| rust: rust enum + Box + String | 6538.1 | 263.2 | 28.8 | |
| rust: rust arena + &str | 661.3 | 288.0 | 28.9 | |

### lru (small, N=2000000)

An LRU cache of capacity N/8 under N skewed lookups, with an invalidation every 16th operation.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose reusable pool + pool-relative refs | 105.2 | 7.9 | 7.8 | 16 MB |
| rust: rust HashMap + arena | 190.6 | 11.3 | 25.6 | |
| rust: rust open addressing + arena | 119.7 | 9.2 | 21.6 | |

### lru (medium, N=8000000)

An LRU cache of capacity N/8 under N skewed lookups, with an invalidation every 16th operation.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose reusable pool + pool-relative refs | 797.9 | 31.8 | 7.8 | 32 MB |
| rust: rust HashMap + arena | 1374.0 | 41.8 | 25.6 | |
| rust: rust open addressing + arena | 821.7 | 45.4 | 21.6 | |

### scene (small, depth=12)

Build a scene graph of the given depth, then animate it and recompute world transforms for 16 frames.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose pool + inline child refs | 15.3 | 4.6 | 5.2 | 16 MB |
| rust: rust Box children | 24.7 | 2.6 | 20.3 | |
| rust: rust arena + child indices | 15.6 | 9.6 | 20.9 | |

### scene (medium, depth=14)

Build a scene graph of the given depth, then animate it and recompute world transforms for 16 frames.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose pool + inline child refs | 141.6 | 28.4 | 5.2 | 64 MB |
| rust: rust Box children | 274.1 | 29.7 | 20.3 | |
| rust: rust arena + child indices | 149.3 | 57.1 | 20.9 | |

### calc (small, N=100000)

Generate, parse and evaluate N small arithmetic expressions, a third of them malformed.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose local pool + return from | 50.9 | 1.1 | 10.0 | 16 MB |
| rust: rust Box + Result | 95.2 | 0.0 | 22.6 | |
| rust: rust arena + Result | 51.6 | 0.0 | 22.8 | |

### calc (medium, N=400000)

Generate, parse and evaluate N small arithmetic expressions, a third of them malformed.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose local pool + return from | 203.9 | 1.1 | 10.0 | 16 MB |
| rust: rust Box + Result | 377.6 | 0.0 | 22.6 | |
| rust: rust arena + Result | 204.8 | 0.1 | 22.8 | |

### bintrees (small, depth=15)

The Benchmarks Game binary-trees: build, check and discard many trees up to the given depth.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose local pool per tree | 48.1 | 2.1 | 4.4 | 16 MB |
| rust: rust Box | 173.3 | 0.0 | 18.8 | |
| rust: rust Vec arena | 30.8 | 0.0 | 19.8 | |

### bintrees (medium, depth=17)

The Benchmarks Game binary-trees: build, check and discard many trees up to the given depth.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose local pool per tree | 215.8 | 6.4 | 4.4 | 16 MB |
| rust: rust Box | 798.9 | 5.6 | 18.9 | |
| rust: rust Vec arena | 140.3 | 5.3 | 20.0 | |

### respond (small, N=100000)

Build and render N JSON responses, each a DTO holding a name and a list of line items.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose inline DTO | 54.5 | 4.5 | 10.6 | 16 MB |
| goose: goose streaming | 35.6 | 1.7 | 7.6 | 16 MB |
| rust: rust DTO + String | 123.9 | 0.7 | 23.0 | |
| rust: rust streaming | 71.6 | 0.4 | 21.6 | |

### respond (medium, N=400000)

Build and render N JSON responses, each a DTO holding a name and a list of line items.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose inline DTO | 219.5 | 4.8 | 10.6 | 16 MB |
| goose: goose streaming | 141.5 | 1.8 | 7.6 | 16 MB |
| rust: rust DTO + String | 492.7 | 0.6 | 23.0 | |
| rust: rust streaming | 284.1 | 0.6 | 21.6 | |

### blur (small, W=1024)

16 passes of a 3x3 blur over a WxW 8-bit image.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose flat indexing | 46.3 | 2.3 | 3.0 | 16 MB |
| goose: goose row slices | 46.0 | 2.1 | 3.0 | 16 MB |
| rust: rust flat indexing | 61.7 | 0.0 | 20.0 | |
| rust: rust row slices | 43.8 | 0.0 | 20.0 | |

### blur (medium, W=2048)

16 passes of a 3x3 blur over a WxW 8-bit image.

| implementation | ms | MB used | code KB | Goose reserve per stack |
|---|---:|---:|---:|---:|
| goose: goose flat indexing | 182.6 | 8.2 | 3.0 | 16 MB |
| goose: goose row slices | 182.6 | 8.3 | 3.0 | 16 MB |
| rust: rust flat indexing | 242.9 | 5.7 | 20.0 | |
| rust: rust row slices | 175.0 | 5.7 | 20.1 | |

