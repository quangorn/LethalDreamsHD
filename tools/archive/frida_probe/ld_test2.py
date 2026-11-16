"""Attach vertex-scale hook, click 'Изменить', capture the settings dialog, detach."""
import frida, time, sys, ctypes, subprocess

pid = int(sys.argv[1]); scale = float(sys.argv[2]); mode = sys.argv[3]; tag = sys.argv[4]

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
u32.ShowWindow(h, 9); u32.SetWindowPos(h, -1, 0, 0, 0, 0, 3); u32.SetForegroundWindow(h)


import os
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
js = open(os.path.join(_ROOT, 'tools', 'archive', 'frida_probe', 'scale_hook.js')).read() % (scale, mode)
s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
sc.on("message", lambda m, d: print("MSG", m.get("payload") if isinstance(m, dict) else m, flush=True))
sc.load()
u32.SetForegroundWindow(h)
u32.SetCursorPos(2400, 1750)
time.sleep(1.0)
u32.mouse_event(0x2, 0, 0, 0, 0); time.sleep(0.08); u32.mouse_event(0x4, 0, 0, 0, 0)
time.sleep(2.0)
subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                "shot_activate.ps1", "-Pid_", str(pid), "-Tag", tag, "-NoCursor"], check=False)
time.sleep(0.2)
u32.SetCursorPos(4000, 20)
time.sleep(0.4)
s.detach()
print("detached", flush=True)
