import frida, json, time, sys

JS = r"""
'use strict';
function hx(v){ return '0x' + v.toString(16); }
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function at(v){ return base.add(v - 0x400000); }

// all SetDisplayMode calls (w,h at entry)
Interceptor.attach(at(0x477de0), { onEnter: function (a) {
  var esp = this.context.esp;
  send({ ev: 'SDM', w: esp.add(4).readU32(), h: esp.add(8).readU32(),
         p3: esp.add(12).readU32(), p4: esp.add(16).readU32() });
}});

// Cave_P store site: count fires, show slot and kx
var nP = 0;
Interceptor.attach(at(0x51c000), { onEnter: function (a) {
  nP++;
  if (nP < 6 || nP % 500 === 0) {
    var esp = this.context.esp;
    send({ ev: 'CAVEP', n: nP, slot: esp.add(0xc).readU32(), kx: at(0x51c0a0).readDouble() });
  }
}});

// Cave_K end: dump the kx written
var nK = 0;
Interceptor.attach(at(0x51c036), { onEnter: function (a) {
  nK++;
  var esp = this.context.esp;
  send({ ev: 'CAVEK', n: nK, w: esp.add(0x10).readU32(), h: esp.add(0x14).readU32() });
}});

send('READY');
"""

def on_message(m, data):
    if m.get("type") == "send":
        p = m["payload"]
        print("MSG", json.dumps(p), flush=True)
    else:
        print("MSG", json.dumps(m)[:200], flush=True)

dev = frida.get_local_device()
pid = dev.spawn(["D:\\Games\\LethalDreams\\LETHALDREAMS.EXE"])
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on("message", on_message)
sc.load()
dev.resume(pid)
time.sleep(12)
# read K_KX at runtime
sc.post("""send({ev:'KXNOW', kx: %s})""" % "Process.findModuleByName('LETHALDREAMS.EXE').base.add(0x11c0a0).readDouble()")
time.sleep(1)
dev.kill(pid)
print("done", flush=True)
