// perf_frida8.js — validate the loop-cap fix: inside FUN_0045e0a0, FUN_0045ce80 returns NULL
// after 32 nested calls (simulates the exe patch: cap loop iterations -> exit with "not found").
let mod = Process.findModuleByName('LETHALDREAMS.EXE');
if (!mod) {
  for (const m of Process.enumerateModules()) {
    if (m.name.toLowerCase() === 'lethal dreams.exe' || m.name.toLowerCase() === 'lethaldreams.exe') { mod = m; break; }
  }
}
if (!mod) { send({ type: 'error', msg: 'module not found' }); } else {
const B = (o) => mod.base.add(o);
const CAP = 32;
const st = { e0a0: 0, e0a0_ms: 0, e0a0_max: 0, capped: 0 };
let e0a0Depth = 0, ce80InE0a0 = 0;

const origCE80 = new NativeFunction(B(0x5ce80), 'int', ['pointer', 'int'], 'thiscall');
Interceptor.replace(B(0x5ce80), new NativeCallback(function (th, p1) {
  if (e0a0Depth > 0) {
    ce80InE0a0++;
    if (ce80InE0a0 > CAP) { st.capped++; return 0; }
  }
  return origCE80(th, p1);
}, 'int', ['pointer', 'int'], 'thiscall'));

Interceptor.attach(B(0x5e0a0), {
  onEnter() { this.t0 = Date.now(); e0a0Depth++; if (e0a0Depth === 1) ce80InE0a0 = 0; },
  onLeave() {
    const dt = Date.now() - this.t0;
    st.e0a0++; st.e0a0_ms += dt; if (dt > st.e0a0_max) st.e0a0_max = dt;
    e0a0Depth--;
  }
});
send({ type: 'hooked', name: 'capvalidate32' });

setInterval(() => {
  const out = { e0a0: st.e0a0, e0a0_ms: st.e0a0_ms, e0a0_max: st.e0a0_max, capped: st.capped };
  st.e0a0 = 0; st.e0a0_ms = 0; st.e0a0_max = 0; st.capped = 0;
  send({ type: 'stats', data: out });
}, 2000);
}