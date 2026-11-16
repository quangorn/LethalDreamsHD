"""Run slot_probe.js against pid, print font-object facts."""
import frida, time, sys

pid = int(sys.argv[1])
u32 = ctypes_mod = __import__("ctypes").windll.user32
u32.SetProcessDPIAware()

s = frida.get_local_device().attach(pid)

import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sc = s.create_script(open(os.path.join(_ROOT, 'tools', 'archive', 'frida_probe', 'slot_probe.js')).read())
sc.on("message", lambda m, d: print("MSG", m, flush=True))
sc.load()
time.sleep(1.5)
print("calls:", sc.exports_sync.stat())
s.detach()
