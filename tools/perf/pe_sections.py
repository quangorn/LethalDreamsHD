# pe_sections.py — dump PE sections + find zero-runs (executable & writable)
# Usage: python3 pe_sections.py <exe-path>
import struct, sys

if len(sys.argv) < 2:
    print("usage: python3 pe_sections.py <exe-path>")
    sys.exit(1)
path = sys.argv[1]
d = open(path, 'rb').read()
e_lfanew = struct.unpack_from('<I', d, 0x3c)[0]
coff = e_lfanew + 4
machine, nsec, _, _, _, opt_sz, _ = struct.unpack_from('<HHIIIHH', d, coff)
opt = coff + 20
magic = struct.unpack_from('<H', d, opt)[0]
pe32p = magic == 0x20b
print(f"PE32+={pe32p} sections={nsec}")
sec_off = opt + opt_sz
for i in range(nsec):
    name, vsize, vaddr, rawsize, rawptr = struct.unpack_from('<8sIIII', d, sec_off + i*40)
    chars = struct.unpack_from('<I', d, sec_off + i*40 + 36)[0]
    nm = name.rstrip(b'\x00').decode('ascii', 'replace')
    flags = []
    if chars & 0x20000000: flags.append('EXEC')
    if chars & 0x80000000: flags.append('WRITE')
    if chars & 0x40000000: flags.append('READ')
    print(f"{nm:8s} VA=0x{vaddr:08x} VS=0x{vsize:08x} RAW=0x{rawptr:08x} RS=0x{rawsize:08x} chars=0x{chars:08x} {'+'.join(flags)}")
    # zero-run at the end of raw data (within rawsize)
    if rawsize > 0:
        seg = d[rawptr:rawptr+rawsize]
        nz = 0
        for b in reversed(seg):
            if b == 0: nz += 1
            else: break
        if nz > 16:
            print(f"    trailing zero-run in raw: {nz} bytes (VA 0x{vaddr+rawsize-nz:08x} .. 0x{vaddr+rawsize:08x})")