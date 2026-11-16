"""Hook glTexImage2D: sizes + backtrace + pixel-content hash for the first uploads."""
import frida, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'ver4.txt'), "w")

js = """
var gl = Process.getModuleByName('opengl32.dll').getExportByName('glTexImage2D');
send('glTexImage2D=' + gl);
var n = 0, nsub = 0;
Interceptor.attach(gl, { onEnter: function(a){
  var target = a[0].toInt32(), level = a[1].toInt32();
  var w = a[3].toInt32(), h = a[4].toInt32();
  var pix = a[8];
  if (n < 40) {
    n++;
    var bt = Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 6)
      .map(function(p){ var m = Process.findModuleByAddress(p);
        return m ? m.name + '+' + p.sub(m.base).toString(16) : String(p); }).join(' < ');
    var h0 = '';
    if (!pix.isNull() && w > 0 && h > 0) {
      var npx = Math.min(w*h*4, 64);
      var s = 0; for (var i=0;i<npx;i++) s = (s*31 + pix.add(i).readU8()) & 0xffffff;
      h0 = ' hash=' + s;
    }
    send('TEX target=' + target + ' ' + w + 'x' + h + h0 + ' bt=' + bt);
  }
}});
Interceptor.attach(gl, { onLeave: function(r){ }});
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
# close dialog, reopen via hover click points to force fresh text draw
u32.SetCursorPos(2400, 1750); time.sleep(0.8)
u32.mouse_event(0x2,0,0,0,0); time.sleep(0.08); u32.mouse_event(0x4,0,0,0,0)
time.sleep(1.5)
u32.SetCursorPos(640, 330); time.sleep(0.8)
out.close()
s.detach()
