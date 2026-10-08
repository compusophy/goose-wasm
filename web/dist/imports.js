// The host side of both runtimes: what rt/goose_wasm_rt.c and
// rt/rust_prelude.rs import from module "env". Shared by the Node runner
// (host/run.mjs) and the browser demo (web/worker.js), so a benchmark sees
// the same imports wherever it runs.

export class Exit extends Error {
    constructor(code) {
        super(`exit ${code}`);
        this.code = code;
    }
}

// %.<p>g, as C prints it.
export function fmtG(v, p) {
    if (v === 0) return Object.is(v, -0) ? "-0" : "0";
    const [mant, ex] = v.toExponential(p - 1).split("e");
    const x = parseInt(ex, 10);
    const strip = (s) => (s.includes(".") ? s.replace(/0+$/, "").replace(/\.$/, "") : s);
    if (x < -4 || x >= p) {
        const ax = Math.abs(x);
        return strip(mant) + "e" + (x < 0 ? "-" : "+") + (ax < 10 ? "0" : "") + ax;
    }
    return strip(v.toFixed(p - 1 - x));
}

// runtime_impl.h's gs_fmt_f64 / gs_fmt_f32, so float output matches native:
// the shortest of %.15g / %.17g that reads back, and 1.0 rather than 1.
export function fmtFloat(v, f32) {
    if (Number.isNaN(v)) return "nan";
    if (v === Infinity) return "inf";
    if (v === -Infinity) return "-inf";
    if (f32) {
        const sub = Math.abs(v) < 1.17549435e-38;
        for (let p = sub ? 1 : 6; p <= 9; p++) {
            const r = parseFloat(fmtG(v, p));
            if (Math.fround(r) === v) { v = r; break; }
        }
    }
    let s = fmtG(v, 15);
    if (parseFloat(s) !== v) s = fmtG(v, 17);
    if (!/[.ein]/.test(s)) s += ".0";
    return s;
}

// A fresh host for one instance: `env` to instantiate with, then `attach`
// the instance's memory before running it.
export function makeHost(now) {
    let memory = null;
    const out = [];
    const err = [];
    const decoder = new TextDecoder();
    const encoder = new TextEncoder();
    const view = (p, n) => new Uint8Array(memory.buffer, p, n);
    const env = {
        // Goose runtime (rt/goose_wasm_rt.c)
        gs_host_write(fd, p, n) { (fd === 2 ? err : out).push(decoder.decode(view(p, n).slice())); },
        gs_host_exit(code) { throw new Exit(code); },
        gs_host_fmt_f64(v, f32, dst) {
            const s = fmtFloat(v, f32);
            view(dst, s.length).set(encoder.encode(s));
            return s.length;
        },
        gs_host_now_ms: now,
        // Rust prelude (rt/rust_prelude.rs)
        rs_host_write(p, n) { out.push(decoder.decode(view(p, n).slice())); },
    };
    return {
        env,
        attach(instance) { memory = instance.exports.memory; },
        out: () => out.join(""),
        err: () => err.join(""),
        memoryBytes: () => memory.buffer.byteLength,
    };
}

// Runs an instance's program to completion: the Goose C main (renamed
// gs_c_main) or Rust's run(). Returns the exit code.
export function runProgram(instance, lang, err) {
    try {
        if (lang === "goose") {
            instance.exports.gs_c_main(0, 0);
            instance.exports.gs_wasm_flush();
        } else {
            instance.exports.run();
        }
        return 0;
    } catch (e) {
        if (e instanceof Exit) return e.code;
        if (err) err(String(e));
        return 101;
    }
}
