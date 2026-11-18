// perf_stack_sampler2.cs — stack sampler for a running x86 process, syscall-aware.
// For samples whose leaf is inside an ntdll syscall stub (Zw* wrappers), the return
// address to the real caller sits at [ESP+4]/[ESP+8] — we record those directly,
// plus a heuristic scan of the stack for code pointers into the main modules.
// Compiles with csc.exe (/platform:x86).
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

class StackSampler2 {

    [StructLayout(LayoutKind.Sequential)]
    struct FLOATING_SAVE_AREA {
        public uint ControlWord, StatusWord, TagWord, ErrorOffset, ErrorSelector, DataOffset, DataSelector;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 80)] public byte[] RegisterArea;
        public uint Cr0NpxState;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct CONTEXT {
        public uint ContextFlags;
        public uint Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
        public FLOATING_SAVE_AREA FloatSave;
        public uint SegGs, SegFs, SegEs, SegDs;
        public uint Edi, Esi, Ebx, Edx, Ecx, Eax;
        public uint Ebp, Eip, SegCs, EFlags, Esp, SegSs;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 512)] public byte[] ExtendedRegisters;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct STACKFRAME64 {
        public ulong AddrPC, AddrReturn, AddrFrame, AddrStack;
        public IntPtr FuncTableEntry;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)] public ulong[] Params;
        public int Far, Virtual;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)] public ulong[] Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct MODULEINFO { public IntPtr lpBaseOfDll; public uint SizeOfImage; public IntPtr EntryPoint; }

    [StructLayout(LayoutKind.Sequential)]
    struct THREADENTRY32 {
        public uint dwSize; public uint cntUsage; public uint th32ThreadID; public uint th32OwnerProcessID;
        public int tpBasePri; public int tpDeltaPri; public uint dwFlags;
    }

    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
    [DllImport("kernel32.dll")] static extern uint SuspendThread(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr h);
    [DllImport("kernel32.dll")] static extern bool GetThreadTimes(IntPtr h, out long c, out long e, out long k, out long u);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetThreadContext(IntPtr h, ref CONTEXT ctx);
    [DllImport("kernel32.dll")] static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint pid);
    [DllImport("kernel32.dll")] static extern bool Thread32First(IntPtr snap, ref THREADENTRY32 te);
    [DllImport("kernel32.dll")] static extern bool Thread32Next(IntPtr snap, ref THREADENTRY32 te);
    [DllImport("psapi.dll")] static extern bool EnumProcessModulesEx(IntPtr h, [Out] IntPtr[] lph, uint cb, out uint needed, uint filter);
    [DllImport("psapi.dll")] static extern bool GetModuleInformation(IntPtr h, IntPtr mod, out MODULEINFO mi, uint cb);
    [DllImport("psapi.dll")] static extern uint GetModuleFileNameEx(IntPtr h, IntPtr mod, StringBuilder name, uint size);
    [DllImport("dbghelp.dll")] static extern bool StackWalk64(uint machine, IntPtr hProcess, IntPtr hThread, ref STACKFRAME64 frame, ref CONTEXT ctx, IntPtr readMem, IntPtr funcTable, IntPtr getModuleBase, IntPtr translate);
    [DllImport("dbghelp.dll")] static extern bool SymInitialize(IntPtr hProcess, string userDir, bool invade);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool ReadProcessMemory(IntPtr h, IntPtr baseA, byte[] buf, int size, out int read);

    const uint THREAD_GET_CONTEXT = 0x0008;
    const uint THREAD_SUSPEND_RESUME = 0x0002;
    const uint PROCESS_QUERY_INFORMATION = 0x0400;
    const uint PROCESS_VM_READ = 0x0010;
    const uint TH32CS_SNAPTHREAD = 0x00000004;
    const uint CONTEXT_FULL_i386 = 0x00010007;
    const uint IMAGE_FILE_MACHINE_I386 = 0x0000014c;

    static IntPtr[] mods;
    static uint nMods;
    static IntPtr hProcess;
    static Dictionary<string, int> leafCounts = new Dictionary<string, int>();
    static Dictionary<string, int> syscallCallerCounts = new Dictionary<string, int>(); // leaf -> caller module+off (ESP+4/ESP+8)
    static Dictionary<string, int> stackScanCounts = new Dictionary<string, int>();   // module+off seen on stack
    static int samples = 0, syscallSamples = 0;

    static string MapAddr(ulong addr) {
        for (uint i = 0; i < nMods; i++) {
            MODULEINFO mi;
            if (GetModuleInformation(hProcess, mods[i], out mi, (uint)Marshal.SizeOf(typeof(MODULEINFO)))) {
                ulong baseA = (ulong)mi.lpBaseOfDll.ToInt64();
                if (addr >= baseA && addr < baseA + mi.SizeOfImage) {
                    var sb = new StringBuilder(512);
                    uint len = GetModuleFileNameEx(hProcess, mods[i], sb, 512);
                    string p = sb.ToString();
                    int sl = p.LastIndexOf('\\');
                    string name = sl >= 0 ? p.Substring(sl + 1) : p;
                    return name + "+0x" + (addr - baseA).ToString("x");
                }
            }
        }
        return null;
    }

    static void Bump(Dictionary<string, int> d, string k) {
        if (d.ContainsKey(k)) d[k]++; else d[k] = 1;
    }

    static void Main(string[] args) {
        uint pid = args.Length > 0 ? uint.Parse(args[0]) : 0;
        int seconds = args.Length > 1 ? int.Parse(args[1]) : 45;
        int intervalMs = args.Length > 2 ? int.Parse(args[2]) : 2;
        uint forcedTid = args.Length > 3 ? uint.Parse(args[3]) : 0;

        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
        if (hProcess == IntPtr.Zero) { Console.WriteLine("OpenProcess failed"); return; }

        IntPtr[] m = new IntPtr[2048];
        uint needed;
        if (!EnumProcessModulesEx(hProcess, m, (uint)(m.Length * IntPtr.Size), out needed, 0x03)) { Console.WriteLine("EnumProcessModulesEx failed"); return; }
        nMods = needed / (uint)IntPtr.Size;
        mods = new IntPtr[nMods];
        Array.Copy(m, mods, (int)nMods);

        SymInitialize(hProcess, null, false);

        uint tid = forcedTid;
        if (tid == 0) {
            // pick thread with max total cpu time
            long best = -1;
            IntPtr snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap != IntPtr.Zero && snap != new IntPtr(-1)) {
                var te = new THREADENTRY32(); te.dwSize = (uint)Marshal.SizeOf(typeof(THREADENTRY32));
                bool ok = Thread32First(snap, ref te);
                while (ok) {
                    if (te.th32OwnerProcessID == pid) {
                        IntPtr ht = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, false, te.th32ThreadID);
                        if (ht != IntPtr.Zero) {
                            long c, e, k, u;
                            if (GetThreadTimes(ht, out c, out e, out k, out u) && (k + u) > best) { best = k + u; tid = te.th32ThreadID; }
                            CloseHandle(ht);
                        }
                    }
                    te = new THREADENTRY32(); te.dwSize = (uint)Marshal.SizeOf(typeof(THREADENTRY32));
                    ok = Thread32Next(snap, ref te);
                }
                CloseHandle(snap);
            }
            if (tid == 0) { Console.WriteLine("no threads"); return; }
        }
        Console.WriteLine("sampler: pid=" + pid + " tid=" + tid + " seconds=" + seconds + " intervalMs=" + intervalMs);

        IntPtr hThread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, false, tid);
        if (hThread == IntPtr.Zero) { Console.WriteLine("OpenThread failed"); return; }

        var proc = System.Diagnostics.Process.GetProcessById((int)pid);
        var cpu1 = proc.TotalProcessorTime.TotalMilliseconds;

        DateTime end = DateTime.UtcNow.AddSeconds(seconds);
        while (DateTime.UtcNow < end) {
            SuspendThread(hThread);
            var ctx = new CONTEXT();
            ctx.ContextFlags = CONTEXT_FULL_i386;
            ctx.ExtendedRegisters = new byte[512];
            ctx.FloatSave.RegisterArea = new byte[80];
            if (GetThreadContext(hThread, ref ctx)) {
                string leaf = MapAddr(ctx.Eip);
                if (leaf != null) Bump(leafCounts, leaf);
                samples++;

                // syscall? leaf in ntdll stub region -> caller at stack
                if (leaf != null && leaf.StartsWith("ntdll.dll+0x73")) {
                    syscallSamples++;
                    byte[] buf = new byte[256];
                    int read;
                    if (ReadProcessMemory(hProcess, (IntPtr)ctx.Esp, buf, buf.Length, out read) && read >= 12) {
                        uint r0 = BitConverter.ToUInt32(buf, 0);   // ret into stub (after call)
                        uint r1 = BitConverter.ToUInt32(buf, 4);   // ret into caller of Zw wrapper
                        uint r2 = BitConverter.ToUInt32(buf, 8);   // maybe caller's caller
                        string m1 = MapAddr(r1);
                        string m2 = MapAddr(r2);
                        string key = leaf + " <- " + (m1 ?? ("0x" + r1.ToString("x"))) + " / " + (m2 ?? ("0x" + r2.ToString("x")));
                        Bump(syscallCallerCounts, key);
                        // also heuristic scan of the 256-byte window for code pointers
                        for (int i = 0; i + 4 <= read; i += 4) {
                            uint v = BitConverter.ToUInt32(buf, i);
                            string mm = MapAddr(v);
                            if (mm != null && !mm.StartsWith("0x")) Bump(stackScanCounts, mm);
                        }
                    }
                } else {
                    // non-syscall leaf: EBP-chain walk
                    var sf = new STACKFRAME64();
                    sf.AddrPC = ctx.Eip; sf.AddrFrame = ctx.Ebp; sf.AddrStack = ctx.Esp;
                    sf.Params = new ulong[4]; sf.Reserved = new ulong[3];
                    int depth = 0;
                    while (depth < 16) {
                        depth++;
                        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, hProcess, hThread, ref sf, ref ctx, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero)) break;
                    }
                }
            }
            ResumeThread(hThread);
            Thread.Sleep(intervalMs);
        }
        CloseHandle(hThread);

        proc.Refresh();
        var cpu2 = proc.TotalProcessorTime.TotalMilliseconds;
        CloseHandle(hProcess);

        Console.WriteLine("samples=" + samples + " syscallSamples=" + syscallSamples);
        Console.WriteLine("processCPU: start={0:N0}ms end={1:N0}ms delta={2:N0}ms ({3:P1} of one core over {4}s)",
            cpu1, cpu2, cpu2 - cpu1, (cpu2 - cpu1) / 1000.0 / seconds / 16.0, seconds);
        Console.WriteLine("--- leaf frames (top 30) ---");
        PrintTop(leafCounts, 30);
        Console.WriteLine("--- syscall leaf <- caller (ESP+4 / ESP+8) (top 30) ---");
        PrintTop(syscallCallerCounts, 30);
        Console.WriteLine("--- stack-scan module+offset (top 30) ---");
        PrintTop(stackScanCounts, 30);
    }

    static void PrintTop(Dictionary<string, int> d, int n) {
        var keys = new List<string>(d.Keys);
        keys.Sort((a, b) => d[b].CompareTo(d[a]));
        for (int i = 0; i < keys.Count && i < n; i++)
            Console.WriteLine(d[keys[i]].ToString().PadLeft(8) + "  " + keys[i]);
    }
}