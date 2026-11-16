import frida, json, time, sys, ctypes, subprocess

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import cavegen

pid = int(sys.argv[1])
tag = sys.argv[2]
dur = float(sys.argv[3]) if len(sys.argv) > 3 else 6.0

cave = list(cavegen.build())
siteb = list(cavegen.site_bytes())

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var site = base.add(0x70932);
var cavebase = base.add(0xf5201);
var cave = %s;
var siteb = %s;
Memory.patchCode(cavebase, 265, function(cp){
  for (var i=0;i<cave.length;i++){ cp.add(i).writeU8(cave[i]); }
});
Memory.patchCode(site, 6, function(cp){
  for (var i=0;i<6;i++){ cp.add(i).writeU8(siteb[i]); }
});
var n1=0;
Interceptor.attach(cavebase, { onEnter: function(a){ n1++; } });
setInterval(function(){ send('N1=' + n1); }, 3000);
""" % (json.dumps(cave), json.dumps(siteb))

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
def h(m, d):
    p = m.get("payload")
    if isinstance(p, str) and p.startswith("N1"):
        print("MSG", p, flush=True)
sc.on("message", h)
sc.load()
u32 = ctypes.windll.user32
u32.SetProcessDPIAware()
steps = max(4, int(dur/0.3))
for i in range(steps):
    u32.SetCursorPos(1500+i*40, 1200+(i%10)*25)
    time.sleep(0.3)
s.detach()
print("DETACHED", flush=True)
time.sleep(0.4)
subprocess.run(["powershell","-NoProfile","-ExecutionPolicy","Bypass","-File","probe_shotwin.ps1",str(pid),tag],check=False)
