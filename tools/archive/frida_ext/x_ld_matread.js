'use strict';
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
function at(v){ return base.add(v - 0x400000); }
var seen = {};
Interceptor.attach(at(0x46f970), { onEnter: function (a) {
  var esp = this.context.esp;
  var idx = esp.add(4).readU32();
  if (idx !== 2) return;
  var mp = esp.add(8).readPointer();
  var cx = mp.readFloat();
  var cy = mp.add(5*4).readFloat();
  var key = mp.toString();
  if (!(key in seen)) {
    seen[key] = 1;
    send({ev:'READ', key: key, cx: cx, cy: cy});
  }
}});
send("READY");
