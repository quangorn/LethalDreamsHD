# Builds cave as a .s file assembled by gcc -m32.
# Record base = esi + 0xc + idx*32; fields: f0=0, x=f1 (+4), y=f2 (+8), w=-1 (f3), color (f4), flags f5..f7
# => from esi without header: idx*32 + 16 = x, + 20 = y.
import subprocess, os, sys

RET = 0x70938      # 0x470938 - 0x400000
SLOT = 0x51cd90

def src(cave_off, k75=0x3f400000, k25=0x3e800000, lo=None, hi=None, loy=None, hiy=None):
    # filter: lo < min(vertex x) < hi, loy < min(vertex y) < hiy (float bits, positive floats)
    init = "movl $0x7f800000, (%esp)\nmovl $0x7f800000, 20(%esp)\n"
    inloop = """movss (%esp), %xmm0
minss 16(%esi,%edx,1), %xmm0
movss %xmm0, (%esp)
movss 20(%esp), %xmm0
minss 20(%esi,%edx,1), %xmm0
movss %xmm0, 20(%esp)
"""
    check = f"""cmpl $0x{lo:08x}, (%esp)
jle .Lrestore
cmpl $0x{hi:08x}, (%esp)
jge .Lrestore
cmpl $0x{loy:08x}, 20(%esp)
jle .Lrestore
cmpl $0x{hiy:08x}, 20(%esp)
jge .Lrestore
"""
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
movl $0x{k25:08x}, 16(%esp)
cmpl $4, %ecx
jne .Lrestore
xorps %xmm0, %xmm0
movss %xmm0, 4(%esp)
movss %xmm0, 8(%esp)
{init}movl $4, %eax
.Lloop1:
dec %eax
movzwl (%edi,%eax,2), %edx
imull $32, %edx, %edx
{inloop}movss 16(%esi,%edx,1), %xmm0
addss 4(%esp), %xmm0
movss %xmm0, 4(%esp)
movss 20(%esi,%edx,1), %xmm0
addss 8(%esp), %xmm0
movss %xmm0, 8(%esp)
test %eax, %eax
jne .Lloop1
{check}movss 4(%esp), %xmm0
mulss 16(%esp), %xmm0
movss %xmm0, 4(%esp)
movss 8(%esp), %xmm0
mulss 16(%esp), %xmm0
movss %xmm0, 8(%esp)
movl $4, %eax
.Lloop2:
dec %eax
movzwl (%edi,%eax,2), %edx
imull $32, %edx, %edx
movss 16(%esi,%edx,1), %xmm0
mulss 12(%esp), %xmm0
movss 4(%esp), %xmm1
mulss 16(%esp), %xmm1
addss %xmm1, %xmm0
movss %xmm0, 16(%esi,%edx,1)
movss 20(%esi,%edx,1), %xmm0
mulss 12(%esp), %xmm0
movss 8(%esp), %xmm1
mulss 16(%esp), %xmm1
addss %xmm1, %xmm0
movss %xmm0, 20(%esi,%edx,1)
test %eax, %eax
jne .Lloop2
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
    # extract .text bytes via objcopy
    r2 = subprocess.run(["objcopy", "-j", ".text", "-O", "binary",
                         os.path.join(paths.cache_dir('cave'), 'cave.o'), out_bin], capture_output=True)
    if r2.returncode != 0:
        print("objcopy:", r2.stderr.decode(errors="ignore")); return None
    data = open(out_bin, "rb").read()
    print("bin total", len(data), "cave bytes at tail from off", cave_off, "=", len(data) - cave_off)
    return data[cave_off:]

def relpatch(cave, cave_off):
    i = cave.find(bytes([0xff, 0x15, 0x90, 0xcd, 0x51, 0x00]))
    jpos = i + 6
    assert cave[jpos] == 0xe9, ("jmp byte", hex(cave[jpos]))
    return cave[:jpos+1] + (0x470938 - (0x400000 + cave_off + jpos + 5)).to_bytes(4, "little", signed=True)

if __name__ == "__main__":
    # text strip filter: minx in (537,539), miny in (398,400)
    import struct
    b = lambda v: struct.unpack(">I", struct.pack(">f", float(v)))[0]
    cave = build(0xf5201, os.path.join(paths.cache_dir('cave'), 'cave_rt.bin'),
                 lo=b(537), hi=b(539), loy=b(398), hiy=b(400))
    cave = relpatch(cave, 0xf5201)
    open(os.path.join(paths.cache_dir('cave'), 'cave_rt2.bin'), "wb").write(cave)
    print("cave len", len(cave))
