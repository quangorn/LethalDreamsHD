var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var D = %f, S = %f, MODE = '%s';
Interceptor.attach(base.add(0x77220), { onEnter: function(a){
  var thisp = this.context.ecx, esp = this.context.esp;
  var a0 = esp.add(4).readU32(), a2 = esp.add(0xc).readU32();
  if (a2 <= 0 || a2 > 20000) return;
  var V = thisp.add(4).readPointer().add(a0*32);
  for (var i=0;i<a2;i++){
    var r = V.add(i*32);
    if (MODE == 'shift') r.writeFloat(r.readFloat() + D);
    else { r.writeFloat(r.readFloat() * S); r.add(4).writeFloat(r.add(4).readFloat() * S); }
  }
}});
send('READY');
