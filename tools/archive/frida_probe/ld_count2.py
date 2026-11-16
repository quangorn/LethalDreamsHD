import frida, time, sys, ctypes
pid=int(sys.argv[1])
js="""
var base=Process.findModuleByName('LETHALDREAMS.EXE').base;
var sites={0x706e8:0,0x707b7:0,0x708d0:0,0x70932:0,0x77220:0,0x7726f:0};
var log=[];
Object.keys(sites).forEach(function(k){
  Interceptor.attach(base.add(parseInt(k)),{onEnter:function(){
    sites[k]++;
  }});
});
// sample geometry of class B submits
var b1=null;
Interceptor.attach(base.add(0x707b7),{onEnter:function(){
  if(b1) return; var esp=this.context.esp; var cnt=esp.add(8).readU32();
  var idx=esp.add(0xc).readPointer();
  b1='707b7 cnt='+cnt+' idxptr='+idx;
}});
Interceptor.attach(base.add(0x706e8),{onEnter:function(){
  if(b1) return; var esp=this.context.esp; var cnt=esp.add(0xc).readU32();
  b1='706e8 cnt='+cnt;
}});
rpc.exports={stat:function(){return [sites,b1];}};
send('READY');
"""
s=frida.get_local_device().attach(pid); sc=s.create_script(js)
sc.on('message',lambda m,d:print('MSG',m)); sc.load()
u32=ctypes.windll.user32; u32.SetProcessDPIAware()
time.sleep(0.4)
for i in range(6): u32.SetCursorPos(1100+i*30,900); time.sleep(0.2)
print(sc.exports_sync.stat())
s.detach()
