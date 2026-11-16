import frida, json, time, sys

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import cavegen

pid = int(sys.argv[1])
cave = cavegen.build()
body = cave[:-11] + b"\xc3"
RESTORE = cavegen.META["RESTORE"]
L1 = cavegen.META["L1"]
L2 = len(body)  # stub location handled below; L2 computed from build META too

# recompute L2 by rebuilding with capture
src = open(os.path.join(_ROOT, 'tools', 'archive', 'cave_gen', 'cavegen.py'))
import re
txt = src.read()
m = re.search(r"emit\(b\"\\x8b\\xc1\"\); L2 = len\(code\)", txt)
# L2: second 'mov eax,ecx' position: find index in code manually
import subprocess

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var cavebase = base.add(0xf5201);
var body = %s;
var pool = Memory.alloc(4*32);
var idx = Memory.alloc(8);
for (var i=0;i<4;i++){ idx.add(i*2).writeU16(i); }
Memory.patchCode(cavebase, 265, function(cp){
  var start = cp;
  for (var i=0;i<body.length;i++){ cp.add(i).writeU8(body[i]); }
  var s = start.add(body.length);
  function w(b){ s.writeU8(b); s = s.add(1); }
  function imm32(op, ptr){
    w(op);
    var scratch = Memory.alloc(4);
    scratch.writePointer(ptr);
    for (var k=0;k<4;k++){ w(scratch.add(k).readU8()); }
  }
  imm32(0xBE, pool.sub(12));
  imm32(0xBF, idx);
  w(0xB9); w(4); w(0); w(0); w(0);
  w(0xE8);
  var relv = -(body.length + 15);
  for (var k=0;k<4;k++){ w((relv >>> (k*8)) & 0xff); }
  w(0xC3);
});
function readf32(p){
  var buf = new ArrayBuffer(4); var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) u[j]=p.add(j).readU8();
  return new Float32Array(buf)[0];
}
function writef32(p, v){
  var buf = new ArrayBuffer(4); var f = new Float32Array(buf); f[0]=v;
  var u = new Uint8Array(buf);
  for (var j=0;j<4;j++) p.add(j).writeU8(u[j]);
}
var xs=[100,200,200,100], ys=[100,100,200,200];
for (var i=0;i<4;i++){ writef32(pool.add(i*32), xs[i]); writef32(pool.add(i*32+4), ys[i]); }
function dump(p){ return '[' + readf32(p) + ',' + readf32(p.add(4)) + ']'; }
send('before ' + JSON.stringify([dump(pool),dump(pool.add(32)),dump(pool.add(64)),dump(pool.add(96))]));

var hits = {};
function hook(name, off){
  try {
    Interceptor.attach(cavebase.add(off), { onEnter: function(a){
      hits[name] = (hits[name]||0)+1;
      if (hits[name] <= 2) send('HIT ' + name + ' eax=' + this.context.eax + ' ebx=' + this.context.ebx + ' ecx=' + this.context.ecx + ' esp=' + this.context.esp);
    }});
  } catch(e){ send('HOOKERR ' + name + ' ' + e.message); }
}
hook('start', 0);
hook('loop1', %d);
hook('restore', %d);
try {
  var f = new NativeFunction(cavebase.add(body.length), 'void', []);
  f();
  send('CALLED OK');
} catch(e){ send('CALLERR ' + e.message); }
var out=[];
for (var i=0;i<4;i++) out.push(dump(pool.add(i*32)));
send('after ' + JSON.stringify(out));
send('hits ' + JSON.stringify(hits));
send('expect [[112.5,112.5],[187.5,112.5],[187.5,187.5],[112.5,187.5]]');
""" % (json.dumps(list(body)), L1, RESTORE)

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
def h(m, d):
    print("MSG", json.dumps(m, ensure_ascii=False)[:400], flush=True)
sc.on("message", h)
sc.load()
time.sleep(2)
s.detach()
