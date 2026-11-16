import frida, json, time

JS = r"""
'use strict';
var k32 = Process.getModuleByName('kernel32.dll');
var names = ['BTexturer', 'msvcr70', 'msvcp70', 'dxwrapper', 'dsound'];
var seen = {};
function log(name, ok) {
  var key = name + (ok ? ' OK' : ' FAIL');
  if (!seen[key]) { seen[key] = 1; send({ev:'LOAD', name: name, ok: ok}); }
}
['LoadLibraryA','LoadLibraryW','LoadLibraryExA','LoadLibraryExW'].forEach(function(fn){
  var f = k32.getExportByName(fn);
  if (!f) return;
  Interceptor.attach(f, { onEnter: function(a){ this.n = a[0].readCString(); },
    onLeave: function(r){
      var n = this.n || '';
      for (var i=0;i<names.length;i++) {
        if (n.toLowerCase().indexOf(names[i].toLowerCase()) >= 0) log(n, r.toInt32() !== 0);
      }
    }});
});
send('READY');
"""

def on_message(m, data):
    if m.get('type') == 'send':
        print('MSG', json.dumps(m['payload'], ensure_ascii=False), flush=True)

dev = frida.get_local_device()
pid = dev.spawn(["D:\\Games\\LethalDreams\\LETHALDREAMS.EXE"])
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on('message', on_message)
sc.load()
dev.resume(pid)
time.sleep(14)
dev.kill(pid)
print('done', flush=True)
