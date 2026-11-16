import frida, json, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'seqlog.txt'), "w")

BIND_SITES = [0x4700b7, 0x4700f6, 0x471487, 0x4715a1, 0x4718b7, 0x4718d5,
              0x477471, 0x4774c8, 0x4774e0, 0x4774f8, 0x477510, 0x47776e]
ACTIVE_SITES = [0x4700c2, 0x4700cd, 0x4700d8, 0x4700e3, 0x470102, 0x47010d, 0x470118,
                0x47012d, 0x470bae, 0x470bde, 0x470c57, 0x47122d, 0x47131d, 0x4718c2]

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function f32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
function hookCall(off, name){
  Interceptor.attach(base.add(off), { onEnter: function(a){
    var arg = this.context.esp.readU32();
    send(name + ' site=' + off + ' arg=' + arg);
  }});
}
BIND.forEach(function(o){ hookCall(o, 'BIND'); });
ACT.forEach(function(o){ hookCall(o, 'ACT'); });
Interceptor.attach(base.add(0x70932), { onEnter: function(a){
  var ecx = this.context.ecx, esi = this.context.esi;
  var n = ecx < 16 ? ecx : 16;
  var parts = [];
  for (var k=0;k<n;k++){
    var idx = this.context.edi.add(k*2).readU16();
    var b = esi.add(12).add(idx*32);
    parts.push(idx+':'+f32(b.add(4)).toFixed(1)+','+f32(b.add(8)).toFixed(1));
  }
  send('DRAW n=' + ecx + ' esi=' + esi + ' ' + parts.join(' '));
}});
send('READY');
"""
js = js.replace("BIND", json.dumps(BIND_SITES)).replace("ACT", json.dumps(ACTIVE_SITES))

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
