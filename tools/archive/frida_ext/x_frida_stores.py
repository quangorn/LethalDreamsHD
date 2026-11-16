import frida, json, time

SITES = [0x41cf52,0x43fbc5,0x44058d,0x4405fd,0x4412fa,0x45a260,0x4768bb,
         0x477be9,0x4785d0,0x4b9d7f,0x4d5568,0x4d5777]
REGS  = {0x41cf52:"ecx",0x43fbc5:"ebx",0x44058d:"edi",0x4405fd:"ebx",0x4412fa:"ecx",
         0x45a260:"ebx",0x4768bb:"edx",0x477be9:"ebx",0x4785d0:"eax",0x4b9d7f:"eax",
         0x4d5568:"eax",0x4d5777:"eax"}

JS = r"""
'use strict';
function hx(v){ return '0x' + v.toString(16); }
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function at(v){ return base.add(v - 0x400000); }
var sites = %SITES%;
var regs = %REGS%;
var counts = {};
sites.forEach(function(v){
  counts[v] = 0;
  Interceptor.attach(at(v), { onEnter: function (a) {
    counts[v]++;
    var ctx = this.context;
    var r = regs[v];
    var val = ctx[r.toLowerCase()].toInt32();
    if (counts[v] <= 4) {
      send({ ev: 'STORE', site: hx(v), n: counts[v], val: val, hex: hx(ctx[r.toLowerCase()].toUInt32 ? ctx[r.toLowerCase()].toUInt32() : val) });
    }
  }});
});
Interceptor.attach(at(0x477de0), { onEnter: function (a) {
  var esp = this.context.esp;
  send({ ev: 'SDM', w: esp.add(4).readU32(), h: esp.add(8).readU32() });
}});
send('READY');
"""

def on_message(m, data):
    if m.get("type") == "send":
        print("MSG", json.dumps(m["payload"]), flush=True)
    else:
        print("MSG", json.dumps(m)[:200], flush=True)

js = JS.replace("%SITES%", json.dumps(SITES)).replace("%REGS%", json.dumps(REGS))
dev = frida.get_local_device()
pid = dev.spawn(["D:\\Games\\LethalDreams\\LETHALDREAMS.EXE"])
s = dev.attach(pid)
sc = s.create_script(js)
sc.on("message", on_message)
sc.load()
dev.resume(pid)
time.sleep(14)
sc.post("send({ev:'COUNTS', counts: counts})")
time.sleep(1)
dev.kill(pid)
print("done", flush=True)
