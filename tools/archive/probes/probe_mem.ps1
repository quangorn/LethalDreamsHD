# Dump bytes at VA 0x478343 (patch1) and 0x4EBF12 (cave) from the running game.
Add-Type @"
using System;using System.Runtime.InteropServices;
public class M {
 [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out int read);
 [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
"@
$procs = Get-Process LETHALDREAMS -ErrorAction SilentlyContinue
if (-not $procs) { "game not running"; exit }
$p = $procs[0]
$h = [M]::OpenProcess(0x10, $false, $p.Id)   # PROCESS_VM_READ
if (-not $h) { "OpenProcess failed"; exit }
foreach ($pair in @( @(0x478343, 21), @(0x4EBF12, 26) )) {
  $addr = $pair[0]; $len = $pair[1]
  $buf = New-Object byte[] $len
  $read = 0
  $ok = [M]::ReadProcessMemory($h, [IntPtr]$addr, $buf, $len, [ref]$read)
  if (-not $ok) { ("0x{0:X} read failed" -f $addr) } else {
    ("0x{0:X} : {1}" -f $addr, (($buf | ForEach-Object { "{0:X2}" -f $_ }) -join " "))
  }
}
[void][M]::CloseHandle($h)
