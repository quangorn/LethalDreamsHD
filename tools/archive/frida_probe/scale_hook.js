var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var S = %f, MODE = '%s';
Interceptor.attach(base.add(0x708d0), { onEnter: function(a){
  var thisp = this.context.ecx;
  var vb = thisp.add(4).readPointer();
  var n = thisp.add(0xc).readInt();
  if (n <= 0 || n > 8000) return;
  var ax = vb.readFloat(), ay = vb.add(4).readFloat();
  var buf = vb.readByteArray(n*32);
  var dv = new DataView(buf), out = new Uint8Array(n*32);
  out.set(new Uint8Array(buf));
  var ov = new DataView(out.buffer);
  if (MODE == 'ff') {
    for (var i=0;i<n*32;i++) out[i] = 0xff;
  } else if (MODE == 'obj') {
    for (var i=0;i<n;i++){
      var o = i*32;
      var x = dv.getFloat32(o,true), y = dv.getFloat32(o+4,true);
      ov.setFloat32(o, ax+(x-ax)*S, true); ov.setFloat32(o+4, ay+(y-ay)*S, true);
    }
  } else {
    for (var q=0;q+3<n;q+=4){
      var xs=[],ys=[];
      for (var j=0;j<4;j++){ xs.push(dv.getFloat32((q+j)*32,true)); ys.push(dv.getFloat32((q+j)*32+4,true)); }
      var cx=(Math.min.apply(null,xs)+Math.max.apply(null,xs))/2, cy=(Math.min.apply(null,ys)+Math.max.apply(null,ys))/2;
      for (var j=0;j<4;j++){
        ov.setFloat32((q+j)*32, cx+(xs[j]-cx)*S, true);
        ov.setFloat32((q+j)*32+4, cy+(ys[j]-cy)*S, true);
      }
    }
  }
  vb.writeByteArray(out.buffer);
  this.saved = [vb, buf];
}, onLeave: function(r){
  if (this.saved) { this.saved[0].writeByteArray(this.saved[1]); this.saved = null; }
}});
send('READY');
