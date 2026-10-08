/* A Goose runtime for wasm32-unknown-unknown: no libc, no WASI, no
   Emscripten. It stands in for the object `goose --emit-runtime` writes, and
   defines what a generated program unit declares extern (runtime.h with
   GS_SEPARATE_RUNTIME): aborts, data stack regions, text forms and printing,
   plus the few libc functions that unit calls.

   The host (host/run.mjs, or a browser page) supplies four imports in module
   "env": gs_host_write, gs_host_exit, gs_host_fmt_f64 and gs_host_now_ms.

   Data stacks. The native runtime reserves address space per stack and
   commits pages on touch, with an unmapped guard gap after each one. Wasm
   linear memory is one contiguous range with no page protection, so here a
   region is a memory.grow of the full reservation: the engine maps zero
   pages lazily, so untouched reservation costs address space, not RAM. What
   wasm cannot give is the guard gap. Running past the last region traps
   (out-of-bounds access), but an overrun of an earlier region would land in
   the next one, so a program must stay inside GS_STACK_RESERVE per stack;
   bench.py sizes it per benchmark with headroom. */

#include <stdint.h>
#include <stddef.h>

#ifndef GS_RT_START_NAME
#error "build with -DGS_RT_START_NAME=gs_rt_start_<version> (see build.py)"
#endif

#define GS_IMPORT(name) __attribute__((import_module("env"), import_name(#name)))
#define GS_EXPORT(name) __attribute__((export_name(#name)))
#define GS_NORETURN __attribute__((noreturn))

GS_IMPORT(gs_host_write) void gs_host_write(int32_t fd, const uint8_t *p, int32_t len);
GS_IMPORT(gs_host_exit) void gs_host_exit(int32_t code);
GS_IMPORT(gs_host_fmt_f64) int32_t gs_host_fmt_f64(double v, int32_t f32, uint8_t *dst);
GS_IMPORT(gs_host_now_ms) double gs_host_now_ms(void);

/* Laid out as the generated code lays them out (CodeGen::EmitCoreTypes). */
typedef struct { uint8_t *top; } gs_stack;
#pragma pack(push, 1)
typedef struct { uint8_t *base; int64_t len; } gs_rhdr;
typedef struct { gs_rhdr *hdr; gs_stack *stk; } gs_rref;
typedef struct { uint8_t *data; int64_t len; } sl_u8;
#pragma pack(pop)

/* ---------------------------------------------------------------------------
   libc subset. With bulk-memory, clang turns these builtins into single
   memory.copy / memory.fill instructions rather than calls back here. */

void *memcpy(void *restrict d, const void *restrict s, size_t n) {
    __builtin_memcpy(d, s, n);
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    __builtin_memmove(d, s, n);
    return d;
}
void *memset(void *d, int c, size_t n) {
    __builtin_memset(d, c, n);
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (uint8_t)c) return (void *)(p + i);
    return NULL;
}
size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

/* ---------------------------------------------------------------------------
   Memory. Linear memory only grows, at its end, in 64 KiB pages; the heap and
   the data stack regions both take their memory that way. The heap is a bump
   allocator: the generated code allocates one stack block at startup and
   nothing else, so free has nothing worth doing. */

#define GS_PAGE 65536u
extern uint8_t __heap_base;
static uintptr_t gs_heap_cur, gs_heap_end;

GS_NORETURN void gs_panic(const char *msg);

/* n bytes (a multiple of the page size) at the end of linear memory. */
static uint8_t *gs_grow(size_t n) {
    size_t old = __builtin_wasm_memory_grow(0, n / GS_PAGE);
    if (old == (size_t)-1) return NULL;
    return (uint8_t *)(old * GS_PAGE);
}

void *malloc(size_t n) {
    if (!gs_heap_cur) {
        gs_heap_cur = (uintptr_t)&__heap_base;
        gs_heap_end = __builtin_wasm_memory_size(0) * GS_PAGE;
    }
    n = (n + 15) & ~(size_t)15;
    gs_heap_cur = (gs_heap_cur + 15) & ~(uintptr_t)15;
    if (gs_heap_end - gs_heap_cur < n) {
        size_t want = (n + GS_PAGE - 1) & ~(size_t)(GS_PAGE - 1);
        uint8_t *p = gs_grow(want);
        if (!p) return NULL;
        /* Contiguous with the old end unless a region was reserved since. */
        if ((uintptr_t)p != gs_heap_end) gs_heap_cur = (uintptr_t)p;
        gs_heap_end = (uintptr_t)p + want;
    }
    void *r = (void *)gs_heap_cur;
    gs_heap_cur += n;
    return r;
}

