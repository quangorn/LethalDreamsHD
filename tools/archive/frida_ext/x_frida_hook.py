import frida, json, time
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths

JS = r"""
'use strict';
function hx(v){ return '0x' + v.toString(16); }
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function at(v){ return base.add(v - 0x400000); }

Process.setExceptionHandler(function (d) {
  var pc = d.context.pc;
  send({ ev: 'EXC', type: d.type, pc: hx(pc.sub(base)), ax: d.address ? hx(d.address) : null,
         esp: hx(d.context.esp) });
  return false;
});

// SetDisplayMode entry: params [esp+4], [esp+8]
Interceptor.attach(at(0x477de0), { onEnter: function (a) {
  var esp = this.context.esp;
  send({ ev: 'SDM', p1: hx(esp.add(4).readU32()), p2: hx(esp.add(8).readU32()),
         p3: hx(esp.add(12).readU32()), p4: hx(esp.add(16).readU32()), esp: hx(esp) });
}});

// Cave_K start: log ESP (== ESP_A, after 3 SEH pushes) and param slots via +0x10/+0x14
Interceptor.attach(at(0x51c036), { onEnter: function (a) {
  var esp = this.context.esp;
  send({ ev: 'CAVEK', esp: hx(esp), p1: hx(esp.add(0x10).readU32()), p2: hx(esp.add(0x14).readU32()) });
}});

// Cave_P start
Interceptor.attach(at(0x51c000), { onEnter: function (a) {
  var esp = this.context.esp;
  send({ ev: 'CAVEP', esp: hx(esp), slot: hx(esp.add(0xc).readU32()) });
}});
send('READY');
"""

def on_message(m, data):
    print("MSG", json.dumps(m, ensure_ascii=False)[:400], flush=True)

dev = frida.get_local_device()
pid = dev.spawn([paths.game_path('LETHALDREAMS.EXE')])
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on("message", on_message)
sc.load()
dev.resume(pid)
t0 = time.time()
while time.time() - t0 < 25:
    time.sleep(1.0)
    try:
        dev.get_process_by_pid(pid)
    except Exception:
        print("process gone", flush=True)
        break
print("done", flush=True)
