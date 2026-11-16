"""Live test: scale UI quads at the submit site 0x4708d0.

correct record layout: x@+0 y@+4 z@+8 color@+0xc u@+0x10 v@+0x14
this+4 = vertex buffer, this+0xc = vertex count
mode=obj  -> scale all vertices about the buffer's first vertex
mode=quad -> scale every quad about its own centre
"""
import frida, time, sys, ctypes, subprocess

pid = int(sys.argv[1])
scale = float(sys.argv[2])
mode = sys.argv[3] if len(sys.argv) > 3 else "obj"
tag = sys.argv[4] if len(sys.argv) > 4 else "lt"

js = """
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var S = %f, MODE = '%s';
var saved = [];
Interceptor.attach(base.add(0x708d0), { onEnter: function(a){
  var thisp = this.context.ecx;
  var vb = thisp.add(4).readPointer();
  var n = thisp.add(0xc).readInt();
  if (n <= 0 || n > 4000) return;
  var ax = vb.add(0).readFloat(), ay = vb.add(4).readFloat();
  var buf = vb.readByteArray(n*32);
  var dv = new DataView(buf);
  var out = new Uint8Array(n*32);
  out.set(new Uint8Array(buf));
  var ov = new DataView(out.buffer);
  if (MODE == 'obj') {
    for (var i=0;i<n;i++){
      var o=i*32;
      var x=dv.getFloat32(o,true), y=dv.getFloat32(o+4,true);
      ov.setFloat32(o, ax + (x-ax)*S, true);
      ov.setFloat32(o+4, ay + (y-ay)*S, true);
    }
  } else {
    for (var q=0;q+3<n;q+=4){
      var xs=[], ys=[];
      for (var j=0;j<4;j++){ var o=(q+j)*32; xs.push(dv.getFloat32(o,true)); ys.push(dv.getFloat32(o+4,true)); }
      var cx=(Math.min.apply(null,xs)+Math.max.apply(null,xs))/2, cy=(Math.min.apply(null,ys)+Math.max.apply(null,ys))/2;
      for (var j=0;j<4;j++){ var o=(q+j)*32;
        ov.setFloat32(o, cx + (xs[j]-cx)*S, true);
        ov.setFloat32(o+4, cy + (ys[j]-cy)*S, true);
      }
    }
  }
  Memory.writeByteArray(vb, out.buffer);
  saved.push([vb, buf]);
}, onLeave: function(r){
  while (saved.length) { var s = saved.pop(); Memory.writeByteArray(s[0], s[1]); }
}});
send('READY');
""" % (scale, mode)

s = frida.get_local_device().attach(pid)
sc = s.create_script(js)
sc.on("message", lambda m, d: print("MSG", m, flush=True))
sc.load()
u32 = ctypes.windll.user32
u32.SetProcessDPIAware()
time.sleep(0.3)
u32.SetCursorPos(1500, 900)
time.sleep(1.2)
s.detach()
time.sleep(0.6)
r = subprocess.run(["powershell", "-NoProfile", "-Command",
                    "(Get-Process -Id %d -ErrorAction SilentlyContinue) -ne $null" % pid], capture_output=True)
print("ALIVE", "True" in r.stdout.decode(errors="ignore"), flush=True)
subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                "probe_shotwin.ps1", str(pid), tag], check=False)
