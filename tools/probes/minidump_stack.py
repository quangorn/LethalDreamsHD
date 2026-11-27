#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""minidump_stack.py — разбор WER-дампа LETHALDREAMS.EXE без отладчика (stdlib).

Зачем: краш вне загруженных модулей (`unknown` / `ntdll+...`) не даёт адреса
вызова, а отладочных средств в окружении нет. WER LocalDumps пишет полный
дамп (HKCU\\...\\Windows Error Reporting\\LocalDumps\\LETHALDREAMS.EXE,
DumpType 2); здесь дамп читается напрямую: модули, все потоки с контекстами,
исключение и **скан стека** — каждое слово в пределах стека раскрывается в
`модуль+смещение`, по возрастанию адреса это и есть цепочка возвратов.

  python3 tools/probes/minidump_stack.py <dump.dmp> [-v] [--from-module NAME]

Формат: MINIDUMP_HEADER, каталог потоков (ModuleList 4, ThreadList 3,
Exception 6, MemoryList 5, Memory64List 9). Все структуры — 32-битный дамп
(i386-контекст = 716 Б). Read-only, ничего не пишет.
"""
import struct
import sys

MDMP = b"MDMP"
STREAM_THREAD_LIST = 3
STREAM_MODULE_LIST = 4
STREAM_MEMORY_LIST = 5
STREAM_EXCEPTION = 6
STREAM_SYSTEM_INFO = 7
STREAM_MEMORY64_LIST = 9

# Архитектуры MINIDUMP_SYSTEM_INFO.ProcessorArchitecture.
ARCH_X86 = 0
ARCH_AMD64 = 9

# Смещения регистров в CONTEXT по архитектуре (x86: 716 B, x64: 1232 B).
REGS_X86 = {"Edi": 0x9c, "Esi": 0xa0, "Ebx": 0xa4, "Edx": 0xa8, "Ecx": 0xac,
            "Eax": 0xb0, "Ebp": 0xb4, "Eip": 0xb8, "Esp": 0xc4}
REGS_X64 = {"Rax": 0x78, "Rcx": 0x80, "Rdx": 0x88, "Rbx": 0x90, "Rsp": 0x98,
            "Rbp": 0xa0, "Rsi": 0xa8, "Rdi": 0xb0, "R8": 0xb8, "R9": 0xc0,
            "R10": 0xc8, "R11": 0xd0, "R12": 0xd8, "R13": 0xe0, "R14": 0xe8,
            "R15": 0xf0, "Rip": 0xf8}

# Виноватые имена кодов исключений, которые нас интересуют.
CODES = {0xC0000005: "ACCESS_VIOLATION", 0xC00000FD: "STACK_OVERFLOW",
         0xC0000409: "STACK_BUFFER_OVERRUN", 0xC000041D: "FATAL_USER_CALLBACK"}


class Dump(object):
    def __init__(self, data):
        self.d = data
        if data[:4] != MDMP:
            raise SystemExit("это не minidump (нет 'MDMP')")
        self.streams = {}
        n, rva = struct.unpack_from("<II", data, 8)
        for i in range(n):
            t, size, r = struct.unpack_from("<III", data, rva + i * 12)
            self.streams.setdefault(t, []).append((size, r))
        self.arch = ARCH_X86
        for _size, r in self.streams.get(STREAM_SYSTEM_INFO, []):
            self.arch = struct.unpack_from("<H", data, r)[0]
            break
        self.regs_offsets = REGS_X64 if self.arch == ARCH_AMD64 else REGS_X86
        self.word = 8 if self.arch == ARCH_AMD64 else 4
        self.pc = "Rip" if self.arch == ARCH_AMD64 else "Eip"
        self.sp = "Rsp" if self.arch == ARCH_AMD64 else "Esp"
        self.bp = "Rbp" if self.arch == ARCH_AMD64 else "Ebp"
        self.modules = self._modules()
        self.mem = self._memory()

    # --- служебное -----------------------------------------------------
    def u32(self, off):
        return struct.unpack_from("<I", self.d, off)[0]

    def u64(self, off):
        return struct.unpack_from("<Q", self.d, off)[0]

    def mstr(self, rva):
        ln = self.u32(rva)
        return self.d[rva + 4:rva + 4 + ln].decode("utf-16-le", "replace")

    def _modules(self):
        out = []
        for _size, rva in self.streams.get(STREAM_MODULE_LIST, []):
            n = self.u32(rva)
            for i in range(n):
                m = rva + 4 + i * 108
                base = self.u64(m)
                size = self.u32(m + 8)
                name = self.mstr(self.u32(m + 20)) if self.u32(m + 20) else "?"
                out.append({"base": base, "size": size,
                            "name": name.split("\\")[-1], "path": name})
        return out

    def _memory(self):
        """[(start, bytes)] из MemoryList и Memory64List."""
        out = []
        for _size, rva in self.streams.get(STREAM_MEMORY_LIST, []):
            n = self.u32(rva)
            for i in range(n):
                m = rva + 4 + i * 16
                start = self.u64(m)
                dsize, drva = struct.unpack_from("<II", self.d, m + 8)
                out.append((start, self.d[drva:drva + dsize]))
        for _size, rva in self.streams.get(STREAM_MEMORY64_LIST, []):
            n = self.u64(rva)
            base = self.u64(rva + 8)          # RVA первого блока данных
            off = rva + 16
            cur = base
            for i in range(n):
                start, dsize = struct.unpack_from("<QQ", self.d, off + i * 16)
                out.append((start, self.d[cur:cur + dsize]))
                cur += dsize
        return out

    # --- потоки / исключение ------------------------------------------
    def threads(self):
        out = []
        for _size, rva in self.streams.get(STREAM_THREAD_LIST, []):
            n = self.u32(rva)
            for i in range(n):
                t = rva + 4 + i * 48
                tid = self.u32(t)
                teb = self.u64(t + 16)
                s_start, s_size, s_rva = struct.unpack_from("<QII", self.d, t + 24)
                c_size, c_rva = struct.unpack_from("<II", self.d, t + 40)
                ctx = self.d[c_rva:c_rva + c_size] if c_size >= 0xcc else None
                out.append({"tid": tid, "teb": teb, "stack": (s_start, s_size, s_rva),
                            "ctx": ctx})
        return out

    def pick_layout(self, ctx, addr):
        """Раскладка CONTEXT подбирается по PC: берём ту, где PC == адресу
        исключения (SystemInfo.ProcessorArchitecture врёт в смешанных дампах)."""
        for offs in (REGS_X64, REGS_X86):
            k = "Rip" if offs is REGS_X64 else "Eip"
            o = offs[k]
            fmt = "<Q" if offs is REGS_X64 else "<I"   # ширина поля PC
            if o + struct.calcsize(fmt) <= len(ctx):
                if struct.unpack_from(fmt, ctx, o)[0] == addr:
                    return offs
        return self.regs_offsets

    def exception(self):
        for _size, rva in self.streams.get(STREAM_EXCEPTION, []):
            tid = self.u32(rva)
            er = rva + 8
            code = self.u32(er)
            addr = self.u64(er + 16)
            nparam = self.u32(er + 24)
            params = [self.u64(er + 32 + 8 * i) for i in range(min(nparam, 15))]
            c_size, c_rva = struct.unpack_from("<II", self.d, er + 152)
            ctx = self.d[c_rva:c_rva + c_size]
            return {"tid": tid, "code": code, "addr": addr, "params": params,
                    "ctx": ctx}
        return None

    # --- разрешение адреса --------------------------------------------
    def resolve(self, va):
        for m in self.modules:
            if m["base"] <= va < m["base"] + m["size"]:
                return "%s+0x%x" % (m["name"], va - m["base"])
        return None

    def stack_bytes(self, start, end):
        """Байты диапазона [start,end) из MemoryList/Memory64List."""
        for mstart, blob in self.mem:
            if mstart <= start and end <= mstart + len(blob):
                return blob[start - mstart:end - mstart]
        return b""


def regs(ctx, offsets=None):
    if ctx is None:
        return {}
    offsets = offsets or REGS_X86
    wide = offsets is REGS_X64      # по таблице, а не по смещению: Rip = 0xf8
    fmt, w = ("<Q", 8) if wide else ("<I", 4)
    out = {}
    for k, o in offsets.items():
        if o + w <= len(ctx):
            out[k] = struct.unpack_from(fmt, ctx, o)[0]
    return out


def scan(dump, esp, limit=0x2000, verbose=False):
    """Слова стека [esp, esp+limit) внутри модулей — кандидаты в возвраты."""
    blob = dump.stack_bytes(esp, esp + limit)
    if not blob:
        return [], 0
    hits = []
    step, fmt = dump.word, "<Q" if dump.word == 8 else "<I"
    for i in range(0, len(blob) - step + 1, step):
        va = struct.unpack_from(fmt, blob, i)[0]
        r = dump.resolve(va)
        if r:
            hits.append((esp + i, va, r))
    return hits, len(blob)


def main(argv):
    if not argv or argv[0].startswith("-"):
        raise SystemExit(__doc__.strip())
    verbose = "-v" in argv
    only = None
    if "--from-module" in argv:
        only = argv[argv.index("--from-module") + 1]
    path = argv[0]
    with open(path, "rb") as fh:
        dump = Dump(fh.read())

    print("dump: %s" % path)
    print("modules:")
    for m in sorted(dump.modules, key=lambda x: x["base"]):
        print("  %08x %08x %s" % (m["base"], m["size"], m["name"]))

    exc = dump.exception()
    if exc:
        name = CODES.get(exc["code"] & 0xFFFFFFFF, "")
        print("\nexception: code=0x%08x %s addr=0x%016x params=%s tid=%s"
              % (exc["code"], name, exc["addr"],
                 ["0x%x" % p for p in exc["params"]], exc["tid"]))
        r = dump.resolve(exc["addr"])
        print("  faulting address resolves to: %s" % (r or "outside all modules"))
        lay = dump.pick_layout(exc["ctx"], exc["addr"]) if exc["ctx"] else None
        rr = regs(exc["ctx"], lay or dump.regs_offsets)
        print("  registers: " + " ".join("%s=%08x" % (k.upper(), v)
                                         for k, v in sorted(rr.items())))
        for k in ("Rbp", "Rip", "Rsp", "Ebp", "Eip", "Esp"):
            if rr.get(k):
                print("  %s -> %s" % (k.upper(), dump.resolve(rr[k]) or "?"))
        if exc["ctx"] is not None:
            flags = struct.unpack_from("<I", exc["ctx"], 0)[0]
            print("  context flags=0x%08x" % flags)

    print("\nthreads (%d):" % len(dump.threads()))
    for t in dump.threads():
        rr = regs(t["ctx"], dump.regs_offsets)
        cur = "  curr" if exc and t["tid"] == exc["tid"] else ""
        print("  tid=%s teb=%08x %s=%s %s=%08x%s"
              % (t["tid"], t["teb"], dump.pc, dump.resolve(rr.get(dump.pc, 0)) or "?",
                 dump.sp, rr.get(dump.sp, 0), cur))

    # стек аварийного потока: цепочка кандидатов-возвратов
    target = exc["ctx"] if exc else None
    if target is None:
        ts = dump.threads()
        target = ts[0]["ctx"] if ts else None
    if target is not None:
        rr = regs(target, dump.regs_offsets)
        esp = rr.get(dump.sp, 0)
        hits, got = scan(dump, esp, 0x4000, verbose)
        print("\nstack scan from esp=0x%08x (%d bytes available):" % (esp, got))
        prev = None
        for addr, va, r in hits:
            name = r.split("+")[0]
            if only and only.lower() not in name.lower():
                continue
            mark = "  "
            if prev is not None and va > prev:
                mark = "> "        # рост адреса — правдоподобный кадр
            print("%s[%08x] %08x %s" % (mark, addr, va, r))
            prev = va


if __name__ == "__main__":
    main(sys.argv[1:])
