"""Live vertex-scale test with the window kept in the foreground.

Usage: ld_final_test.py <pid> <scale> <mode:obj|quad> <tag> [hoverx hovery]
"""
import frida, time, sys, ctypes, subprocess

pid = int(sys.argv[1]); scale = float(sys.argv[2])
mode = sys.argv[3]; tag = sys.argv[4]
hx = int(sys.argv[5]) if len(sys.argv) > 5 else 2400
hy = int(sys.argv[6]) if len(sys.argv) > 6 else 1750

u32 = ctypes.windll.user32
u32.SetProcessDPIAware()

def main_window(pid_):
    res = []
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(h, p):
        p_ = ctypes.c_uint()
        u32.GetWindowThreadProcessId(h, ctypes.byref(p_))
        if p_.value == pid_ and u32.IsWindowVisible(h):
            r = ctypes.create_string_buffer(16)
            u32.GetWindowRect(h, r)
            l, t, rr, bb = ctypes.cast(r, ctypes.POINTER(ctypes.c_int))[:4]
            res.append((max(0, (rr-l)*(bb-t)), h))
        return True
    u32.EnumWindows(WNDENUMPROC(cb), None)
    return max(res)[1] if res else None

h = main_window(pid)
print("hwnd", h, flush=True)
u32.ShowWindow(h, 9)
u32.SetWindowPos(h, -1, 0, 0, 0, 0, 3)
u32.SetForegroundWindow(h)


import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
js = open(os.path.join(_ROOT, 'tools', 'archive', 'frida_probe', 'scale_hook.js')).read() % (scale, mode)

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
sc.on("message", lambda m, d: print("MSG", m.get("payload") if isinstance(m, dict) else m, flush=True))
sc.load()
u32.SetForegroundWindow(h)
u32.SetCursorPos(hx, hy)
time.sleep(1.5)
subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                "shot_activate.ps1", "-Pid_", str(pid), "-Tag", tag, "-NoCursor"], check=False)
time.sleep(0.3)
s.detach()
print("detached", flush=True)
