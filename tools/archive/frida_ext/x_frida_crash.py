import frida, json, sys, time

JS = r"""
'use strict';
function hx(v){ return '0x' + v.toString(16); }
Process.setExceptionHandler(function (d) {
  var base = Process.findModuleByName('LETHALDREAMS.EXE');
  var pc = d.context.pc;
  var out = { type: d.type, pc: hx(pc.sub(base.base)), ax: d.address ? hx(d.address) : null,
             esp: hx(d.context.esp), eax: hx(d.context.eax), ecx: hx(d.context.ecx),
             edx: hx(d.context.edx) };
  var dis = []; var p = pc.sub(24);
  for (var i = 0; i < 12; i++) { var ins = Instruction(p); dis.push(hx(p) + ' ' + ins.toString()); p = ins.next; }
  out.disasm = dis;
  send(out);
  return false;
});
send('READY');
"""

def on_message(m, data):
    print("MSG", json.dumps(m, ensure_ascii=False)[:600], flush=True)

dev = frida.get_local_device()
pid = dev.spawn(["D:\\Games\\LethalDreams\\LETHALDREAMS.EXE"])
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on("message", on_message)
sc.load()
dev.resume(pid)
t0 = time.time()
alive = True
while time.time() - t0 < 25 and alive:
    time.sleep(1.0)
    try:
        dev.get_process_by_pid(pid)
    except Exception:
        alive = False
        print("process gone", flush=True)
print("done", flush=True)
