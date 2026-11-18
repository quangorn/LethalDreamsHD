# tools/perf — профилирование боя (CPU-bound battle analysis)

Инструментарий сессии 18.11.2026: поиск «тяжёлых долгих вызовов» в бою
LethalDreams (движок 2003, OpenGL, встроенный Python 2.2.3). Только чтение:
ни один файл игры не меняется; эксперименты с записью — на scratch-деревьях
(правило AGENTS.md). Игра после прогонов гасится.

## Состав

- `perf_activate.ps1` — активировать окно игры (AttachThreadInput) + клавиша.
- `perf_sendkey.ps1` — то же проще; авто-детект процесса.
- `perf_cpuload.ps1` — CPU процесса за 3 с.
- `perf_tid.ps1` — busiest thread (TID главного потока).
- `perf_shot.ps1` — снимок только окна игры (DPI-aware).
- `perf_frida_hook.py` — драйвер frida: `py -3.12 perf_frida_hook.py <pid> <сек> <js> <out.jsonl>`.
- `perf_frida3.js` — throttle-эксперимент (skip каждого 2-го `FUN_0044bdd0`).
- `perf_frida5.js` — длина списка `this+0x6c` ↔ длительность `FUN_0045eee0`.
- `perf_frida6.js` — callees `FUN_0045e0a0`.
- `perf_frida7.js` — итерации цикла внутри `FUN_0045e0a0`.
- `perf_frida8.js` — валидация cap-фикса (32 вызова `FUN_0045ce80` → NULL).
- `perf_sampler3.cs` — посекундные листья главного потока + CPU.
- `perf_stack_sampler2.cs` — syscall callers + stack-scan модулей.
- `perf_analyze2.py` — сводка лога `perf_frida3` (per-2s + totals).
- `pe_exports.py`, `pe_sections.py` — парсеры PE (экспорты, секции, zero-runs).

## Сборка C#-семплеров (x86! игра 32-битная)

```
C:/Windows/Microsoft.NET/Framework/v4.0.30319/csc.exe /nologo /platform:x86 /out:perf_sampler3.exe perf_sampler3.cs
C:/Windows/Microsoft.NET/Framework/v4.0.30319/csc.exe /nologo /platform:x86 /out:perf_stack_sampler2.exe perf_stack_sampler2.cs
```

## Типовой прогон

1. `perf_tid.ps1 -Pid <pid>` — TID главного потока.
2. `perf_sampler3.exe <pid> <сек> 2 <tid> <csv>` — листья; анализ `perf_analyze2.py`-подобным скриптом.
3. `perf_stack_sampler2.exe <pid> <сек> 2 <tid>` — syscall callers.
4. `py -3.12 perf_frida_hook.py <pid> <сек> perf_frida5.js <log>` — диагностика списка/длительностей.
5. `perf_activate.ps1 -Pid <pid>` — снять паузу (пробел); после прогона — поставить.

##环境 / пути

- PowerShell-скрипты берут процесс по имени (авто-детект PID); `-Pid` — явно.
- `$env:LD_GAME` / `$env:LD_SCRATCH` / `$env:LD_REPO` — как в `tools/probes/*`.
- frida — Windows-интерпретатор с пакетом frida (`py -3.12`); Cara: frida 17, ABI
  для thiscall передаётся 4-м позиционным аргументом-строкой (`'thiscall'`).
- Все оффсеты — module-relative (exe base 0x400000): `FUN_0044bdd0` = +0x4bdd0 и т.д.