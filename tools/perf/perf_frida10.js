// perf_frida10.js — что реально происходит в поиске цели FUN_0045e0a0.
// Только замер: ловит важёлые вызовы (итераций > 20) и по каждому пишет:
//   iter      — сколько раз вызван сканер FUN_0045ce80
//   uniq      — сколько разных объектов вернул сканер
//   repeats   — сколько раз вернулся уже встречавшийся объект
//   arg1/arg2 — фильтры-аргументы e0a0 (поле [obj+8] сравнивается с ними)
//   metric    — [obj+0x24] у выбранного объекта
//   dist2     — self-расстояние (dx^2+dy^2) до объекта, на котором идёт поиск
//   mutated   — менялось ли [obj+0x24] внутри одного поиска
//   seq       — первые 12 целей как "obj:metric:dist2"
let mod = null;
for (const m of Process.enumerateModules()) {
  if (m.name.toLowerCase() === 'lethaldreams.exe') { mod = m; break; }
}
if (!mod) { send({ type: 'error', msg: 'module not found' }); } else {
const B = (o) => mod.base.add(o);
const P = (p, off) => p.add(off).readS32();
let depth = 0;
let cur = null;         // текущий поиск
const queue = [];       // отчёт после onLeave

function newSearch(thisPtr, arg1, arg2) {
  return {
    thisPtr: thisPtr, arg1: arg1, arg2: arg2,
    seq: [], uniq: new Map(), repeats: 0, mutated: false, c880: 0, c5a0: 0,
  };
}

Interceptor.attach(B(0x5e0a0), {
  onEnter(args) {
    this.arg1 = args[0];
    this.arg2 = args[1];
    this.t0 = Date.now();
    depth++;
    if (depth === 1) { this.top = true; cur = newSearch(this.context.ecx, args[0], args[1]); }
  },
  onLeave() {
    depth--;
    if (this.top && cur) {
      const it = cur.seq.length;
      if (it > 20) {
        const rec = {
          iter: it, uniq: cur.uniq.size, repeats: cur.repeats,
          arg1: cur.arg1.toString(), arg2: cur.arg2.toString(),
          mutated: cur.mutated, ms: Date.now() - this.t0,
          seq: cur.seq.slice(0, 12),
        };
        send({ type: 'heavy', data: rec });
      }
      cur = null;
    }
  }
});

Interceptor.attach(B(0x5ce80), {
  onLeave(retval) {
    if (depth === 0 || !cur) return;
    const o = retval;
    if (o.isNull()) { cur.seq.push('null'); return; }
    const metric = P(o, 0x24);
    const dx = P(o, 0x0c) - P(cur.thisPtr, 0x0c);
    const dy = P(o, 0x10) - P(cur.thisPtr, 0x10);
    const d2 = dx * dx + dy * dy;
    const key = o.toString();
    const prev = cur.uniq.get(key);
    if (prev === undefined) {
      cur.uniq.set(key, metric);
    } else {
      cur.repeats++;
      if (prev !== metric) cur.mutated = true;
    }
    if (cur.seq.length < 12) cur.seq.push(key + ':' + metric + ':' + d2);
  }
});

// Предикаты: дошли до успеха или нет (просто счётчики на поиск).
Interceptor.attach(B(0x5c880), { onLeave(retval) { if (cur) cur.c880 += (retval.toInt32() & 0xff) ? 1 : 0; } });
Interceptor.attach(B(0x5c5a0), { onLeave(retval) { if (cur) cur.c5a0 += (retval.toInt32() & 0xff) ? 1 : 0; } });

send({ type: 'hooked', name: 'target-search' });
}
