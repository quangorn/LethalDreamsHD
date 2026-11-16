import frida, sys, json

pid = int(sys.argv[1])
dur = int(sys.argv[2]) if len(sys.argv) > 2 else 8
s = frida.attach(pid)
scr = s.create_script(open("x_ld_matread.js").read())
scr.on("message", lambda m, d: print(json.dumps(m)))
scr.load()
import time
time.sleep(dur)
s.detach()
