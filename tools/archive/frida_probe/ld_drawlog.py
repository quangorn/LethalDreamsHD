import frida, json, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'drawlog.txt'), "w")
sites = eval(open(os.path.join(paths.scratch_dir('ld'), 'drawsites.json')).read())

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function f32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
var SITES = %s;
SITES.forEach(function(pair){
  var va = pair[0];
  Interceptor.attach(base.add(va - 0x400000), { onEnter: function(a){
    var c = this.context;
    var parts = 'H ' + va.toString(16) + ' eax=' + c.eax + ' ecx=' + c.ecx + ' edx=' + c.edx + ' esi=' + c.esi + ' edi=' + c.edi;
    var sp = c.esp;
    parts += ' args=' + [sp.add(4).readU32(), sp.add(8).readU32(), sp.add(12).readU32(), sp.add(16).readU32()].join(',');
    // try pool at esi+0xc, indices in edi (word array)
    try {
      var n = c.ecx >>> 0;
      if (n > 0 && n <= 20000) {
        var recs = [];
        for (var i=0;i<Math.min(n,4);i++){
          var idx = edi2word(c.edi, i);
          var r = c.esi.add(0xc).add(idx*32);
          recs.push('[' + f32(r.add(4)) + ',' + f32(r.add(8)) + ',' + f32(r.add(12)) + ',' + f32(r.add(16)) + ']');
        }
        parts += ' recs=' + recs.join('|');
      }
    } catch(e){ parts += ' err=' + e; }
    send(parts);
  }});
});
function edi2word(edi, i){ return ptr(edi).add(i*2).readU16(); }
send('READY');
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
