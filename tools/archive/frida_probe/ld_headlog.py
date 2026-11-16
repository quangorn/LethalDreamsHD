import frida, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'hooklog5.txt'), "w")

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function u32(p){ return p.readU32(); }
Interceptor.attach(base.add(0x70932), { onEnter: function(a){
  var esi = this.context.esi, ecx = this.context.ecx, edi = this.context.edi;
  var head = [];
  for (var t=-8; t<16; t++) head.push(u32(esi.add(t*4)).toString(16));
  send('OBJ n=' + ecx + ' esi=' + esi + ' head=' + head.join(','));
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
seq = [("ON",(2400,1750)),("OFF",(600,300)),("ON2",(2400,1750)),("OFF2",(600,300))]
for name,(x,y) in seq:
    out.write("MARK %s\n" % name); out.flush()
    u32.SetCursorPos(x,y)
    time.sleep(1.2)
out.write("MARK END\n"); out.flush(); out.close()
s.detach()
