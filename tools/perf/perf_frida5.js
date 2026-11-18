// perf_frida5.js — diagnostic: FUN_0045eee0 call duration vs target-list length (this+0x6c).
let mod = Process.findModuleByName('LETHALDREAMS.EXE');
if (!mod) {
  for (const m of Process.enumerateModules()) {
    if (m.name.toLowerCase() === 'lethal dreams.exe' || m.name.toLowerCase() === 'lethaldreams.exe') { mod = m; break; }
  }
}
if (!mod) { send({ type: 'error', msg: 'module not found' }); } else {
const B = (o) => mod.base.add(o);

const st = {
  calls: 0, totalMs: 0, maxMs: 0, heavy: 0, heavyMs: 0,
  nSum: 0, nMax: 0, nHeavySum: 0,
  e0a0_calls: 0, e0a0_ms: 0,
  nHist: {}, durHist: {},
};

function bucketN(n) { return n <= 0 ? '0' : n <= 5 ? '1-5' : n <= 20 ? '6-20' : n <= 100 ? '21-100' : '100+'; }
function bucketD(dt) { return dt <= 2 ? '1-2' : dt <= 5 ? '2-5' : dt <= 10 ? '5-10' : dt <= 20 ? '10-20' : dt <= 50 ? '20-50' : '50+'; }

Interceptor.attach(B(0x5eee0), {
  onEnter() {
    this.t0 = Date.now();
    let n = -1;
    try {
      const listPtr = this.context.ecx.add(0x6c).readPointer();
      let node = listPtr.readPointer();
      n = 0;
      while (!node.isNull() && n < 200000) {
        if (node.equals(listPtr)) break;
        n++;
        node = node.add(8).readPointer();
      }
    } catch (e) { n = -1; }
    this.n = n;
  },
  onLeave() {
    const dt = Date.now() - this.t0;
    st.calls++; st.totalMs += dt; if (dt > st.maxMs) st.maxMs = dt;
    st.nSum += this.n; if (this.n > st.nMax) st.nMax = this.n;
    if (dt > 50) { st.heavy++; st.heavyMs += dt; st.nHeavySum += this.n; }
    const bn = bucketN(this.n); st.nHist[bn] = (st.nHist[bn] || 0) + 1;
    const bd = bucketD(dt); st.durHist[bd] = (st.durHist[bd] || 0) + 1;
  }
});
Interceptor.attach(B(0x5e0a0), {
  onEnter() { this.t0 = Date.now(); },
  onLeave() { st.e0a0_calls++; st.e0a0_ms += Date.now() - this.t0; }
});
send({ type: 'hooked', name: 'listdiag' });

setInterval(() => {
  const out = {
    calls: st.calls, totalMs: st.totalMs, maxMs: st.maxMs,
    heavy: st.heavy, heavyMs: st.heavyMs,
    avgN: st.calls ? Math.round(st.nSum / st.calls) : 0, nMax: st.nMax,
    avgNheavy: st.heavy ? Math.round(st.nHeavySum / st.heavy) : 0,
    e0a0_calls: st.e0a0_calls, e0a0_ms: st.e0a0_ms,
    nHist: Object.assign({}, st.nHist), durHist: Object.assign({}, st.durHist),
  };
  st.calls = 0; st.totalMs = 0; st.maxMs = 0; st.heavy = 0; st.heavyMs = 0;
  st.nSum = 0; st.nMax = 0; st.nHeavySum = 0;
  st.e0a0_calls = 0; st.e0a0_ms = 0;
  st.nHist = {}; st.durHist = {};
  send({ type: 'stats', data: out });
}, 2000);
}