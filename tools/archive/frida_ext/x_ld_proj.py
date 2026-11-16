import frida, json, time, sys

JS = r"""
'use strict';
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function at(v){ return base.add(v - 0x400000); }
var n = 0;
var seen = {};
var ctxp = at(0x51cc7c).readPointer();
var winw = ctxp.add(0x18).readU32(), winh = ctxp.add(0x1c).readU32();
send({ev:'K', winw: winw, winh: winh, k: (winw*768)/(winh*1024)});
Interceptor.attach(at(0x46f970), { onEnter: function (a) {
  var esp = this.context.esp;
  var idx = esp.add(4).readU32();
  if (idx !== 2) return;
  var mp = esp.add(8).readPointer();
  var cx = mp.readFloat();
  var key = mp.toString();
  if (!(key in seen)) {
    seen[key] = 1;
    send({ev:'LEARN', key: key, cx: cx, ret: '0x' + (this.context.esp.readPointer().sub(base).add(0x400000)).toString(16)});
  }
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
