// Hook FUN_0041d020 (text emitter) and scale the font table fields ONCE per font object.
// MODE: 'adv' (slot4 = advance), 'pitch' (font+0x40), 'both'.
// No rollback on draw: geometry is rebuilt from the table every frame.
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var MODE = '%s', GA = %f, GP = %f;
var done = {};
var saved = [];   // [addr, original 4-byte int] for restore on detach
var logged = 0;

function mulInt(p, g) {
  var v = p.readInt();
  var n = Math.round(v * g);
  saved.push([p, p.readByteArray(4)]);
  p.writeInt(n);
  return [v, n];
}

Interceptor.attach(base.add(0x1d020), { onEnter: function (a) {
  var obj = this.context.ecx;
  var key = obj.toString();
  if (done[key]) return;
  // flag +0x58 == 0 means UV-prepare (FUN_0041cc90) is about to run in this call;
  // mutate only after UV rects are normalized, so UV stays from the full cell.
  if (obj.add(0x58).readU8() === 0) return;
  done[key] = true;

  var pitch = obj.add(0x40).readInt();
  var s = 'obj=' + obj + ' pitch=' + pitch;
  try { s += ' name=' + obj.add(0x48).readPointer().readCString(); } catch (e) { s += ' name=?'; }
  // sample glyph 0x41 ('A') and 0xC0 (first Cyrillic in uncial sheets)
  var recA = obj.add(0x5c + 0x41 * 20);
  s += ' A[u0=' + recA.readFloat() + ' v0=' + recA.add(4).readFloat()
     + ' u1=' + recA.add(8).readFloat() + ' v1=' + recA.add(0xc).readFloat()
     + ' adv=' + recA.add(0x10).readInt() + ']';

  if (MODE === 'pitch' || MODE === 'both') {
    var r = mulInt(obj.add(0x40), GP);
    s += ' pitch ' + r[0] + '->' + r[1];
  }
  if (MODE === 'adv' || MODE === 'both') {
    var n0 = 0, n1 = 0;
    for (var i = 0; i < 256; i++) {
      var p = obj.add(0x5c + i * 20 + 0x10);
      var v = p.readInt();
      if (v === 0) continue;
      var r = mulInt(p, GA);
      if (i === 0x41) { n0 = r[0]; n1 = r[1]; }
    }
    s += ' advA=' + n0 + '->' + n1;
  }
  if (logged < 12) { logged++; send(s); }
}});

rpc.exports = {
  restore: function () {
    for (var i = saved.length - 1; i >= 0; i--) saved[i][0].writeByteArray(saved[i][1]);
    saved = [];
    return 'restored';
  },
  stat: function () { return Object.keys(done).length; }
};
send('READY');
