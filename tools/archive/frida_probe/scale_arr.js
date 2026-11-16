var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var MODE = '%s', S = %f, D = %f;
var logged = 0;
Interceptor.attach(base.add(0x77220), { onEnter: function(a){
  var thisp = this.context.ecx;
  var esp = this.context.esp;
  var a0 = esp.add(4).readU32(), a1 = esp.add(8).readU32(), a2 = esp.add(0xc).readU32();
  var vb = thisp.add(4).readPointer();
  if (a2 <= 0 || a2 > 20000) return;
  var V = vb.add(a0*32);
  if (logged < 8) { logged++;
    var s = 'call a0=' + a0 + ' mode=' + a1 + ' n=' + a2 + ' base=' + vb;
    for (var i=0;i<Math.min(a2,4);i++){
      var r = V.add(i*32);
      s += ' [' + r.readFloat().toFixed(1) + ',' + r.add(4).readFloat().toFixed(1)
        + ' uv=' + r.add(0x10).readFloat().toFixed(3) + ',' + r.add(0x14).readFloat().toFixed(3) + ']';
    }
    send(s);
  }
  var saved = [];
  var xs = [], ys = [];
  for (var i=0;i<a2;i++){ var r=V.add(i*32); xs.push(r.readFloat()); ys.push(r.add(4).readFloat()); saved.push([r, r.readByteArray(8)]); }
  if (MODE == 'shift') {
    for (var i=0;i<a2;i++) saved[i][0].writeFloat(xs[i]+D);
  } else if (MODE == 'quad') {
    var cx=(Math.min.apply(null,xs)+Math.max.apply(null,xs))/2, cy=(Math.min.apply(null,ys)+Math.max.apply(null,ys))/2;
    for (var i=0;i<a2;i++){ saved[i][0].writeFloat(cx+(xs[i]-cx)*S); saved[i][0].add(4).writeFloat(cy+(ys[i]-cy)*S); }
  } else if (MODE == 'nan') {
    for (var i=0;i<a2;i++){ saved[i][0].writeFloat(NaN); saved[i][0].add(4).writeFloat(NaN); }
  } else if (MODE == 'y') {
    for (var i=0;i<a2;i++) saved[i][0].add(4).writeFloat(ys[i]*S);
  }
  this.saved = saved;
}, onLeave: function(r){
  if (this.saved) { for (var i=0;i<this.saved.length;i++) this.saved[i][0].writeByteArray(this.saved[i][1]); this.saved = null; }
}});
send('READY');
