var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var MODE = '%s', S = %f, D = %f;
Interceptor.attach(base.add(0x70932), { onEnter: function(a){
  var ecx = this.context.ecx, esi = this.context.esi, edi = this.context.edi;
  var V = esi.sub(0x10), n = ecx;
  if (n <= 0 || n > 64) return;
  var saved = [];
  var xs = [], ys = [];
  for (var i=0;i<n;i++){
    var idx = edi.add(i*2).readU16();
    var rec = V.add(idx*32);
    xs.push(rec.readFloat()); ys.push(rec.add(4).readFloat());
    saved.push([rec, rec.readByteArray(8)]);
  }
  if (MODE == 'shift') {
    for (var i=0;i<n;i++){ var rec=saved[i][0]; rec.writeFloat(xs[i]+D); }
  } else if (MODE == 'quad') {
    var cx = (Math.min.apply(null,xs)+Math.max.apply(null,xs))/2;
    var cy = (Math.min.apply(null,ys)+Math.max.apply(null,ys))/2;
    for (var i=0;i<n;i++){ var rec=saved[i][0];
      rec.writeFloat(cx + (xs[i]-cx)*S);
      rec.add(4).writeFloat(cy + (ys[i]-cy)*S);
    }
  }
  this.saved = saved;
}, onLeave: function(r){
  if (this.saved) { for (var i=0;i<this.saved.length;i++) this.saved[i][0].writeByteArray(this.saved[i][1]); this.saved = null; }
}});
send('READY');
