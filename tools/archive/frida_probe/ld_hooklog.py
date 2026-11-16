import frida, json, time, sys, ctypes, subprocess

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'hooklog.txt'), "w")

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function f32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
var N = 0;
function hookSite(off, name){
  Interceptor.attach(base.add(off), { onEnter: function(a){
    var ecx = this.context.ecx, esi = this.context.esi, edi = this.context.edi, edx = this.context.edx;
    var out = [];
    var minx=1e9, maxx=-1e9, miny=1e9, maxy=-1e9;
    var n = ecx < 24 ? ecx : 24;
    for (var k=0;k<n;k++){
      var idx = edi.add(k*2).readU16();
      var rec = esi.add(idx*32);
      var x = f32(rec.add(12)), y = f32(rec.add(16));
      var u = f32(rec.add(20)), v = f32(rec.add(24));
      out.push(idx+':' + x.toFixed(1) + ',' + y.toFixed(1) + ',' + u.toFixed(3) + ',' + v.toFixed(3));
      if (x<minx) minx=x; if (x>maxx) maxx=x;
      if (y<miny) miny=y; if (y>maxy) maxy=y;
    }
    send(name + ' n=' + ecx + ' mode=' + edx + ' esi=' + esi + ' edi=' + edi +
         ' range=[' + minx.toFixed(1) + ',' + miny.toFixed(1) + ',' + maxx.toFixed(1) + ',' + maxy.toFixed(1) + ']' +
         ' verts=' + out.join(' '));
    N++;
  }});
}
hookSite(0x70932, 'S932');
hookSite(0x707b7, 'S7b7');
send('HOOKS ON N=0');
"""

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
def h(m, d):
    p = m.get("payload")
    if isinstance(p, str):
        out.write(p + "\n")
sc.on("message", h)
sc.load()
u32 = ctypes.windll.user32
u32.SetProcessDPIAware()
# hover 'Изменить' region, then sweep away, then hover again
pts = [(2400,1750),(2350,1730),(2450,1770),(2400,1760),(1200,900),(1000,1100),(2400,1750),(2400,1760)]
for (x,y) in pts:
    u32.SetCursorPos(x,y)
    time.sleep(0.5)
out.flush()
out.close()
s.detach()
