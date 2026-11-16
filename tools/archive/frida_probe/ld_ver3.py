"""Backtrace for small (text) quads at the quad submit 0x4708d0."""
import frida, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'ver3.txt'), "w")

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function f32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
var sent = 0, sentimg = 0;
Interceptor.attach(base.add(0x708d0), { onEnter: function(a){
  var thisp = this.context.ecx;
  var vb = thisp.add(4).readPointer();
  var a0 = this.context.esp.add(4).readU32();
  var rec = vb.add(a0*32);
  var x = f32(rec), y = f32(rec.add(4));
  var bt = Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 8)
      .map(function(p){ var m = Process.findModuleByAddress(p);
        return m ? m.name + '+' + p.sub(m.base).toString(16) : String(p); }).join(' < ');
  var small = false;
  // second vertex x gives width (a0+1)
  var w = f32(vb.add((a0+1)*32)) - x, h = f32(vb.add((a0+3)*32).add(4)) - y;
  small = Math.abs(w) <= 34 && Math.abs(h) <= 40;
  if (small && sent < 3) { sent++; send('SMALL ' + x.toFixed(1) + ',' + y.toFixed(1) + ' wh=' + w.toFixed(1) + 'x' + h.toFixed(1) + ' bt=' + bt); }
  if (!small && sentimg < 2) { sentimg++; send('IMG ' + x.toFixed(1) + ',' + y.toFixed(1) + ' wh=' + w.toFixed(1) + 'x' + h.toFixed(1) + ' bt=' + bt); }
}});
send('READY');
"""

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
def h(m, d):
    p = m.get("payload")
    if isinstance(p, str):
        out.write(p + "\n"); out.flush()
sc.on("message", h)
sc.load()
u32 = ctypes.windll.user32
u32.SetProcessDPIAware()
time.sleep(0.3)
u32.SetCursorPos(640, 330)
time.sleep(1.5)
out.close()
s.detach()
