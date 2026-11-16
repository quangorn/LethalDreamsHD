import frida, json, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'hooklog2.txt'), "w")

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function f32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
function hookSite(off, name){
  Interceptor.attach(base.add(off), { onEnter: function(a){
    var ecx = this.context.ecx, esi = this.context.esi, edi = this.context.edi, edx = this.context.edx;
    var parts = [];
    var n = ecx < 16 ? ecx : 16;
    for (var k=0;k<n;k++){
      var idx = edi.add(k*2).readU16();
      var b = esi.add(12).add(idx*32);   // base = esi+0xc + idx*32
      var f = [];
      for (var t=0;t<8;t++) f.push(f32(b.add(t*4)).toFixed(2));
      parts.push(idx+':[' + f.join(',') + ']');
    }
    send(name + ' n=' + ecx + ' mode=' + edx + ' esi=' + esi + ' ' + parts.join(' '));
  }});
}
hookSite(0x70932, 'S932');
hookSite(0x707b7, 'S7b7');
send('HOOKS ON');
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
pts = [(2400,1750),(2350,1730),(2450,1770),(2400,1760),(1200,900),(1000,1100),(2400,1750),(2400,1760)]
for (x,y) in pts:
    u32.SetCursorPos(x,y)
    time.sleep(0.5)
out.flush(); out.close()
s.detach()
