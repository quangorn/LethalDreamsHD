# Record-level filter: scale w (+12) of records whose tex x (+16), y (+20) in band.
import subprocess, struct

RET = 0x70938
SLOT = 0x51cd90

def src(cave_off, k75=0x3f400000, lox=400.0, hix=610.0, loy=380.0, hiy=610.0):
    L = lambda v: struct.unpack(">I", struct.pack(">f", float(v)))[0]
    return f"""
.section .text
.fill {cave_off}, 1, 0
cave:
push %eax
push %ecx
push %edx
push %ebx
push %esi
push %edi
sub $24, %esp
movl $0x{k75:08x}, 12(%esp)
cmpl $4, %ecx
jne .Lrestore
movl $0x{L(lox):08x}, (%esp)
movl $0x{L(hix):08x}, 4(%esp)
movl $0x{L(loy):08x}, 8(%esp)
movl $0x{L(hiy):08x}, 16(%esp)
movl $4, %eax
.Lloop:
dec %eax
movzwl (%edi,%eax,2), %edx
imull $32, %edx, %edx
movl 16(%esi,%edx,1), %ecx
cmpl (%esp), %ecx
jle .Lnext
cmpl 4(%esp), %ecx
jge .Lnext
movl 20(%esi,%edx,1), %ecx
cmpl 8(%esp), %ecx
jle .Lnext
cmpl 16(%esp), %ecx
jge .Lnext
movss 12(%esi,%edx,1), %xmm0
mulss 12(%esp), %xmm0
movss %xmm0, 12(%esi,%edx,1)
.Lnext:
test %eax, %eax
jne .Lloop
.Lrestore:
add $24, %esp
pop %edi
pop %esi
pop %ebx
pop %edx
pop %ecx
pop %eax
call *{SLOT}
jmp {RET}
"""


import os
import sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
def build(cave_off, out_bin, **kw):
    s = src(cave_off, **kw)
    open(os.path.join(paths.cache_dir('cave'), 'cave.s'), "w").write(s)
    r = subprocess.run(["gcc", "-m32", "-c", os.path.join(paths.cache_dir('cave'), 'cave.s'),
                        "-o", os.path.join(paths.cache_dir('cave'), 'cave.o')], capture_output=True)
    if r.returncode != 0:
        print(r.stderr.decode(errors="ignore")); return None
    r2 = subprocess.run(["objcopy", "-j", ".text", "-O", "binary",
                         os.path.join(paths.cache_dir('cave'), 'cave.o'), out_bin], capture_output=True)
    if r2.returncode != 0:
        print("objcopy:", r2.stderr.decode(errors="ignore")); return None
    data = open(out_bin, "rb").read()
    return data[cave_off:]

def relpatch(cave, cave_off):
    i = cave.find(bytes([0xff, 0x15, 0x90, 0xcd, 0x51, 0x00]))
    jpos = i + 6
    assert cave[jpos] == 0xe9, ("jmp byte", hex(cave[jpos]))
    return cave[:jpos+1] + (0x470938 - (0x400000 + cave_off + jpos + 5)).to_bytes(4, "little", signed=True)

if __name__ == "__main__":
    cave = build(0xf5201, os.path.join(paths.cache_dir('cave'), 'cave_rt.bin'))
    cave = relpatch(cave, 0xf5201)
    open(os.path.join(paths.cache_dir('cave'), 'cave_rt2.bin'), "wb").write(cave)
    print("cave len", len(cave))