/* Fresh memory from memory.grow is zero, but bump memory below the initial
   end may not be after a free; zero it either way. */
void *calloc(size_t count, size_t size) {
    size_t n = count * size;
    void *p = malloc(n);
    if (p) __builtin_memset(p, 0, n);
    return p;
}

void free(void *p) { (void)p; }

void *realloc(void *p, size_t n) {
    (void)p;
    (void)n;
    gs_panic("realloc is not supported by the wasm runtime");
}

/* ---------------------------------------------------------------------------
   Output: buffered, flushed when full, on exit, and when the host calls
   gs_wasm_flush after the program returns. fd 1 is stdout, 2 stderr. */

static uint8_t gs_obuf[1 << 16];
static int32_t gs_olen;

GS_EXPORT(gs_wasm_flush) void gs_wasm_flush(void) {
    if (gs_olen) gs_host_write(1, gs_obuf, gs_olen);
    gs_olen = 0;
}

static void gs_out(const uint8_t *p, int64_t n) {
    if (n > (int64_t)sizeof(gs_obuf) - gs_olen) {
        gs_wasm_flush();
        if (n > (int64_t)sizeof(gs_obuf)) {
            gs_host_write(1, p, (int32_t)n);
            return;
        }
    }
    __builtin_memcpy(gs_obuf + gs_olen, p, (size_t)n);
    gs_olen += (int32_t)n;
}

GS_NORETURN void exit(int code) {
    gs_wasm_flush();
    gs_host_exit(code);
    __builtin_trap();
}

GS_NORETURN void abort(void) { exit(134); }

/* Diagnostics go to stderr unbuffered; they are rare and must not be lost. */
static void gs_err(const char *s) { gs_host_write(2, (const uint8_t *)s, (int32_t)strlen(s)); }

/* ---------------------------------------------------------------------------
   Text forms (spec 3.7). Integers here; floats through the host, which formats
   them exactly as the native runtime does (%.15g, else %.17g if that does not
   read back, two-digit exponents, and a trailing .0 on whole numbers). */

#define GS_FMT_MAX 32

int64_t gs_fmt_u64(uint8_t *dst, uint64_t v) {
    uint8_t tmp[24];
    int n = 0;
    do { tmp[n++] = (uint8_t)('0' + v % 10); v /= 10; } while (v);
    for (int i = 0; i < n; i++) dst[i] = tmp[n - 1 - i];
    return n;
}

int64_t gs_fmt_i64(uint8_t *dst, int64_t v) {
    if (v >= 0) return gs_fmt_u64(dst, (uint64_t)v);
    dst[0] = '-';
    return 1 + gs_fmt_u64(dst + 1, 0 - (uint64_t)v);
}

int64_t gs_fmt_f64(uint8_t *dst, double v) { return gs_host_fmt_f64(v, 0, dst); }
int64_t gs_fmt_f32(uint8_t *dst, float v) { return gs_host_fmt_f64((double)v, 1, dst); }

int64_t gs_fmt_bool(uint8_t *dst, int64_t v) {
    __builtin_memcpy(dst, v ? "true" : "false", v ? 4 : 5);
    return v ? 4 : 5;
}

int64_t gs_fmt_quoted(uint8_t *dst, const uint8_t *s, int64_t n) {
    uint8_t *d = dst;
    *d++ = '"';
    for (int64_t i = 0; i < n; i++) {
        uint8_t c = s[i];
        switch (c) {
            case '"': *d++ = '\\'; *d++ = '"'; break;
            case '\\': *d++ = '\\'; *d++ = '\\'; break;
            case '\n': *d++ = '\\'; *d++ = 'n'; break;
            case '\r': *d++ = '\\'; *d++ = 'r'; break;
            case '\t': *d++ = '\\'; *d++ = 't'; break;
            default: *d++ = c; break;
        }
    }
    *d++ = '"';
    return (int64_t)(d - dst);
}

