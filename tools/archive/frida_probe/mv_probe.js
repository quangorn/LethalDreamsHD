var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var glGetFloatv = new NativeFunction(Process.getModuleByName('opengl32.dll').getExportByName('glGetFloatv'), 'void', ['uint', 'pointer']);
var buf = Memory.alloc(64);
var n = 0;
Interceptor.attach(base.add(0x77220), { onEnter: function(a){
  var thisp = this.context.ecx, esp = this.context.esp;
  var a0 = esp.add(4).readU32(), a2 = esp.add(0xc).readU32();
  if (a2 <= 0 || a2 > 20000) return;
  var V = thisp.add(4).readPointer().add(a0*32);
  var u = V.add(0x10).readFloat();
  if (n < 12 && u < 0.95 && u > 0.05) {
    n++;
    glGetFloatv(0x0BA6, buf);
    var m = [];
    for (var i=0;i<16;i++) m.push(buf.add(i*4).readFloat().toFixed(3));
    send('mv=' + m.join(',') + ' v0=' + V.readFloat().toFixed(1) + ',' + V.add(4).readFloat().toFixed(1));
  }
}});
send('READY');
