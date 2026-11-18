// perf_frida7.js — per FUN_0045e0a0 call: nested FUN_0045ce80 count (loop iterations) + duration.
let mod = Process.findModuleByName('LETHALDREAMS.EXE');
if (!mod) {
  for (const m of Process.enumerateModules()) {
    if (m.name.toLowerCase() === 'lethal dreams.exe' || m.name.toLowerCase() === 'lethaldreams.exe') { mod = m; break; }
  }
}
if (!mod) { send({ type: 'error', msg: 'module not found' }); } else {
const B = (o) => mod.base.add(o);
const st = { e0a0: 0, e0a0_ms: 0, e0a0_max: 0, iterSum: 0, iterMax: 0, heavyIters: 0 };
let depth = 0, curIters = 0;

Interceptor.attach(B(0x5ce80), {
  onEnter() { if (depth > 0) curIters++; }
});
Interceptor.attach(B(0x5e0a0), {
  onEnter() {
    this.t0 = Date.now();
    depth++;
    if (depth === 1) curIters = 0;
    this.outer = depth === 1;
  },
  onLeave() {
    const dt = Date.now() - this.t0;
    st.e0a0++; st.e0a0_ms += dt; if (dt > st.e0a0_max) st.e0a0_max = dt;
    if (this.outer) {
      st.iterSum += curIters; if (curIters > st.iterMax) st.iterMax = curIters;
      if (curIters > 20) st.heavyIters++;
    }
    depth--;
  }
});
send({ type: 'hooked', name: 'iterdiag' });
setInterval(() => {
  const out = {
    e0a0: st.e0a0, e0a0_ms: st.e0a0_ms, e0a0_max: st.e0a0_max,
    avgIters: st.e0a0 ? Math.round(st.iterSum / st.e0a0) : 0, iterMax: st.iterMax,
    heavyIters: st.heavyIters,
  };
  st.e0a0 = 0; st.e0a0_ms = 0; st.e0a0_max = 0; st.iterSum = 0; st.iterMax = 0; st.heavyIters = 0;
  send({ type: 'stats', data: out });
}, 2000);
}