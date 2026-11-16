import frida, json, time, sys

JS = r"""
'use strict';
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function at(v){ return base.add(v - 0x400000); }
var ctxp = at(0x51cc7c).readPointer();   // DAT_0051cc7c = pointer to render ctx object
send({ev:'CTX', w: ctxp.add(0x18).readU32(), h: ctxp.add(0x1c).readU32()});
var winobj = at(0x51d510).readPointer();
send({ev:'WINOBJ', w: winobj.add(0x18).readU32(), h: winobj.add(0x1c).readU32(), hwnd: winobj.add(0x7c).readPointer()});
// dump nearby scale-ish floats in ctx
var out = [];
for (var o = 0; o < 0x60; o += 4) out.push(o.toString(16) + ':' + ctx.add(o).readU32().toString(16));
send({ev:'CTXDUMP', d: out.join(' ')});
"""

def on_message(m, data):
    if m.get("type") == "send":
        print("MSG", json.dumps(m["payload"]), flush=True)
    else:
        print("MSG", json.dumps(m)[:200], flush=True)

dev = frida.get_local_device()
pid = int(sys.argv[1])
act = sys.argv[2] if len(sys.argv) > 2 else 'read'
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on("message", on_message)
sc.load()
time.sleep(1)
if act == 'write':
    newh = int(sys.argv[3])
    sc.post("""
      var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
      function at(v){ return base.add(v - 0x400000); }
      var ctxp = at(0x51cc7c).readPointer();
      ctxp.add(0x1c).writeU32(%d);
      send({ev:'WROTE', h: ctxp.add(0x1c).readU32(), w: ctxp.add(0x18).readU32()});
    """ % newh)
    time.sleep(1)
s.detach()
print("detached", flush=True)
