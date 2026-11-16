function dump(name, base, len) {
  if (base.isNull()) { send(name + " = NULL"); return; }
  var buf = new Uint8Array(base.readByteArray(len));
  var dv = new DataView(buf.buffer);
  var out = [];
  for (var o = 0; o < len; o += 4) {
    var i = dv.getInt32(o, true);
    var fl = dv.getFloat32(o, true);
    out.push("+" + o.toString(16) + ":" + i + (Math.abs(fl - i) > 1e-6 && Math.abs(fl) > 1e-6 && Math.abs(fl) < 1e6 ? "(" + fl.toFixed(3) + ")" : ""));
  }
  send(name + " " + base + "\n  " + out.join(" "));
}
var base = null;
Process.enumerateModules().forEach(function (m) {
  if (m.name.toLowerCase().indexOf("lethaldreams.exe") >= 0) base = m.base;
});
var cc = base.add(0x11cc7c).readPointer();
var d4 = base.add(0x11d490).readPointer();
dump("cc7c", cc, 0x80);
dump("d490", d4, 0x40);
var font = null;
Interceptor.attach(base.add(0x1d020), {
  onEnter: function () { if (font === null) { font = this.context.ecx; send("FONT " + font); dump("font", font, 0x1480 > 0x600 ? 0x60 : 0x60); } }
});
setTimeout(function () { send("done font=" + font); }, 1500);
