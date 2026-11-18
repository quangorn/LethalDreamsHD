// perf_frida6.js — profile FUN_0045e0a0 callees: find the 72ms sub-call.
let mod = Process.findModuleByName('LETHALDREAMS.EXE');
if (!mod) {
  for (const m of Process.enumerateModules()) {
    if (m.name.toLowerCase() === 'lethal dreams.exe' || m.name.toLowerCase() === 'lethaldreams.exe') { mod = m; break; }
  }
}
if (!mod) { send({ type: 'error', msg: 'module not found' }); } else {
const B = (o) => mod.base.add(o);
const targets = [
  ['FUN_0045e0a0', 0x5e0a0],
  ['FUN_0045ce80', 0x5ce80],
  ['FUN_0045dd70', 0x5dd70],
  ['FUN_0045c880', 0x5c880],
  ['FUN_0045c5a0', 0x5c5a0],
  ['FUN_0045c4b0', 0x5c4b0],
  ['FUN_0045c570', 0x5c570],
  ['FUN_0045c6f0', 0x5c6f0],
  ['FUN_0045c730', 0x5c730],
];
const st = {};
for (const [name, off] of targets) {
  st[name] = { calls: 0, totalMs: 0, maxMs: 0, heavy: 0 };
  Interceptor.attach(B(off), {
    onEnter() { this.t0 = Date.now(); },
    onLeave() {
      const dt = Date.now() - this.t0;
      const s = st[name];
      s.calls++; s.totalMs += dt; if (dt > s.maxMs) s.maxMs = dt;
      if (dt > 50) s.heavy++;
    }
  });
}
send({ type: 'hooked', name: 'callees_of_e0a0' });
setInterval(() => {
  const out = {};
  for (const k in st) { out[k] = st[k]; st[k] = { calls: 0, totalMs: 0, maxMs: 0, heavy: 0 }; }
  send({ type: 'stats', data: out });
}, 2000);
}