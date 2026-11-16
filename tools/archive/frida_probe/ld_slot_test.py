"""Slot-level test: hook FUN_0041d020, scale font table fields, screenshot with active window.

Usage: ld_slot_test.py <pid> <mode:adv|pitch|both|none> <gadv> <gpitch> <tag>
"""
import frida, time, sys, ctypes, subprocess

pid = int(sys.argv[1]); mode = sys.argv[2]
gadv = float(sys.argv[3]); gpitch = float(sys.argv[4]); tag = sys.argv[5]
shotmode = sys.argv[6] if len(sys.argv) > 6 else "screen"

def shot(pid_, tag_):
    script = "shot_activate.ps1" if shotmode == "screen" else "probe_shotwin.ps1"
    args = ["-Pid_", str(pid_), "-Tag", tag_] if shotmode == "screen" else [str(pid_), tag_]
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    script] + args, check=False)

u32 = ctypes.windll.user32
u32.SetProcessDPIAware()

def main_window(pid_):
    res = []
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(h, p):
        p_ = ctypes.c_uint(); u32.GetWindowThreadProcessId(h, ctypes.byref(p_))
        if p_.value == pid_ and u32.IsWindowVisible(h):
            r = ctypes.create_string_buffer(16); u32.GetWindowRect(h, r)
            l, t, rr, bb = ctypes.cast(r, ctypes.POINTER(ctypes.c_int))[:4]
            res.append((max(0, (rr-l)*(bb-t)), h))
        return True
    u32.EnumWindows(WNDENUMPROC(cb), None)
    return max(res)[1] if res else None

h = main_window(pid)
if mode == 'none':
    shot(pid, tag)
    print("detached", flush=True)
    sys.exit(0)

u32.ShowWindow(h, 9); u32.SetWindowPos(h, -1, 0, 0, 0, 0, 3); u32.SetForegroundWindow(h)


import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
js = open(os.path.join(_ROOT, 'tools', 'archive', 'frida_probe', 'slot_test.js')).read() % (mode, gadv, gpitch)
s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
msgs = []
sc.on("message", lambda m, d: print("MSG", m.get("payload") if isinstance(m, dict) else m, flush=True))
sc.load()
u32.SetForegroundWindow(h)
u32.SetCursorPos(1200, 900)
time.sleep(1.0)
print("fonts mutated:", sc.exports_sync.stat(), flush=True)
shot(pid, tag)
sc.exports_sync.restore()
time.sleep(0.3)
s.detach()
print("detached", flush=True)
