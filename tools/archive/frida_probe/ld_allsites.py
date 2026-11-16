import frida, json, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'allsites.txt'), "w")
sites = eval(open(os.path.join(paths.scratch_dir('ld'), 'sites.json')).read())

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function f32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
var SITES = %s;
var cnt = {};
SITES.forEach(function(pair){
  var va = pair[0];
  Interceptor.attach(base.add(va - 0x400000), { onEnter: function(a){
    var key = va.toString(16);
    cnt[key] = (cnt[key]||0)+1;
    send('H ' + key + ' ecx=' + this.context.ecx + ' edx=' + this.context.edx + ' esi=' + this.context.esi);
  }});
});
send('READY');
function flushcounts(){ send('CNT ' + JSON.stringify(cnt)); }
setInterval(flushcounts, 500);
""" % json.dumps(sites)

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
try: s.detach()
except Exception as e: print("detach", e)
