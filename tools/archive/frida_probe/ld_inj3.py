import frida, json, time, sys, ctypes, subprocess

pid = int(sys.argv[1])
binfile = sys.argv[2]
zone_off = int(sys.argv[3], 16)   # file offset of cave (VA = 0x400000 + zone_off)
tag = sys.argv[4]

cave = open(binfile, "rb").read()

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var site = base.add(0x70932);
var cavebase = base.add(%d);
var cave = %s;
var siteb = %s;
Memory.patchCode(site, 6, function(cp){
  for (var i=0;i<6;i++){ cp.add(i).writeU8(siteb[i]); }
});
Memory.patchCode(cavebase, cave.length, function(cp){
  for (var i=0;i<cave.length;i++){ cp.add(i).writeU8(cave[i]); }
});
""" % (zone_off, json.dumps(list(cave)), json.dumps(list(bytes([0xE9]) + (zone_off + 0x400000 - (0x470932 + 5)).to_bytes(4, "little", signed=True) + b"\x90")))

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
sc.on("message", lambda m, d: print("MSG", json.dumps(m, ensure_ascii=False)[:120], flush=True))
sc.load()
u32 = ctypes.windll.user32
u32.SetProcessDPIAware()
for i in range(12):
    u32.SetCursorPos(1500 + i * 40, 1200 + (i % 12) * 25)
    time.sleep(0.3)
s.detach()
time.sleep(0.5)
r = subprocess.run(["powershell", "-NoProfile", "-Command",
                    "(Get-Process -Id %d -ErrorAction SilentlyContinue) -ne $null" % pid], capture_output=True)
print("ALIVE", "True" in r.stdout.decode(errors="ignore"), flush=True)
subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                "probe_shotwin.ps1", str(pid), tag], check=False)
