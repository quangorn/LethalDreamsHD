import frida, json, time

JS = r"""
'use strict';
function hx(v){ return '0x' + v.toString(16); }
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function at(v){ return base.add(v - 0x400000); }

var n = 0;
Interceptor.attach(at(0x41d020), { onEnter: function (a) {
  n++;
  var o = a.context.ecx;   // ebp = font/text object
  if (n <= 5) {
    send({ev:'REND', n:n, obj:hx(o), flag58:o.add(0x58).readU8(),
          f40:hx(o.add(0x40).readPointer()), f44:hx(o.add(0x44).readPointer()),
          f1460:hx(o.add(0x1460).readPointer())});
  }
}});

var np = 0;
Interceptor.attach(at(0x41cc90), { onEnter: function (a) {
  np++;
  var o = a.context.ecx;
  if (np <= 5) send({ev:'PARSE', n:np, obj:hx(o), flag58:o.add(0x58).readU8()});
}});

send('READY');
"""

def on_message(m, data):
    if m.get("type") == "send":
        p = m["payload"]
        if isinstance(p, dict): print("MSG", json.dumps(p), flush=True)

dev = frida.get_local_device()
pid = dev.spawn(["D:\\Games\\LethalDreams\\LETHALDREAMS.EXE"])
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on("message", on_message)
sc.load()
dev.resume(pid)
time.sleep(12)
dev.kill(pid)
print("done", flush=True)