void gs_out_int(int64_t v) { uint8_t b[GS_FMT_MAX]; gs_out(b, gs_fmt_i64(b, v)); }
void gs_out_uint(uint64_t v) { uint8_t b[GS_FMT_MAX]; gs_out(b, gs_fmt_u64(b, v)); }
void gs_out_flt(double v) { uint8_t b[GS_FMT_MAX]; gs_out(b, gs_fmt_f64(b, v)); }
void gs_out_f32(float v) { uint8_t b[GS_FMT_MAX]; gs_out(b, gs_fmt_f32(b, v)); }
void gs_out_bool(int64_t v) { uint8_t b[8]; gs_out(b, gs_fmt_bool(b, v)); }
void gs_out_bytes(const uint8_t *p, int64_t len) { gs_out(p, len); }
void gs_out_nl(void) { gs_out((const uint8_t *)"\n", 1); }

/* ---------------------------------------------------------------------------
   Aborts (spec 9.3), worded as runtime_impl.h words them. */

static const char *gs_errmsgs[] = {
    "limited array capacity exceeded",
    "slice bounds out of range",
    "relative reference offset overflow",
    "assert failed",
    "invalid capacity",
    "resize growth requires a fill value",
    "resize to a negative length",
    "pop on empty array",
    "thread_wait on an unknown thread id",
    "thread_wait on the current thread",
    "corrupt ADT tag",
    "serialization needs a little-endian host (not supported yet)",
    "invalid slice length",
    "slice not from this pool",
    "non-null relative reference encodes as null",
    "too many data stacks (deep call nesting?)",
};

static void gs_err_i64(int64_t v) {
    uint8_t b[GS_FMT_MAX + 1];
    b[gs_fmt_i64(b, v)] = 0;
    gs_err((const char *)b);
}

static GS_NORETURN void gs_die_at(const char *file, int line) {
    gs_err(" (");
    gs_err(file);
    gs_err(":");
    gs_err_i64(line);
    gs_err(")\n");
    exit(1);
}

GS_NORETURN void gs_panic(const char *msg) {
    gs_err("goose runtime error: ");
    gs_err(msg);
    gs_err("\n");
    exit(1);
}

GS_NORETURN void gs_abort(int err, const char *file, int line) {
    gs_err("goose runtime error: ");
    gs_err(gs_errmsgs[err]);
    gs_die_at(file, line);
}

GS_NORETURN void gs_abort_msg(const uint8_t *msg, int64_t len, const char *file, int line) {
    gs_err("goose runtime error: ");
    gs_host_write(2, msg, (int32_t)len);
    gs_die_at(file, line);
}

GS_NORETURN void gs_exit(int64_t code) { exit((int)code); }

GS_NORETURN void gs_idxfail(int64_t i, int64_t n, const char *file, int line) {
    gs_err("goose runtime error: index ");
    gs_err_i64(i);
    gs_err(" out of bounds (length ");
    gs_err_i64(n);
    gs_err(")");
    gs_die_at(file, line);
}

GS_NORETURN void gs_divfail(const char *file, int line) {
    gs_err("goose runtime error: division by zero");
    gs_die_at(file, line);
}

GS_NORETURN void gs_divovf(const char *file, int line) {
    gs_err("goose runtime error: integer overflow in division");
    gs_die_at(file, line);
}

GS_NORETURN void gs_ovf(int64_t a, const char *op, int64_t b, const char *type,
                        const char *file, int line) {
    gs_err("goose runtime error: integer overflow (debug): ");
    gs_err_i64(a);
    gs_err(" ");
    gs_err(op);
    gs_err(" ");
    gs_err_i64(b);
    gs_err(" at ");
    gs_err(type);
    gs_die_at(file, line);
}

GS_NORETURN void gs_ovf_neg(int64_t a, const char *type, const char *file, int line) {
    gs_err("goose runtime error: integer overflow (debug): -(");
    gs_err_i64(a);
    gs_err(") at ");
    gs_err(type);
    gs_die_at(file, line);
}

