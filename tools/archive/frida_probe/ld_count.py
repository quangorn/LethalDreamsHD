import frida, time, sys, ctypes
pid=int(sys.argv[1])
js="""
var base=Process.findModuleByName('LETHALDREAMS.EXE').base;
var c1=0,c2=0,c3=0,l1=[],l2=[];
Interceptor.attach(base.add(0x708d0),{onEnter:function(a){c1++; if(c1<=5){var t=this.context.ecx; l1.push('708d0 n='+t.add(0xc).readInt());}}});
Interceptor.attach(base.add(0x70932),{onEnter:function(a){c2++;}});
Interceptor.attach(base.add(0x77220),{onEnter:function(a){c3++; if(c3<=5){var t=this.context.ecx; l2.push('77220 n='+t.add(0xc).readInt());}}});
rpc.exports={stat:function(){return [c1,c2,c3,l1.join('|'),l2.join('|')];}};
send('READY');
"""
s=frida.get_local_device().attach(pid); sc=s.create_script(js)
sc.on('message',lambda m,d:print('MSG',m))
sc.load(); u32=ctypes.windll.user32; u32.SetProcessDPIAware()
h=None
time.sleep(0.3)
for i in range(6):
    u32.SetCursorPos(1200+i*40,900); time.sleep(0.25)
print("STAT", sc.exports_sync.stat())
s.detach()
