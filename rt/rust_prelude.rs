// --- wasm prelude (wasm-bench/rt/rust_prelude.rs), spliced in by bench.py ---
// wasm32-unknown-unknown has no stdout: std's println! writes nowhere. This
// println! shadows it (a macro_rules! in textual scope wins over the std
// prelude, and the scope reaches into `mod bench;` declared below), and hands
// each line to the same host the Goose modules print through. `run` is the
// entry point the host calls; main's drops all happen inside it.
#[link(wasm_import_module = "env")]
extern "C" {
    fn rs_host_write(p: *const u8, n: usize);
}
pub fn __wasm_write(s: &str) {
    unsafe { rs_host_write(s.as_ptr(), s.len()) }
}
#[allow(unused_macros)]
macro_rules! println {
    () => { $crate::__wasm_write("\n") };
    ($($arg:tt)*) => {{
        let mut s = format!($($arg)*);
        s.push('\n');
        $crate::__wasm_write(&s);
    }};
}
#[no_mangle]
pub extern "C" fn run() {
    main();
}
// --- end of wasm prelude ---
