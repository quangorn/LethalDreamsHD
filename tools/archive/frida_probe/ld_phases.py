import frida, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'hooklog3.txt'), "w")

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function f32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
Interceptor.attach(base.add(0x70932), { onEnter: function(a){
  var ecx = this.context.ecx, esi = this.context.esi;
  var parts = [];
  var n = ecx < 16 ? ecx : 16;
  for (var k=0;k<n;k++){
    var idx = this.context.edi.add(k*2).readU16();
    var b = esi.add(12).add(idx*32);
    parts.push(idx + ':' + f32(b.add(4)).toFixed(1) + ',' + f32(b.add(8)).toFixed(1));
  }
  send('DRAW n=' + ecx + ' esi=' + esi + ' ' + parts.join(' '));
}});
send('READY');
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
# phases: hover ON over 'Изменить', then OFF far away, repeat
seq = [("ON",(2400,1750)),("OFF",(600,300)),("ON2",(2400,1750)),("OFF2",(600,300))]
for name,(x,y) in seq:
    out.write("MARK %s\n" % name); out.flush()
    u32.SetCursorPos(x,y)
    time.sleep(1.2)
out.write("MARK END\n"); out.flush(); out.close()
s.detach()
