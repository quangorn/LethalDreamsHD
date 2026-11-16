# SSE2 cave generator for glyph shrink at site 0x470932.
# Site semantics: push edi; push 0x1403; push ecx; push edx; call [0x51cd90]; return 0x470938.
# Vertex record: stride 0x20, x float @ base+0xc, y float @ base+0x10, indices ushort @ edi, count ecx.
# Cave scales every vertex about quad center by 3/4: new = 0.75*v + 0.25*c.

SLOT_DE = 0x51cd90; RETURN = 0x470938; SITE = 0x470932; C = 0x4f5201
K75 = 0x3f400000
K25 = 0x3e800000
META = {}

PUSH = {0x00:0x50,0x01:0x51,0x02:0x52,0x03:0x53,0x06:0x56,0x07:0x57}
def push(r): return bytes([PUSH[r]])
def pop(r): return bytes([0x58+r])
def imm32(v): return v.to_bytes(4,"little",signed=False)

XORPS00 = b"\x0f\x5c\xc0"
MOVSS_XMM0_ESP   = b"\xf3\x0f\x10\x04\x24"
MOVSS_XMM0_ESP4  = b"\xf3\x0f\x10\x44\x24\x04"
MOVSS_ESP_XMM0   = b"\xf3\x0f\x11\x04\x24"
MOVSS_ESP4_XMM0  = b"\xf3\x0f\x11\x44\x24\x04"
MOVSS_XMM0_VX    = b"\xf3\x0f\x10\x44\x1e\x0c"
MOVSS_XMM0_VY    = b"\xf3\x0f\x10\x44\x1e\x10"
MOVSS_VX_XMM0    = b"\xf3\x0f\x11\x44\x1e\x0c"
MOVSS_VY_XMM0    = b"\xf3\x0f\x11\x44\x1e\x10"
ADDSS_XMM0_ESP   = b"\xf3\x0f\x58\x04\x24"
ADDSS_XMM0_ESP4  = b"\xf3\x0f\x58\x44\x24\x04"
CVTSI2SS_XMM1_ECX= b"\xf3\x0f\x2a\xc9"
DIVSS_XMM0_XMM1  = b"\xf3\x0f\x5e\xc1"
ADDSS_XMM0_XMM1  = b"\xf3\x0f\x58\xc1"

def movsse(regidx, opcode, modrm):
    b = bytearray(modrm)
    b[0] = (b[0] & 0xC7) | (regidx << 3)
    return b"\xf3\x0f" + opcode + bytes(b)

def mov_dword_esp(d, v):
    return b"\xc7\x44\x24" + bytes([d]) + imm32(v)

def build(c=C, parts=("subesp","consts","guard","zero","loop1","center","loop2")):
    code = bytearray()
    def emit(b): code.extend(b)
    for r in (0x00,0x01,0x02,0x03,0x06,0x07): emit(push(r))
    jmp_slots = []
    if "subesp" in parts:
        emit(b"\x83\xec\x10")
    if "consts" in parts:
        emit(mov_dword_esp(8, K75))
        emit(mov_dword_esp(12, K25))
    if "guard" in parts:
        emit(b"\x85\xc9")
        emit(b"\x0f\x84"); REL1=len(code); code.extend([0,0,0,0])
    else:
        emit(b"\xeb"); JUMP0=len(code); code.append(0)
    if "zero" in parts:
        emit(XORPS00)
        if "store" in parts:
            emit(MOVSS_ESP_XMM0); emit(MOVSS_ESP4_XMM0)
    if "loop1" in parts:
        emit(b"\x8b\xc1")
        L1 = len(code); META["L1"] = L1
        emit(b"\x48")
        emit(b"\x0f\xb7\x04\x47")
        emit(b"\x6b\xd8\x20")
        emit(MOVSS_XMM0_VX)
        if "addstore" in parts:
            emit(ADDSS_XMM0_ESP); emit(MOVSS_ESP_XMM0)
            emit(MOVSS_XMM0_VY); emit(ADDSS_XMM0_ESP4); emit(MOVSS_ESP4_XMM0)
        emit(b"\x85\xc0"); emit(b"\x75\x00"); code[len(code)-1]=(L1-len(code))&0xff
    if "center" in parts:
        META["center"] = len(code)
        emit(CVTSI2SS_XMM1_ECX)
        emit(MOVSS_XMM0_ESP); emit(DIVSS_XMM0_XMM1); emit(MOVSS_ESP_XMM0)
        emit(CVTSI2SS_XMM1_ECX)
        emit(MOVSS_XMM0_ESP4); emit(DIVSS_XMM0_XMM1); emit(MOVSS_ESP4_XMM0)
    if "loop2" in parts:
        emit(b"\x8b\xc1"); L2 = len(code); META["loop2"] = L2
        emit(b"\x48"); emit(b"\x0f\xb7\x04\x47"); emit(b"\x6b\xd8\x20")
        emit(MOVSS_XMM0_VX)
        emit(b"\xf3\x0f\x59\x44\x24\x08")
        emit(movsse(1,b"\x10",b"\x04\x24"))
        emit(b"\xf3\x0f\x59\x4c\x24\x0c")
        emit(ADDSS_XMM0_XMM1)
        emit(MOVSS_VX_XMM0)
        emit(MOVSS_XMM0_VY)
        emit(b"\xf3\x0f\x59\x44\x24\x08")
        emit(movsse(1,b"\x10",b"\x44\x24\x04"))
        emit(b"\xf3\x0f\x59\x4c\x24\x0c")
        emit(ADDSS_XMM0_XMM1)
        emit(MOVSS_VY_XMM0)
        emit(b"\x85\xc0"); emit(b"\x75\x00"); code[len(code)-1]=(L2-len(code))&0xff
    RESTORE = len(code)
    META["RESTORE"] = RESTORE
    if "guard" in parts:
        code[REL1:REL1+4] = (RESTORE-(REL1+4)).to_bytes(4,"little",signed=True)
    else:
        code[JUMP0] = (RESTORE-(JUMP0+1))&0xff
    emit(b"\x83\xc4\x10")
    for r in (0x07,0x06,0x03,0x02,0x01,0x00): emit(pop(r))
    # args already pushed by the site prologue (0x47092a..0x470931); cave replaces only the call
    emit(b"\xff\x15"+SLOT_DE.to_bytes(4,"little"))
    rel = RETURN - (c + len(code) + 5)
    emit(b"\xe9"+rel.to_bytes(4,"little",signed=True))
    return bytes(code)

PARTS = {
    "0":  (),
    "1":  ("subesp","consts"),
    "2":  ("subesp","consts","guard","zero"),
    "3":  ("subesp","consts","guard","zero","store"),
    "4":  ("subesp","consts","guard","zero","store","loop1"),
    "5":  ("subesp","consts","guard","zero","store","loop1","addstore"),
    "6":  ("subesp","consts","guard","zero","store","loop1","addstore","center"),
    "full":("subesp","consts","guard","zero","store","loop1","addstore","center","loop2"),
}

def build_scale(k75, k25, c=C):
    """identity/other scale: new = k75*v + k25*c"""
    global K75, K25
    K75, K25 = k75, k25
    return build(c=c, parts=PARTS["full"])

def site_bytes(c=C):
    siterel = c - (SITE+5)
    return bytes([0xE9]) + siterel.to_bytes(4,"little",signed=True) + b"\x90"


import os
import sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
if __name__ == "__main__":
    cave = build(parts=PARTS["full"])
    open(os.path.join(paths.cache_dir('cave'), 'cave.bin'),"wb").write(cave)
    print("cave len", len(cave))
