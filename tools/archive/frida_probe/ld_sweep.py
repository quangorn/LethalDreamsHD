import frida, json, time, sys, ctypes, subprocess, re

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import cavegen

variants = sys.argv[1:]
siteb = list(cavegen.site_bytes())

def launch():
    r = subprocess.run(["powershell","-NoProfile","-ExecutionPolicy","Bypass","-File",
                        "probe_dlgshot.ps1","3840","2160","1","sweep","-NoClick"],
                       capture_output=True)
    m = re.search(r"pid=(\d+)", r.stdout.decode(errors="ignore"))
    return int(m.group(1)) if m else None

def kill(pid):
    subprocess.run(["powershell","-NoProfile","-Command",
                    "Get-Process -Id %d -ErrorAction SilentlyContinue | Stop-Process -Force" % pid])
    time.sleep(2)

u32 = ctypes.windll.user32
u32.SetProcessDPIAware()

for v in variants:
    parts = cavegen.PARTS[v]
    cave = list(cavegen.build(parts=parts))
    pid = launch()
    if pid is None:
        print("VAR", v, "LAUNCH FAIL"); continue
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
""" % (json.dumps(cave), json.dumps(siteb))
    try:
        s = frida.get_local_device().attach(pid)
        sc = s.create_script(js)
        sc.on("message", lambda m,d: print("MSG", json.dumps(m,ensure_ascii=False)[:120], flush=True))
        sc.load()
        for i in range(12):
            u32.SetCursorPos(1500+i*40, 1200+(i%12)*25)
            time.sleep(0.3)
        time.sleep(1.5)
        r = subprocess.run(["powershell","-NoProfile","-Command",
                            "(Get-Process -Id %d -ErrorAction SilentlyContinue) -ne $null" % pid], capture_output=True)
        alive = "True" in r.stdout.decode(errors="ignore")
        print("VAR", v, "len", len(cave), "ALIVE", alive, flush=True)
        try: s.detach()
        except Exception as e: pass
        kill(pid)
    except Exception as e:
        print("VAR", v, "ATTACH FAIL", e)
        kill(pid)
