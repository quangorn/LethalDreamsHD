"""Launch game at given mode, dump candidate renderer objects, detach."""
import frida, time, sys, subprocess, os

W, H, F = 3840, 2160, 1
if len(sys.argv) > 3:
    W, H, F = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])

cfg = "%d %d 32 %d" % (W, H, F)

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
for p in (os.path.join(_ROOT, 'hd.cfg'), paths.game_path('hd.cfg')):
    open(p, "w").write(cfg + "\n")

subprocess.run(["taskkill", "/F", "/IM", "LETHALDREAMS.EXE"], capture_output=True)
time.sleep(1)
p = subprocess.Popen([paths.game_path('LETHALDREAMS.EXE')], cwd=paths.install_dir())
time.sleep(8)

try:
    s = frida.get_local_device().attach(p.pid)
    sc = s.create_script(open(os.path.join(_ROOT, 'tools', 'archive', 'frida_probe', 'obj.js')).read())
    sc.on("message", lambda m, d: print("MSG", m, flush=True))
    sc.load()
    time.sleep(2.0)
    s.detach()
finally:
    print("pid", p.pid)
