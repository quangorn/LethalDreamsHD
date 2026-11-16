"""Diagnostic: hook 0x470932, log count/esi and shift x by delta to prove the buffer is live."""
import frida, time, sys, ctypes, subprocess

pid = int(sys.argv[1])
delta = float(sys.argv[2]) if len(sys.argv) > 2 else -100.0
tag = sys.argv[3] if len(sys.argv) > 3 else "diag"

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var D = %f; var fired = 0, mods = 0;
function f32p(p){ return p.readFloat(); }
Interceptor.attach(base.add(0x70932), { onEnter: function(a){
  fired++;
  var ecx = this.context.ecx, esi = this.context.esi, edi = this.context.edi;
  var V = esi.sub(0x10);
  var n = ecx;
  if (n <= 0 || n > 64) return;
  var mods_local = 0;
  for (var i=0;i<n;i++){
    var idx = edi.add(i*2).readU16();
    var rec = V.add(idx*32);
    rec.writeFloat(rec.readFloat() + D);
    mods_local++;
  }
  mods += mods_local;
  if (fired <= 3 || fired %% 2000 == 0) send('fire ' + fired + ' n=' + n + ' V=' + V + ' first=' + f32p(V) + ' idx0=' + edi.readU16());
}});
rpc.exports = { stat: function(){ return [fired, mods]; } };
send('READY');
""" % (delta)

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
sc.on("message", lambda m, d: print("MSG", m, flush=True))
sc.load()
u32 = ctypes.windll.user32
u32.SetProcessDPIAware()
time.sleep(0.3)
u32.SetCursorPos(1500, 900)
time.sleep(1.0)
print("STAT", sc.exports_sync.stat())
s.detach()
time.sleep(0.6)
subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                "probe_shotwin.ps1", str(pid), tag], check=False)
