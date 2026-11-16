import frida, json, time, sys, ctypes, subprocess

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import cavegen

pid = int(sys.argv[1])
level = int(sys.argv[2])
cave = list(cavegen.build(level=level))
siteb = list(cavegen.site_bytes())

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var site = base.add(0x70932);
var cavebase = base.add(0xf5201);
var cave = %s;
var siteb = %s;
Memory.patchCode(site, 6, function(cp){
  for (var i=0;i<6;i++){ cp.add(i).writeU8(siteb[i]); }
});
Memory.patchCode(cavebase, 265, function(cp){
  for (var i=0;i<cave.length;i++){ cp.add(i).writeU8(cave[i]); }
});
var n1=0;
Interceptor.attach(cavebase, { onEnter: function(a){ n1++; } });
setInterval(function(){ send('N1=' + n1); }, 2000);
""" % (json.dumps(cave), json.dumps(siteb))

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
count = [0]
def h(m, d):
    p = m.get("payload")
    if isinstance(p, str) and p.startswith("N1"):
        count[0] = int(p.split("=")[1])
        print("MSG", p, flush=True)
sc.on("message", h)
sc.load()
u32 = ctypes.windll.user32
u32.SetProcessDPIAware()
for i in range(10):
    u32.SetCursorPos(1500+i*40, 1200+(i%10)*25)
    time.sleep(0.3)
time.sleep(2.5)
r = subprocess.run(["powershell","-NoProfile","-Command","(Get-Process -Id %d -ErrorAction SilentlyContinue) -ne $null" % pid], capture_output=True)
alive = "True" in r.stdout.decode(errors="ignore")
print("LEVEL", level, "N1=", count[0], "alive", alive, flush=True)
try: s.detach()
except Exception as e: print("detach", e)
