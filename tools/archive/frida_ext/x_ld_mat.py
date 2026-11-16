import frida, json, time, sys

JS = r"""
'use strict';
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function at(v){ return base.add(v - 0x400000); }
var n = 0;
Interceptor.attach(at(0x46f970), { onEnter: function (a) {
  if (n++ > 60) return;
  // thiscall: ecx = ctx, stack args: param_1 idx, param_2 matrix ptr
  var esp = this.context.esp;
  var idx = esp.add(4).readU32();
  var mp = esp.add(8).readPointer();
  var f = [];
  for (var i = 0; i < 16; i++) f.push(mp.add(i*4).readFloat());
  send({ev:'MAT', n: n, idx: idx, m: f.map(function(x){ return Math.round(x*1e6)/1e6; })});
}});
// viewport wrapper
Interceptor.attach(at(0x46f900), { onEnter: function (a) {
  var esp = this.context.esp;
  send({ev:'VIEW', a0: esp.add(4).readU32(), a1: esp.add(8).readU32(), a2: esp.add(12).readU32(), a3: esp.add(16).readU32()});
}});
send('READY');
"""

def on_message(m, data):
    if m.get("type") == "send":
        print("MSG", json.dumps(m["payload"]), flush=True)
    else:
        print("ERR", json.dumps(m)[:300], flush=True)

dev = frida.get_local_device()
pid = int(sys.argv[1])
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 3
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on("message", on_message)
sc.load()
time.sleep(secs)
s.detach()
print("detached", flush=True)
