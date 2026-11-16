import frida, time, sys, ctypes
pid=int(sys.argv[1])
js="""
var base=Process.findModuleByName('LETHALDREAMS.EXE').base;
var out=[],n=0;
Interceptor.attach(base.add(0x708d0),{onEnter:function(a){
  if(n++>3) return;
  var ecx=this.context.ecx, esp=this.context.esp;
  var s='ecx='+ecx+' args=';
  for(var i=1;i<=5;i++) s+=esp.add(i*4).readU32()+' ';
  s+=' [ecx]='+ecx.readU32()+' [ecx+4]='+ecx.add(4).readU32()+' [ecx+8]='+ecx.add(8).readU32()+' [ecx+c]='+ecx.add(0xc).readU32();
  out.push(s);
}});
rpc.exports={get:function(){return out;}};
send('READY');
"""
s=frida.get_local_device().attach(pid); sc=s.create_script(js)
sc.on('message',lambda m,d:print('MSG',m)); sc.load()
u32=ctypes.windll.user32; u32.SetProcessDPIAware()
time.sleep(0.3)
for i in range(4): u32.SetCursorPos(1200+i*30,900); time.sleep(0.2)
for l in sc.exports_sync.get(): print(l)
s.detach()
