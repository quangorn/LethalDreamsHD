#!/bin/sh
# Собирает 32-битный blob пещеры из .s-исходника tools/exe/caves/<name>.s:
#   gcc -m32 -c -O1 -> <cache>/<name>.o ; objcopy -O binary -j .text -> <cache>/<name>.bin ;
#   обрезка по концу последнего символа данных (иначе .align раздела даёт лишние
#   nop-байты, а пещера должна влезть в zero-run: лимит зоны CAVE_VA = 0x4ebf12..0x4ec000
#   = 238 байт .text; exepatch12.py проверяет len(cave) <= 238),
#   затем печатает intel-дизасемблирование блоба.
# ВАЖНО: .s позиционно-независим, но две `fmul qword [c1024|c768]` содержат
# абсолютные disp32 -> их пересчитывает exepatch12.py при установке (CAVE_VA + offset).
# Использование: sh tools/exe/build_cave.sh fixall
set -e
if [ "$#" -ne 1 ]; then
    echo "использование: sh tools/exe/build_cave.sh <name>   # <name> = basename of tools/exe/caves/<name>.s"
    exit 2
fi
name=$1
DIR=$(dirname "$0")                      # tools/exe
ROOT=$(cd "$DIR/.." && pwd)             # корень репозитория
SRC="$DIR/caves/$name.s"
if [ ! -f "$SRC" ]; then
    echo "нет исходника: $SRC (ожидается tools/exe/caves/<name>.s)"
    exit 2
fi
# каталог кэша спрашиваем у конфигурации проекта (LD_CONFIG может его перенаправить)
CACHE=$(python3 -c "import sys; sys.path.insert(0,'$ROOT'); import paths; print(paths.cache_dir('cave'))")
gcc -m32 -c -O1 -o "$CACHE/$name.o" "$SRC"
objcopy -O binary -j .text "$CACHE/$name.o" "$CACHE/$name.bin"
# обрезка padding-а секции: конец = адрес + размер последнего символа; у локальных
# меток (.float/.double) размера в nm нет, тогда конец = последний не-nop (0x90) байт.
python3 - "$CACHE/$name.bin" "$CACHE/$name.o" <<'PY'
import subprocess, sys

p_bin, p_o = sys.argv[1], sys.argv[2]
out = subprocess.check_output(["nm", "--print-size", "-n", p_o]).decode("utf-8", "replace")
rows = [l.split() for l in out.splitlines() if l.strip()]
rows = [r for r in rows if not r[-1].startswith(".")]  # символы, не секции (.text/.data/.bss)
last = rows[-1]                                        # nm -n сортирует по адресу
d = open(p_bin, "rb").read()
if len(last) == 4:                                     # адрес + размер известны
    end = int(last[0], 16) + int(last[1], 16)
else:                                                  # размера нет -> конец данных
    end = len(d)
    while end > 0 and d[end - 1] == 0x90:
        end -= 1
open(p_bin, "wb").write(d[:end])
print("blob: %d bytes (was %d, trimmed %d)" % (end, len(d), len(d) - end))
print("last symbol:", " ".join(last))
PY
objdump -D -b binary -mi386 -M intel "$CACHE/$name.bin" | tail -n +8
