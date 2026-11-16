# Gate cave: only when foreground window client width == 3840 (4K), run the full-corruption
# scaling block (scale floats at esi+ebx+12 / esi+ebx+20 about their mean, 4 iterations, k=0.75).
# GetForegroundWindow IAT slot 0x4ec1c0, GetClientRect IAT slot 0x4ec1cc.
import subprocess, struct

RET = 0x70938
SLOT = 0x51cd90
GFW = 0x4ec1c0
GCR = 0x4ec1cc

def src(cave_off, k75=0x3f400000, k25=0x3e800000, width=3840):
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
movl $0x{k25:08x}, 16(%esp)
cmpl $4, %ecx
jne .Lrestore
call *{GFW}
push %esp
push %eax
call *{GCR}
add $8, %esp
cmpl ${width}, 8(%esp)
jne .Lrestore
xorps %xmm0, %xmm0
movss %xmm0, 20(%esp)
movss %xmm0, 24(%esp)
movl $4, %eax
.Lloop1:
dec %eax
movss 14(%esi), %xmm0
addss 20(%esp), %xmm0
movss %xmm0, 20(%esp)
movss 22(%esi), %xmm0
addss 24(%esp), %xmm0
movss %xmm0, 24(%esp)
test %eax, %eax
jne .Lloop1
cvtsi2ss %ecx, %xmm1
movss 20(%esp), %xmm0
divss %xmm1, %xmm0
movss %xmm0, 20(%esp)
cvtsi2ss %ecx, %xmm1
movss 24(%esp), %xmm0
divss %xmm1, %xmm0
movss %xmm0, 24(%esp)
movl $4, %eax
.Lloop2:
dec %eax
movss 14(%esi), %xmm0
mulss 12(%esp), %xmm0
movss 20(%esp), %xmm1
mulss 16(%esp), %xmm1
addss %xmm1, %xmm0
movss %xmm0, 14(%esi)
movss 22(%esi), %xmm0
mulss 12(%esp), %xmm0
movss 24(%esp), %xmm1
mulss 16(%esp), %xmm1
addss %xmm1, %xmm0
movss %xmm0, 22(%esi)
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
