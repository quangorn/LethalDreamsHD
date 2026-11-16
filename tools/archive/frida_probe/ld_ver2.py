"""Dump caller return address of the quad submit 0x4708d0 for each draw."""
import frida, time, sys, ctypes

pid = int(sys.argv[1])

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
out = open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(paths.scratch_dir('ld'), 'ver2.txt'), "w")

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function f32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
Interceptor.attach(base.add(0x708d0), { onEnter: function(a){
  // entry: ecx=this, [esp+4]=a0 vertex idx, ... ; ret addr at [esp]
  var ret = this.returnAddress;
  var mod = Process.findModuleByAddress(ret);
  var name = mod ? mod.name + '+' + ret.sub(mod.base).toString(16) : String(ret);
  // dump first record of the batch: this+4 is vertex base
  var thisp = this.context.ecx;
  var vb = thisp.add(4).readPointer();
  var a0 = this.context.esp.add(4).readU32();
  var rec = vb.add(a0*32);
  send('Q ret=' + name + ' rec=' + rec + ' xy=' + f32(rec).toFixed(1) + ',' + f32(rec.add(4)).toFixed(1)
       + ' uv=' + f32(rec.add(0x10)).toFixed(3) + ',' + f32(rec.add(0x14)).toFixed(3));
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
time.sleep(0.3)
u32.SetCursorPos(640, 330)
time.sleep(0.7)
out.write("MARK moved\n"); out.flush()
time.sleep(0.5)
out.close()
s.detach()
