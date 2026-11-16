# Builds cave as .s assembled by gcc -m32. Record base = esi + idx*32.
# Fields: +0 f0=0, +4 u, +8 v, +12 w(-1), +16 x, +20 y, +24.. flags.
# Filter: scale w (+12) by k for quads whose x-extent and y-extent <= maxext (glyph quads).
import subprocess, struct

RET = 0x70938
SLOT = 0x51cd90
NEG_INF = 0xff800000
POS_INF = 0x7f800000

def src(cave_off, k75=0x3f400000, maxext=16.0):
    extbits = struct.unpack(">I", struct.pack(">f", maxext))[0]
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
sub $28, %esp
movl $0x{k75:08x}, 12(%esp)
cmpl $4, %ecx
jne .Lrestore
movl $0x{POS_INF:08x}, (%esp)
movl $0x{POS_INF:08x}, 4(%esp)
movl $0x{NEG_INF:08x}, 8(%esp)
movl $0x{NEG_INF:08x}, 16(%esp)
movl $4, %eax
.Lloop1:
dec %eax
movzwl (%edi,%eax,2), %edx
imull $32, %edx, %edx
movss (%esp), %xmm0
minss 16(%esi,%edx,1), %xmm0
movss %xmm0, (%esp)
movss 8(%esp), %xmm0
maxss 16(%esi,%edx,1), %xmm0
movss %xmm0, 8(%esp)
movss 4(%esp), %xmm0
minss 20(%esi,%edx,1), %xmm0
movss %xmm0, 4(%esp)
movss 16(%esp), %xmm0
maxss 20(%esi,%edx,1), %xmm0
movss %xmm0, 16(%esp)
test %eax, %eax
jne .Lloop1
movss 8(%esp), %xmm0
subss (%esp), %xmm0
movss %xmm0, 24(%esp)
movss 16(%esp), %xmm0
subss 4(%esp), %xmm0
movss %xmm0, 20(%esp)
cmpl $0x{extbits:08x}, 24(%esp)
jge .Lrestore
cmpl $0x{extbits:08x}, 20(%esp)
jge .Lrestore
movl $4, %eax
.Lloop2:
dec %eax
movzwl (%edi,%eax,2), %edx
imull $32, %edx, %edx
movss 12(%esi,%edx,1), %xmm0
mulss 12(%esp), %xmm0
movss %xmm0, 12(%esi,%edx,1)
test %eax, %eax
jne .Lloop2
.Lrestore:
add $28, %esp
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
