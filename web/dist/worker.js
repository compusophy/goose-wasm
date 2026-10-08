// Runs benchmark modules for the page, off the main thread. One message per
// implementation: { id, url, lang, reps }. Each rep is a fresh instance (so a
// fresh linear memory), after one warm-up run that also lets the engine tier
// the module's code up; the reply carries the best time and the output, which
// the page checks against the reference checksums.

import { makeHost, runProgram } from "./imports.js";

const modules = new Map();

async function compile(url) {
    if (!modules.has(url)) {
        const bytes = await (await fetch(url)).arrayBuffer();
        modules.set(url, await WebAssembly.compile(bytes));
    }
    return modules.get(url);
}

function runOnce(module, lang) {
    const host = makeHost(() => performance.now());
    const instance = new WebAssembly.Instance(module, { env: host.env });
    host.attach(instance);
    let error = "";
    const t0 = performance.now();
    const code = runProgram(instance, lang, (e) => { error = e; });
    const ms = performance.now() - t0;
    return { ms, code, out: host.out(), err: host.err() || error, memory: host.memoryBytes() };
}

self.onmessage = async ({ data }) => {
    const { id, url, lang, reps } = data;
    try {
        const module = await compile(url);
        let r = runOnce(module, lang);
        if (r.code !== 0) throw new Error(r.err || `exit ${r.code}`);
        let best = Infinity;
        const times = [];
        for (let i = 0; i < reps; i++) {
            r = runOnce(module, lang);
            if (r.code !== 0) throw new Error(r.err || `exit ${r.code}`);
            times.push(r.ms);
            best = Math.min(best, r.ms);
        }
        self.postMessage({ id, ok: true, ms: best, times, out: r.out });
    } catch (e) {
        self.postMessage({ id, ok: false, error: String(e.message || e) });
    }
};
