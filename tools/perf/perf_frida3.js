// perf_frida3.js — throttle experiment v2 (correct thiscall ABI):
// skip every 2nd FUN_0044bdd0 (battle update), cache last return; stats + histogram + backtrace.
let mod = Process.findModuleByName('LETHALDREAMS.EXE');
if (!mod) {
  for (const m of Process.enumerateModules()) {
    if (m.name.toLowerCase() === 'lethal dreams.exe' || m.name.toLowerCase() === 'lethaldreams.exe') { mod = m; break; }
  }
}
if (!mod) { send({ type: 'error', msg: 'module not found' }); } else {
const B = (o) => mod.base.add(o);

const st = { calls: 0, skipped: 0, totalMs: 0, maxMs: 0, f45_calls: 0, f45_ms: 0, f45_max: 0 };
const hist = {};
let lastRet = 0, callIdx = 0, btN = 0;

const origBDD = new NativeFunction(B(0x4bdd0), 'uint', ['pointer', 'char', 'uint', 'pointer'], 'thiscall');
Interceptor.replace(B(0x4bdd0), new NativeCallback(function (th, p1, p2, p3) {
  callIdx++; st.calls++;
  if (btN < 3) {
    btN++;
    try {
      const bt = Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 12)
        .map(DebugSymbol.fromAddress).map(String);
      send({ type: 'bt', n: btN, frames: bt });
    } catch (e) { send({ type: 'bt_err', e: '' + e }); }
  }
  if (callIdx % 2 === 0) { st.skipped++; return lastRet; }
  const t0 = Date.now();
  let r = lastRet;
  try { r = origBDD(th, p1, p2, p3); } catch (e) { send({ type: 'orig_err', e: '' + e }); }
  const dt = Date.now() - t0;
  st.totalMs += dt; if (dt > st.maxMs) st.maxMs = dt;
  lastRet = r;
  return r;
}, 'uint', ['pointer', 'char', 'uint', 'pointer'], 'thiscall'));

Interceptor.attach(B(0x5eee0), {
  onEnter() { this.t0 = Date.now(); },
  onLeave() {
    const dt = Date.now() - this.t0;
    st.f45_calls++; st.f45_ms += dt; if (dt > st.f45_max) st.f45_max = dt;
    const b = dt <= 2 ? '1-2' : dt <= 5 ? '2-5' : dt <= 10 ? '5-10' : dt <= 20 ? '10-20' : dt <= 50 ? '20-50' : '50+';
    hist[b] = (hist[b] || 0) + 1;
  }
});
send({ type: 'hooked', name: 'FUN_0044bdd0_throttle2_thiscall' });

setInterval(() => {
  const out = {
    calls: st.calls, skipped: st.skipped, totalMs: st.totalMs, maxMs: st.maxMs,
    f45_calls: st.f45_calls, f45_ms: st.f45_ms, f45_max: st.f45_max,
    hist: Object.assign({}, hist),
  };
  st.calls = 0; st.skipped = 0; st.totalMs = 0; st.maxMs = 0;
  st.f45_calls = 0; st.f45_ms = 0; st.f45_max = 0;
  for (const k in hist) delete hist[k];
  send({ type: 'stats', data: out });
}, 2000);
}