import frida, json, time
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths

JS = r"""
'use strict';
function hx(v){ return '0x' + v.toString(16); }
var bt = Process.getModuleByName('BTexturer.dll');
send({ev:'BTBASE', base: hx(bt.base)});
var sites = [ {rva: 0xc569, reg:'eax', base:'esi'},
              {rva: 0xc5c5, reg:'eax', base:'esi'},
              {rva: 0xc684, reg:'esi', base:'edi'} ];
var counts = {};
sites.forEach(function(s){
  counts[s.rva] = 0;
  Interceptor.attach(bt.base.add(s.rva), { onEnter: function (a) {
    counts[s.rva]++;
    var ctx = this.context;
    var val = ctx[s.reg].toInt32();
    var bsel = ctx[s.base];
    if (counts[s.rva] <= 10) {
      send({ev:'BTSTORE', rva: hx(s.rva), n: counts[s.rva], val: val, obj: hx(bsel)});
    }
  }});
});
send('READY');
"""

def on_message(m, data):
    if m.get("type") == "send":
        print("MSG", json.dumps(m["payload"], ensure_ascii=False)[:300], flush=True)
    else:
        print("MSG", json.dumps(m)[:300], flush=True)

dev = frida.get_local_device()
pid = dev.spawn([paths.game_path('LETHALDREAMS.EXE')])
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on("message", on_message)
sc.load()
dev.resume(pid)
time.sleep(15)
print("POST", sc.post("send({ev:'COUNTS', counts: counts})"), flush=True)
time.sleep(2)
dev.kill(pid)
print("done", flush=True)
