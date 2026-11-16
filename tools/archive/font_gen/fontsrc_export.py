# fontsrc_export.py - выгрузить РОДНЫЕ листы шрифтов Lethal Dreams в PNG (+сетка ячеек).
#
# Источник: только бэкапы paths.btf_backup_dir()/ld_*_orig.btf|.fdf (никогда из игры -
# в игре могут лежать сжатые сайдкары).
#
#   python3 .tmp/fontsrc_export.py [-o OUTDIR]
import sys, os, zlib, struct, binascii, shutil, argparse

# Архивное семейство: unpack_btf лежит в tools/btf репозитория (было: scratch-каталог).
_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as u

BAK = paths.btf_backup_dir()
# имя, W, H (родные размеры листа), pitch
FONTS = [('ld_uncial20', 512, 256, 38),
         ('ld_uncial12', 256, 256, 25),
         ('ld_tahoma11', 256, 256, 19),
         ('ld_numbers15', 128, 32, 16)]


def png_write_rgba(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)

    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', binascii.crc32(c) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def parse_fdf(path):
    raw = open(path, 'rb').read().decode('latin-1')
    lines = raw.replace('\r\n', '\n').split('\n')
    hdr = lines[0].split()
    pitch = int(hdr[1])
    ents = []
    for ln in lines[1:]:
        ln = ln.strip()
        if not ln:
            continue
        t = ln.split()
        ents.append((t[0], int(t[1]), int(t[2]), int(t[3])))   # tok, x, y, adv
    return pitch, ents


def load_sheet(name):
    d = open('%s/%s_orig.btf' % (BAK, name), 'rb').read()
    # все четыре листа - 32-bit RGBA, первая запись dims
    blob = u.parse_btf(d)['blob']
    return blob


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', default=paths.assets_dir('LethalDreams_fonts_src'))
    a = ap.parse_args()
    out = a.o
    os.makedirs(out, exist_ok=True)

    report = []
    for name, W, H, pitch_hint in FONTS:
        blob = load_sheet(name)
        assert len(blob) == W * H * 4, (name, len(blob))
        rows = [bytearray(blob[y * W * 4:(y + 1) * W * 4]) for y in range(H)]
        png_write_rgba(os.path.join(out, name + '.png'), W, H, rows)

        pitch, ents = parse_fdf('%s/%s_orig.fdf' % (BAK, name))
        assert pitch == pitch_hint, (name, pitch, pitch_hint)
        # копия таблицы: позиции/адвансы в PNG-координатах (родные)
        shutil.copyfile('%s/%s_orig.fdf' % (BAK, name), os.path.join(out, name + '.fdf'))

        # overlay: рамка ячейки [x, x+adv) x [y, y+pitch), полосы шагом pitch
        ov = [bytearray(r) for r in rows]
        for yy in range(H):
            r = ov[yy]
            for xx in range(W):
                i = xx * 4
                if r[i + 3]:
                    # лёгкое затемнение ink, чтобы сетка читалась
                    r[i] = r[i] // 2 + 32
                    r[i + 1] = r[i + 1] // 2 + 32
                    r[i + 2] = r[i + 2] // 2 + 32
                    r[i + 3] = 255
        def px(xx, yy, rgb):
            if 0 <= xx < W and 0 <= yy < H:
                r = ov[yy]
                i = xx * 4
                r[i], r[i + 1], r[i + 2], r[i + 3] = rgb[0], rgb[1], rgb[2], 255
        band_y = sorted(set(y for _, _, y, _ in ents))
        for y in band_y:
            for xx in range(W):
                px(xx, y, (0, 160, 255))
            for _, x, yy, adv in ents:
                if yy != y:
                    continue
                for xx in range(x, min(x + adv, W)):
                    px(xx, y, (255, 60, 60))          # верх ячейки
                for yy2 in range(y, min(y + pitch, H)):
                    px(x, yy2, (255, 60, 60))         # левый край ячейки
                    px(min(x + adv, W - 1), yy2, (60, 255, 60))   # правый край ячейки
        png_write_rgba(os.path.join(out, '_grid_' + name + '.png'), W, H, ov)

        inks = sum(1 for r in rows for i in range(3, len(r), 4) if r[i])
        report.append((name, W, H, pitch, len(ents), inks))
        print('%-14s %dx%d pitch=%d glyphs=%d ink=%d' % (name, W, H, pitch, len(ents), inks))

    table = '\n'.join('  %-14s %4dx%-4d pitch=%-3d glyphs=%-4d ink=%d' % r for r in report)
    open(os.path.join(out, 'README.txt'), 'w', encoding='utf-8').write(README.replace('%TABLE%', table))
    print('->', out)


README = '''Листы шрифтов Lethal Dreams - ИСХОДНИКИ (родные, не масштабированные)
=======================================================================

Получены из бэкапов paths.btf_backup_dir()/ld_*_orig.btf (родные, до любых правок).
Внутри игры сейчас тоже лежат родные листы (restore_fonts.py).

Файлы
-----
  ld_uncial20.png   основной шрифт (заголовки, описания, диалоги)
  ld_uncial12.png   мелкий вариант того же унциала
  ld_tahoma11.png   тело текста в списках/настройках
  ld_numbers15.png  цифры (HUD/счётчики)
  *.fdf             родные таблицы: header "<name> <pitch> <sheet.tga>",
                    далее строки "<char> <x> <y> <adv>"
                    (x,y = левый верх ячейки в PNG, adv = ширина ячейки,
                     y кратно pitch = номер полосы)
  _grid_*.png       те же листы с нарисованной сеткой ячеек для контроля:
                    синий = граница полосы (y), красный = левый край ячейки,
                    зелёный = правый край (x+adv). Ink НЕ выходит за эти рамки.

Что нужно от апскейла (2x)
-------------------------
  1. Размер строго x2 по каждой оси, без кропа и без паддинга:
       ld_uncial20.png  512x256  -> 1024x512
       ld_uncial12.png  256x256  ->  512x512
       ld_tahoma11.png  256x256  ->  512x512
       ld_numbers15.png 128x32   ->  256x64
  2. Сетка должна остаться ровно в 2 раза крупнее: полосы y = 0, 2*pitch, 4*pitch...
     ячейка глифа = [2x, 2(x+adv)) x [2y, 2y+2*pitch). Иначе движок порежет глиф
     по UV-ячейке (кроп) или покажет соседний глиф.
  3. Альфа-канал обязателен и должен остаться АЛЬФОЙ (покрытием), не запекаться в RGB.
     У этих листов: RGB = цвет штриха (в основном белый/серый градиент),
     A = покрытие (0 = пусто, 255 = штрих, между = сглаживание).
     Если утилита умеет только RGB(A) «как есть» - ок, но проверить, что
     прозрачный фон остался прозрачным (A=0), а не стал чёрным.
  4. Формат PNG, 8 бит/канал, RGBA. Имена файлов - те же.

Ориентация: top-down, строка 0 = верх (как в PNG). Ничего не переворачивать.

Сводка по листам
----------------
%TABLE%
'''


if __name__ == '__main__':
    main()
