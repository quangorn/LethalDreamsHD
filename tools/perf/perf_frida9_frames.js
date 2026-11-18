// perf_frida9_frames.js — frame pacing через wglSwapBuffers (только замер).
// Раз в 2 с: кадры, fps, среднее/макс время кадра, число кадров >50 мс и >100 мс.
// "Лаг" = кадр длиннее 50 мс (ниже 20 fps на кадре).
let st = { n: 0, ms: 0, max: 0, over50: 0, over100: 0, gaps: 0 };
let hooked = false;
let last = 0;

function findSwap() {
  // frida 17: Module.findExportByName убран; работаем через модуль.
  try {
    const m = Process.findModuleByName('opengl32.dll');
    if (m) return m.findExportByName('wglSwapBuffers');
  } catch (e) {}
  try {
    if (typeof Module.getGlobalExportByName === 'function') {
      return Module.getGlobalExportByName('wglSwapBuffers');
    }
  } catch (e) {}
  return null;
}

function hook() {
  const p = findSwap();
  if (!p) return false;
  Interceptor.attach(p, {
    onEnter() {
      const now = Date.now();
      if (last > 0) {
        const dt = now - last;
        if (dt > 0) {
          st.n++; st.ms += dt;
          if (dt > st.max) st.max = dt;
          if (dt > 50) st.over50++;
          if (dt > 100) st.over100++;
        }
      }
      last = now;
    }
  });
  send({ type: 'hooked', name: 'frames', addr: p.toString() });
  return true;
}

let tries = 0;
const t = setInterval(() => {
  if (hooked) return;
  if (hook()) { hooked = true; clearInterval(t); return; }
  if (++tries > 120) { send({ type: 'error', msg: 'opengl32!wglSwapBuffers not found' }); clearInterval(t); }
}, 250);

setInterval(() => {
  const out = {
    frames: st.n,
    fps: st.n / 2,
    avg_ms: st.n ? Math.round(st.ms / st.n) : 0,
    max_ms: st.max,
    over50: st.over50,
    over100: st.over100,
  };
  st = { n: 0, ms: 0, max: 0, over50: 0, over100: 0, gaps: 0 };
  send({ type: 'stats', data: out });
}, 2000);
