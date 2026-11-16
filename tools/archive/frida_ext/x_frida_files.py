import frida, json, time

SITES = [0x41cf52,0x43fbc5,0x44058d,0x4405fd,0x4412fa,0x45a260,0x4768bb,
         0x477be9,0x4785d0,0x4b9d7f,0x4d5568,0x4d5777]

JS = r"""
'use strict';
function hx(v){ return '0x' + v.toString(16); }
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function at(v){ return base.add(v - 0x400000); }

// file opens: CreateFileA
var k32 = Process.getModuleByName('kernel32.dll');
var cf = k32.getExportByName('CreateFileA') || k32.getExportByName('CreateFileW');
Interceptor.attach(cf, { onEnter: function(a){
  var name = a[0].readCString();
  if (name && /fdf|btf/i.test(name)) send({ev:'FILE', name: name});
}});

var counts = {};
%sites%.forEach(function(v){ counts[v]=0;
  Interceptor.attach(at(v), { onEnter: function (a) {
    counts[v]++;
    if (counts[v] <= 3) send({ev:'STORE', site: hx(v), n: counts[v]});
  }});
});
send('READY');
"""

def on_message(m, data):
    if m.get("type") == "send":
        print("MSG", json.dumps(m["payload"], ensure_ascii=False)[:300], flush=True)
    else:
        print("MSG", json.dumps(m)[:300], flush=True)

js = JS.replace("%sites%", json.dumps(SITES))
dev = frida.get_local_device()
pid = dev.spawn(["D:\\Games\\LethalDreams\\LETHALDREAMS.EXE"])
s = dev.attach(pid)
sc = s.create_script(js)
sc.on("message", on_message)
sc.load()
dev.resume(pid)
time.sleep(20)
sc.post("send({ev:'COUNTS', counts: counts})")
time.sleep(1)
dev.kill(pid)
print("done", flush=True)
