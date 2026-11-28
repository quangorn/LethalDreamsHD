import frida, json, time
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths

JS = r"""
'use strict';
var k32 = Process.getModuleByName('kernel32.dll');
var names = ['msvcr70', 'msvcp70'];
['LoadLibraryA','LoadLibraryW','LoadLibraryExA','LoadLibraryExW'].forEach(function(fn){
  var f = k32.getExportByName(fn);
  if (!f) return;
  Interceptor.attach(f, { onEnter: function(a){ this.n = a[0].readCString(); },
    onLeave: function(r){
      var n = (this.n || '').toLowerCase();
      for (var i=0;i<names.length;i++) {
        if (n.indexOf(names[i]) >= 0) {
          send({ev:'SIM_FAIL', name: this.n, real: r.toInt32() !== 0});
          r.replace(ptr(0));   // pretend the DLL is missing
        }
      }
    }});
});
var bt = k32.getExportByName('GetProcAddress');
send('READY');
"""

def on_message(m, data):
    if m.get('type') == 'send':
        print('MSG', json.dumps(m['payload'], ensure_ascii=False), flush=True)

dev = frida.get_local_device()
pid = dev.spawn([paths.scratch_dir('ld_copy') + '/LETHALDREAMS.EXE'])
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on('message', on_message)
sc.load()
dev.resume(pid)
time.sleep(14)
alive = dev.get_attachments()
print('attachments after 14s:', alive, flush=True)
try:
    dev.kill(pid)
except Exception as e:
    print('kill:', e)
