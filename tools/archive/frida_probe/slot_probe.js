// Count FUN_0041d020 calls; log font objects field by field to find which read fails.
var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var n = 0;
var seen = {};
function probePointers(obj) {
  var out = [];
  var steps = [
    ['flag', function (p) { return p.add(0x58).readU8(); }],
    ['pitch', function (p) { return p.add(0x40).readInt(); }],
    ['w', function (p) { return p.add(0x54).readFloat(); }],
    ['u0', function (p) { return p.add(0x5c + 0x41 * 20).readFloat(); }],
    ['v0', function (p) { return p.add(0x5c + 0x41 * 20 + 4).readFloat(); }],
    ['u1', function (p) { return p.add(0x5c + 0x41 * 20 + 8).readFloat(); }],
    ['v1', function (p) { return p.add(0x5c + 0x41 * 20 + 0xc).readFloat(); }],
    ['adv', function (p) { return p.add(0x5c + 0x41 * 20 + 0x10).readInt(); }],
    ['name', function (p) { return p.add(0x48).readPointer().readCString(); }]
  ];
  for (var i = 0; i < steps.length; i++) {
    try { out.push(steps[i][0] + '=' + steps[i][1](obj)); }
    catch (e) { out.push(steps[i][0] + '=ERR ' + e.message); }
  }
  return out.join(' ');
}
Interceptor.attach(base.add(0x1d020), { onEnter: function (a) {
  n++;
  var obj = this.context.ecx;
  var key = obj.toString();
  if (seen[key]) return;
  seen[key] = 1;
  send('LOG obj=' + key + ' ' + probePointers(obj));
}});
rpc.exports = { stat: function () { return n; } };
send('READY');
