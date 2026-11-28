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
var gl = Process.findModuleByName('OpenGL32.dll') || Process.findModuleByName('opengl32.dll');
var calls = {};
['glDrawArrays','glDrawElements','glTexImage2D','glTexEnvf','glOrtho','glViewport','wglUseFontOutlinesW','wglUseFontBitmapsW'].forEach(function(fn){
  var e = gl ? gl.findExportByName(fn) : null;
  if (!e) return;
  calls[fn] = 0;
  Interceptor.attach(e, { onEnter: function (a) {
    calls[fn]++;
    if (calls[fn] <= 3) {
      var ra = a.context.pc;
      send({ev: fn, n: calls[fn], ra: hx(ra.sub(base)), args: [a[0].toInt32(), a[1].toInt32(), a[2].toInt32()]});
    }
  }});
});
send('READY');
"""

def on_message(m, data):
    if m.get("type") == "send":
        p = m["payload"]
        if isinstance(p, dict): print("MSG", json.dumps(p), flush=True)

dev = frida.get_local_device()
pid = dev.spawn([paths.game_path('LETHALDREAMS.EXE')])
s = dev.attach(pid)
sc = s.create_script(JS)
sc.on("message", on_message)
sc.load()
dev.resume(pid)
time.sleep(12)
sc.post("send({ev:'COUNTS', c: calls})")
time.sleep(1)
dev.kill(pid)
print("done", flush=True)
