var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var MODE = '%s';
// kill the arrays path: glDrawArrays(count=0) by zeroing the pushed count arg
if (MODE == 'arr0' || MODE == 'both') {
  Interceptor.attach(base.add(0x77220), { onEnter: function(a){
    var esp = this.context.esp;
    if (esp.add(0xc).readU32() > 0) esp.add(0xc).writeU32(0);
  }});
}
// kill the elements path: count=ecx -> 0
if (MODE == 'el0' || MODE == 'both') {
  Interceptor.attach(base.add(0x70932), { onEnter: function(a){ this.context.ecx = 0; }});
}
send('READY');
