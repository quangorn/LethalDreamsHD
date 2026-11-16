import frida, json, time, sys, ctypes, subprocess

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import cavegen

pid = int(sys.argv[1])
key = sys.argv[2]
zone = int(sys.argv[3], 16)
tag = sys.argv[4]
scale = sys.argv[5] if len(sys.argv) > 5 else "75"
if scale == "id":
    cave = list(cavegen.build_scale(0x3f800000, 0x00000000, c=zone))
else:
    cave = list(cavegen.build(c=zone, parts=cavegen.PARTS[key]))
siteb = list(cavegen.site_bytes(c=zone))

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var site = base.add(0x70932);
var cavebase = base.add(ZONE_OFF);
var cave = %s;
var siteb = %s;
Memory.patchCode(site, 6, function(cp){
  for (var i=0;i<6;i++){ cp.add(i).writeU8(siteb[i]); }
});
Memory.patchCode(cavebase, cave.length, function(cp){
  for (var i=0;i<cave.length;i++){ cp.add(i).writeU8(cave[i]); }
});
""" % (json.dumps(cave), json.dumps(siteb))
js = js.replace("ZONE_OFF", "0x%x" % (zone - 0x400000))

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
sc.on("message", lambda m,d: print("MSG", json.dumps(m,ensure_ascii=False)[:120], flush=True))
sc.load()
u32 = ctypes.windll.user32
u32.SetProcessDPIAware()
for i in range(12):
    u32.SetCursorPos(1500+i*40, 1200+(i%12)*25)
    time.sleep(0.3)
s.detach()
time.sleep(0.5)
r = subprocess.run(["powershell","-NoProfile","-Command","(Get-Process -Id %d -ErrorAction SilentlyContinue) -ne $null" % pid], capture_output=True)
print("KEY", key, "ZONE", hex(zone), "len", len(cave), "ALIVE", "True" in r.stdout.decode(errors="ignore"), flush=True)
subprocess.run(["powershell","-NoProfile","-ExecutionPolicy","Bypass","-File","probe_shotwin.ps1",str(pid),tag],check=False)