GS_NORETURN void gs_asfail_i(const char *why, int64_t v, const char *type, const char *file,
                             int line) {
    gs_err("goose runtime error: as conversion ");
    gs_err(why);
    gs_err(" (debug): ");
    gs_err_i64(v);
    gs_err(" as ");
    gs_err(type);
    gs_die_at(file, line);
}

GS_NORETURN void gs_asfail_u(const char *why, uint64_t v, const char *type, const char *file,
                             int line) {
    uint8_t b[GS_FMT_MAX + 1];
    b[gs_fmt_u64(b, v)] = 0;
    gs_err("goose runtime error: as conversion ");
    gs_err(why);
    gs_err(" (debug): ");
    gs_err((const char *)b);
    gs_err(" as ");
    gs_err(type);
    gs_die_at(file, line);
}

GS_NORETURN void gs_asfail_f(const char *why, double d, int f32, const char *type,
                             const char *file, int line) {
    uint8_t b[GS_FMT_MAX + 1];
    b[f32 ? gs_fmt_f32(b, (float)d) : gs_fmt_f64(b, d)] = 0;
    gs_err("goose runtime error: as conversion ");
    gs_err(why);
    gs_err(" (debug): ");
    gs_err((const char *)b);
    gs_err(" as ");
    gs_err(type);
    gs_die_at(file, line);
}

/* ---------------------------------------------------------------------------
   Data stack regions. */

static int gs_argc;
static char **gs_argv;
static int64_t gs_maxregions, gs_nregions;
static size_t gs_region_size;
static uint64_t gs_region_bytes_reserved;
#define GS_MAX_TRACKED 64
static uint8_t *gs_region_base[GS_MAX_TRACKED];

/* How much linear memory the data stack regions reserved, for the host's
   memory report. */
GS_EXPORT(gs_wasm_reserved) double gs_wasm_reserved(void) { return (double)gs_region_bytes_reserved; }

/* The most any one data stack held, in bytes: the last nonzero byte of each
   region, which memory.grow handed over zeroed. A lower bound (a stack whose
   last bytes were zeros reads short), which is why bench.py reserves twice
   it. Called after the program, by a probe run, never inside a timing. */
GS_EXPORT(gs_wasm_highwater) double gs_wasm_highwater(void) {
    size_t most = 0;
    int64_t n = gs_nregions < GS_MAX_TRACKED ? gs_nregions : GS_MAX_TRACKED;
    for (int64_t r = 0; r < n; r++) {
        const uint64_t *base = (const uint64_t *)gs_region_base[r];
        size_t w = gs_region_size / 8;
        while (w > 0 && base[w - 1] == 0) w--;
        if (w * 8 > most) most = w * 8;
    }
    return (double)most;
}

void GS_RT_START_NAME(int argc, char **argv, int64_t maxregions, uint64_t reserve, uint64_t gap) {
    (void)gap;  /* No page protection in linear memory: see the top of this file. */
    gs_argc = argc;
    gs_argv = argv;
    gs_maxregions = maxregions;
    gs_region_size = ((size_t)reserve + GS_PAGE - 1) & ~(size_t)(GS_PAGE - 1);
}

#ifndef GS_REGION_COLOR
#define GS_REGION_COLOR 1088
#endif

uint8_t *gs_reserve_region(void) {
    if (gs_nregions == gs_maxregions) gs_panic("too many data stack regions");
    uint8_t *p = gs_grow(gs_region_size + GS_PAGE);
    if (!p) gs_panic("cannot reserve data stack memory (wasm32 linear memory is at most 4 GiB)");
    /* Cache coloring: start each region at a different offset within its
       first page. Regions are page aligned, so without this the same element
       of two arrays on two stacks has the same low 12 address bits, and a
       loop that reads one and writes the other (blur's src and dst) stalls
       on 4K aliasing between its loads and stores. */
    p += (size_t)(gs_nregions * GS_REGION_COLOR) % (GS_PAGE - 64) & ~(size_t)63;
    if (gs_nregions < GS_MAX_TRACKED) gs_region_base[gs_nregions] = p;
    gs_nregions++;
    gs_region_bytes_reserved += gs_region_size + GS_PAGE;
    return p;
}

/* Linear memory cannot shrink; a finished thread program would leave its
   regions behind, but there are no threads here. */
