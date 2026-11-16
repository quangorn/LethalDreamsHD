import frida, json, time

JS = r"""
'use strict';
function hx(v){ return '0x' + v.toString(16); }
var hooked = false;
function attach_bt() {
  var bt = Process.findModuleByName('BTexturer.dll');
  if (!bt) return false;
  if (hooked) return true;
  hooked = true;
  send({ev:'BTBASE', base: hx(bt.base)});
  var sites = [ {rva: 0xc569, reg:'eax', base:'esi'},
                {rva: 0xc5c5, reg:'eax', base:'esi'},
                {rva: 0xc684, reg:'esi', base:'edi'} ];
  window.counts = {};
  sites.forEach(function(s){
    window.counts[s.rva] = 0;
    Interceptor.attach(bt.base.add(s.rva), { onEnter: function (a) {
      window.counts[s.rva]++;
      var ctx = this.context;
      if (window.counts[s.rva] <= 10) {
        send({ev:'BTSTORE', rva: hx(s.rva), n: window.counts[s.rva],
              val: ctx[s.reg].toInt32(), obj: hx(ctx[s.base])});
      }
    }});
  });
  return true;
}
// poll for the module every 30 ms; attach as soon as it appears
setInterval(function () {
  if (!hooked && attach_bt()) send({ev:'ATTACHED', base: hx(Process.findModuleByName('BTexturer.dll').base)});
}, 30);
send('READY');
"""

def on_message(m, data):
    if m.get("type") == "send":
        print("MSG", json.dumps(m["payload"], ensure_ascii=False)[:300], flush=True)
    else:
        print("MSG", json.dumps(m)[:300], flush=True)

dev = frida.get_local_device()
pid = dev.spawn(["D:\\Games\\LethalDreams\\LETHALDREAMS.EXE"])
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on("message", on_message)
sc.load()
dev.resume(pid)
time.sleep(15)
sc.post("send({ev:'COUNTS', counts: window.counts})")
time.sleep(2)
dev.kill(pid)
print("done", flush=True)
