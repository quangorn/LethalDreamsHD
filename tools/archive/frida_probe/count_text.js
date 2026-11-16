var base = Process.findModuleByName('LETHALDREAMS.EXE').base;
var sites = {'41d020':0,'41d460':0,'41c6b0':0,'41cc90':0,'471180':0};
Object.keys(sites).forEach(function(k){ Interceptor.attach(base.add(parseInt(k,16)-0x400000), { onEnter: function(){ sites[k]++; }}); });
setTimeout(function(){ send(JSON.stringify(sites)); }, 2500);
send('READY');