void gs_release_regions(void) { gs_nregions = 0; }

/* ---------------------------------------------------------------------------
   Threads: none. wasm32-unknown-unknown has no threads without shared memory
   and a host that spawns workers, so a program that spawns one stops here. */

typedef struct gs_qnode { struct gs_qnode *next; int64_t size; } gs_qnode;
typedef struct gs_qstate *gs_queue;

int64_t gs_hardware_threads(void) { return 1; }
int64_t gs_thread_start(void (*run)(void (*)(uint8_t *), uint8_t *), void (*entry)(uint8_t *),
                        const void *args, int64_t argsize) {
    (void)run; (void)entry; (void)args; (void)argsize;
    gs_panic("threads are not supported by the wasm runtime");
}
void gs_thread_wait(int64_t id, const char *file, int line) {
    (void)id; (void)file; (void)line;
    gs_panic("threads are not supported by the wasm runtime");
}
void gs_qinit(gs_queue *q) { *q = 0; }
void gs_qput(gs_queue *q, const void *data, int64_t size) {
    (void)q; (void)data; (void)size;
    gs_panic("queues are not supported by the wasm runtime");
}
gs_qnode *gs_qget(gs_queue *q) { (void)q; gs_panic("queues are not supported by the wasm runtime"); }
gs_qnode *gs_qpoll(gs_queue *q) { (void)q; return NULL; }

/* ---------------------------------------------------------------------------
   The OS layer behind stdlib/os.goose (spec 7.10): output, arguments and
   clocks work; there is no file system, environment or stdin. */

static void gs_bld_append(gs_rref b, const void *p, int64_t n) {
    if (n <= 0) return;
    __builtin_memcpy(b.stk->top, p, (size_t)n);
    b.stk->top += n;
    b.hdr->len += n;
}

uint8_t gs_os_read_file(sl_u8 path, gs_rref out) { (void)path; (void)out; return 0; }
uint8_t gs_os_write_file(sl_u8 path, sl_u8 data) { (void)path; (void)data; return 0; }
uint8_t gs_os_write_file_atomic(sl_u8 path, sl_u8 data) { (void)path; (void)data; return 0; }
uint8_t gs_os_append_file(sl_u8 path, sl_u8 data) { (void)path; (void)data; return 0; }
uint8_t gs_os_file_exists(sl_u8 path) { (void)path; return 0; }
uint8_t gs_os_remove_file(sl_u8 path) { (void)path; return 0; }
uint8_t gs_os_rename_file(sl_u8 from, sl_u8 to) { (void)from; (void)to; return 0; }
uint8_t gs_os_make_dir(sl_u8 path) { (void)path; return 0; }
uint8_t gs_os_is_dir(sl_u8 path) { (void)path; return 0; }
uint8_t gs_os_remove_dir(sl_u8 path) { (void)path; return 0; }
uint8_t gs_os_list_dir(sl_u8 path, gs_rref out) { (void)path; (void)out; return 0; }
void gs_os_write_stdout(sl_u8 s) { gs_out(s.data, s.len); }
void gs_os_write_stderr(sl_u8 s) { gs_host_write(2, s.data, (int32_t)s.len); }
void gs_os_flush_stdout(void) { gs_wasm_flush(); }
uint8_t gs_os_read_line(gs_rref out) { (void)out; return 0; }
void gs_os_read_stdin(gs_rref out) { (void)out; }
int64_t gs_os_arg_count(void) { return gs_argc; }
void gs_os_arg(int64_t i, gs_rref out) {
    if (i < 0 || i >= gs_argc) return;
    gs_bld_append(out, gs_argv[i], (int64_t)strlen(gs_argv[i]));
}
uint8_t gs_os_getenv(sl_u8 name, gs_rref out) { (void)name; (void)out; return 0; }
int64_t gs_os_time_ns(void) { return (int64_t)(gs_host_now_ms() * 1e6); }
int64_t gs_os_clock_ns(void) { return (int64_t)(gs_host_now_ms() * 1e6); }
void gs_os_sleep_ms(int64_t ms) { (void)ms; }
uint64_t gs_os_random_u64(void) {
    static uint64_t s = 0x9e3779b97f4a7c15ull;
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return s;
}
