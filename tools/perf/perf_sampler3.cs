// perf_sampler3.cs — per-second bucket profiler: main-thread leaf samples + process CPU per second.
// Writes CSV (t_ms, cpu_ms, leaf, caller) and prints per-second summary with top leaves.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

class Sampler3 {
    [StructLayout(LayoutKind.Sequential)] struct FLOATING_SAVE_AREA {
        public uint ControlWord, StatusWord, TagWord, ErrorOffset, ErrorSelector, DataOffset, DataSelector;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 80)] public byte[] RegisterArea;
        public uint Cr0NpxState;
    }
    [StructLayout(LayoutKind.Sequential)] struct CONTEXT {
        public uint ContextFlags;
        public uint Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
        public FLOATING_SAVE_AREA FloatSave;
        public uint SegGs, SegFs, SegEs, SegDs;
        public uint Edi, Esi, Ebx, Edx, Ecx, Eax;
        public uint Ebp, Eip, SegCs, EFlags, Esp, SegSs;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 512)] public byte[] ExtendedRegisters;
    }
    [StructLayout(LayoutKind.Sequential)] struct MODULEINFO { public IntPtr lpBaseOfDll; public uint SizeOfImage; public IntPtr EntryPoint; }
    [StructLayout(LayoutKind.Sequential)] struct FILETIME { public uint Low, High; }
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, uint pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint a, bool i, uint tid);
    [DllImport("kernel32.dll")] static extern uint SuspendThread(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetThreadContext(IntPtr h, ref CONTEXT ctx);
    [DllImport("psapi.dll")] static extern bool EnumProcessModulesEx(IntPtr h, [Out] IntPtr[] lph, uint cb, out uint needed, uint filter);
    [DllImport("psapi.dll")] static extern bool GetModuleInformation(IntPtr h, IntPtr mod, out MODULEINFO mi, uint cb);
    [DllImport("psapi.dll")] static extern uint GetModuleFileNameEx(IntPtr h, IntPtr mod, StringBuilder name, uint size);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool ReadProcessMemory(IntPtr h, IntPtr baseA, byte[] buf, int size, out int read);
    [DllImport("kernel32.dll")] static extern bool GetProcessTimes(IntPtr h, out FILETIME c, out FILETIME e, out FILETIME k, out FILETIME u);

    const uint THREAD_GET_CONTEXT = 0x0008;
    const uint THREAD_SUSPEND_RESUME = 0x0002;
    const uint PROCESS_QUERY_INFORMATION = 0x0400;
    const uint PROCESS_VM_READ = 0x0010;
    const uint CONTEXT_FULL_i386 = 0x00010007;

    static IntPtr[] mods; static uint nMods; static IntPtr hProcess;
    static string MapAddr(ulong addr) {
        for (uint i = 0; i < nMods; i++) {
            MODULEINFO mi;
            if (GetModuleInformation(hProcess, mods[i], out mi, (uint)Marshal.SizeOf(typeof(MODULEINFO)))) {
                ulong b = (ulong)mi.lpBaseOfDll.ToInt64();
                if (addr >= b && addr < b + mi.SizeOfImage) {
                    var sb = new StringBuilder(256);
                    uint len = GetModuleFileNameEx(hProcess, mods[i], sb, 256);
                    string p = sb.ToString();
                    int sl = p.LastIndexOf('\\');
                    string name = sl >= 0 ? p.Substring(sl + 1) : p;
                    return name + "+0x" + (addr - b).ToString("x");
                }
            }
        }
        return null;
    }
    static long FtToMs(FILETIME f) { return ((long)f.High << 32 | f.Low) / 10000L; }

    static void Main(string[] args) {
        uint pid = uint.Parse(args[0]);
        int seconds = args.Length > 1 ? int.Parse(args[1]) : 90;
        int intervalMs = args.Length > 2 ? int.Parse(args[2]) : 2;
        uint tid = args.Length > 3 ? uint.Parse(args[3]) : 0;
        string csvPath = args.Length > 4 ? args[4] : "perf_samples.csv";

        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
        if (hProcess == IntPtr.Zero) { Console.WriteLine("OpenProcess failed"); return; }
        IntPtr[] m = new IntPtr[2048]; uint needed;
        if (!EnumProcessModulesEx(hProcess, m, (uint)(m.Length * IntPtr.Size), out needed, 0x03)) { Console.WriteLine("EnumProcessModulesEx failed"); return; }
        nMods = needed / (uint)IntPtr.Size; mods = new IntPtr[nMods]; Array.Copy(m, mods, (int)nMods);

        if (tid == 0) { Console.WriteLine("usage: perf_sampler3.exe <pid> <seconds> <intervalMs> <tid> <csv>  (tid = busiest thread, see perf_tid.ps1)"); return; }
        IntPtr hThread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, false, tid);
        if (hThread == IntPtr.Zero) { Console.WriteLine("OpenThread failed tid=" + tid); return; }

        var sw = new StreamWriter(csvPath, false);
        sw.WriteLine("t_ms,cpu_ms,leaf,caller");

        var bucketLeaves = new Dictionary<int, Dictionary<string, int>>();
        var bucketSys = new Dictionary<int, int>();
        var bucketCpu = new Dictionary<int, long>();
        int prevSec = -1; long prevCpu = 0;
        FILETIME c0, e0, k0, u0;
        GetProcessTimes(hProcess, out c0, out e0, out k0, out u0);
        long prevCpuMs = FtToMs(k0) + FtToMs(u0);

        DateTime start = DateTime.UtcNow;
        DateTime end = start.AddSeconds(seconds);
        int sampleIdx = 0;
        while (DateTime.UtcNow < end) {
            int sec = (int)(DateTime.UtcNow - start).TotalSeconds;
            if (sec != prevSec) { prevSec = sec; }

            SuspendThread(hThread);
            var ctx = new CONTEXT();
            ctx.ContextFlags = CONTEXT_FULL_i386;
            ctx.ExtendedRegisters = new byte[512];
            ctx.FloatSave.RegisterArea = new byte[80];
            if (GetThreadContext(hThread, ref ctx)) {
                string leaf = MapAddr(ctx.Eip) ?? "unknown";
                string caller = "";
                if (leaf.StartsWith("ntdll.dll+0x73")) {
                    byte[] buf = new byte[16]; int read;
                    if (ReadProcessMemory(hProcess, (IntPtr)ctx.Esp, buf, buf.Length, out read) && read >= 8) {
                        uint r1 = BitConverter.ToUInt32(buf, 4);
                        caller = MapAddr(r1) ?? ("0x" + r1.ToString("x"));
                    }
                }
                GetProcessTimes(hProcess, out c0, out e0, out k0, out u0);
                long cpuMs = FtToMs(k0) + FtToMs(u0);
                sw.WriteLine((long)(DateTime.UtcNow - start).TotalMilliseconds + "," + cpuMs + "," + leaf + "," + caller);
                sampleIdx++;
                if (!bucketLeaves.ContainsKey(sec)) { bucketLeaves[sec] = new Dictionary<string, int>(); bucketSys[sec] = 0; }
                var bl = bucketLeaves[sec];
                if (bl.ContainsKey(leaf)) bl[leaf]++; else bl[leaf] = 1;
                if (caller != "") bucketSys[sec]++;
                sw.Flush();
            }
            ResumeThread(hThread);
            Thread.Sleep(intervalMs);
        }
        sw.Close();
        CloseHandle(hThread); CloseHandle(hProcess);

        // final cpu read for last bucket — recompute per-second cpu from csv would be better; here approximate
        Console.WriteLine("samples=" + sampleIdx + " csv=" + csvPath);
        Console.WriteLine("sec,cpu_ms_delta,syscalls,top_leaves");
        // cpu per bucket from the last sample cpu in bucket minus last sample cpu in prev bucket
        // simpler: read csv back
        var cpuBySec = new Dictionary<int, long>();
        string[] lines = File.ReadAllLines(csvPath);
        long lastCpu = 0; int lastSec = 0;
        foreach (var ln in lines) {
            if (ln.StartsWith("t_ms")) continue;
            var parts = ln.Split(',');
            if (parts.Length < 3) continue;
            lastSec = int.Parse(parts[0], CultureInfo.InvariantCulture) / 1000;
            long cpu = long.Parse(parts[1], CultureInfo.InvariantCulture);
            if (cpuBySec.ContainsKey(lastSec)) cpuBySec[lastSec] = cpu; else cpuBySec[lastSec] = cpu;
            lastCpu = cpu;
        }
        long prev = 0;
        for (int s = 0; s <= lastSec; s++) {
            long cur = cpuBySec.ContainsKey(s) ? cpuBySec[s] : prev;
            long delta = cur - prev; prev = cur;
            var bl = bucketLeaves.ContainsKey(s) ? bucketLeaves[s] : null;
            var sb = new StringBuilder();
            if (bl != null) {
                var keys = new List<string>(bl.Keys);
                keys.Sort((a, b) => bl[b].CompareTo(bl[a]));
                for (int i = 0; i < keys.Count && i < 5; i++) { if (i > 0) sb.Append(" | "); sb.Append(keys[i] + ":" + bl[keys[i]]); }
            }
            Console.WriteLine(s + "," + delta + "," + (bucketSys.ContainsKey(s) ? bucketSys[s] : 0) + "," + sb.ToString());
        }
    }
}