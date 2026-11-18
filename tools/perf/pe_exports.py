# pe_exports.py — minimal PE export table parser (x86/x64), no deps.
# Usage: python pe_exports.py <dll> <off1> [off2 ...]   (offsets in hex, e.g. 0x7343c)
import struct, sys

def read_exports(path):
    d = open(path, 'rb').read()
    e_lfanew = struct.unpack_from('<I', d, 0x3c)[0]
    assert d[e_lfanew:e_lfanew+4] == b'PE\x00\x00'
    coff = e_lfanew + 4
    machine, nsec, _, _, _, opt_sz, _ = struct.unpack_from('<HHIIIHH', d, coff)
    opt = coff + 20
    magic = struct.unpack_from('<H', d, opt)[0]
    pe32p = magic == 0x20b
    # data directories at end of optional header
    dd_off = opt + (opt_sz - 16 * 8 if False else (96 if pe32p == False else 112))
    # export dir is data directory index 0
    exp_rva, exp_sz = struct.unpack_from('<II', d, dd_off)
    if exp_rva == 0:
        return machine, pe32p, []
    sections = []
    sec_off = opt + opt_sz
    for i in range(nsec):
        name, vsize, vaddr, rawsize, rawptr = struct.unpack_from('<8sIIII', d, sec_off + i*40)
        sections.append((vaddr, vsize, rawptr, rawsize))
    def rva2off(rva):
        for vaddr, vsize, rawptr, rawsize in sections:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rawptr + (rva - vaddr)
        return None
    eo = rva2off(exp_rva)
    nfun, nname = struct.unpack_from('<II', d, eo + 20)
    addr_of_funcs = rva2off(struct.unpack_from('<I', d, eo + 28)[0])
    addr_of_names = rva2off(struct.unpack_from('<I', d, eo + 32)[0])
    addr_of_ords = rva2off(struct.unpack_from('<I', d, eo + 36)[0])
    funcs = []
    for i in range(nfun):
        funcs.append(struct.unpack_from('<I', d, addr_of_funcs + i*4)[0])
    names = []
    for i in range(nname):
        nrva = struct.unpack_from('<I', d, addr_of_names + i*4)[0]
        noff = rva2off(nrva)
        end = d.index(b'\x00', noff)
        nm = d[noff:end].decode('ascii', 'replace')
        ordi = struct.unpack_from('<H', d, addr_of_ords + i*2)[0]
        names.append((funcs[ordi] if ordi < len(funcs) else None, nm))
    return machine, pe32p, names

def main():
    path = sys.argv[1]
    machine, pe32p, names = read_exports(path)
    print('machine=0x%04x pe32+=%s exports=%d' % (machine, pe32p, len(names)))
    # build sorted list by rva
    lst = sorted(names)
    for a in sys.argv[2:]:
        off = int(a, 16)
        best = None
        for rva, nm in lst:
            if rva is not None and rva <= off:
                best = (rva, nm)
            else:
                break
        if best:
            print('0x%06x  ->  %s+0x%x  (%s)' % (off, best[1], off - best[0], 'inside'))
        else:
            print('0x%06x  ->  (no export below)' % off)

if __name__ == '__main__':
    main()