var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var done = false;
Interceptor.attach(base.add(0x77220), { onEnter: function(a){
  var thisp = this.context.ecx, esp = this.context.esp;
  var a0 = esp.add(4).readU32(), a2 = esp.add(0xc).readU32();
  if (a2 <= 0 || a2 > 20000) return;
  var V = thisp.add(4).readPointer().add(a0*32);
  if (!done) { done = true;
    send('before x=' + V.readFloat() + ' u=' + V.add(0x10).readFloat() + ' base=' + thisp.add(4).readPointer());
  }
  for (var i=0;i<a2;i++){
    var r = V.add(i*32);
    r.writeFloat(0);              // x
    r.add(4).writeFloat(0);       // y
  }
  if (done && a2 > 0 && this.cnt === undefined) { this.cnt = 1;
    send('after x=' + V.readFloat() + ' y=' + V.add(4).readFloat());
  }
}}, {});
send('READY');
