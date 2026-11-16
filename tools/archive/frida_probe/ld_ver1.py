"""Corrected vertex dump at 0x470932.

Real GL layout (verified by disassembly of 0x4708d0):
  record: x@+0, y@+4, z@+8 (floats), color bytes@+0xc,
          u@+0x10, v@+0x14 (floats), pad@+0x18
  at site: esi = V+0x10, so V = esi-0x10; indices from edi (already biased),
  count = ecx, mode = edx.
Previous logger (ld_phases2.py) read base = esi+0xc+idx*32 => off by 0x1c
(padding + next record), which produced the bogus "u,v,w,x,y" layout.
"""
import frida, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'ver1.txt'), "w")

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function f32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
var frame = 0, total = 0;
Interceptor.attach(base.add(0x70932), { onEnter: function(a){
  var ecx = this.context.ecx, esi = this.context.esi, edi = this.context.edi, edx = this.context.edx;
  var V = esi.sub(0x10);
  var parts = [];
  var n = ecx < 64 ? ecx : 64;
  for (var k=0;k<n;k++){
    var idx = edi.add(k*2).readU16();
    var b = V.add(idx*32);
    parts.push(idx + ':' + f32(b).toFixed(1) + ',' + f32(b.add(4)).toFixed(1)
      + '/' + f32(b.add(0x10)).toFixed(4) + ',' + f32(b.add(0x14)).toFixed(4));
  }
  total++;
  send('DRAW n=' + ecx + ' mode=' + edx + ' esi=' + esi + ' ' + parts.join(' '));
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
time.sleep(0.5)
u32.SetCursorPos(640, 330)
time.sleep(1.0)
out.write("MARK moved\n"); out.flush()
time.sleep(0.8)
out.write("MARK end\n"); out.close()
s.detach()
