# fontmask_export.py - выгрузить маски покрытия (альфа) листов шрифтов в 8-бит grayscale PNG.
#
# Зачем: в RGBA-апскейле Upscayl модель обрабатывает только RGB, а альфу ресемплит
# отдельным (не модельным) фильтром - силуэт глифов остаётся «усреднённым».
# Чтобы модель восстановила именно форму штриха, альфу прогоняем отдельным PNG.
#
# Белый = штрих (255), чёрный = пусто (0); значения 1..254 = сглаживание.
#   python3 .tmp/fontmask_export.py [-o OUTDIR]
import sys, os, zlib, struct, binascii, argparse

# Архивное семейство: unpack_btf лежит в tools/btf репозитория (было: scratch-каталог).
_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as u

BAK = paths.btf_backup_dir()
FONTS = [('ld_uncial20', 512, 256), ('ld_uncial12', 256, 256),
         ('ld_tahoma11', 256, 256), ('ld_numbers15', 128, 32)]


def png_write_gray(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)

    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', binascii.crc32(c) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)   # color type 0 = grayscale
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', default=paths.assets_dir('LethalDreams_fonts_alpha_src'))
    a = ap.parse_args()
    os.makedirs(a.o, exist_ok=True)
    for nm, W, H in FONTS:
        blob = u.parse_btf(open('%s/%s_orig.btf' % (BAK, nm), 'rb').read())['blob']
        assert len(blob) == W * H * 4, (nm, len(blob))
        rows = []
        for y in range(H):
            rows.append(blob[y * W * 4 + 3:(y + 1) * W * 4:4])
        png_write_gray(os.path.join(a.o, nm + '_mask.png'), W, H, rows)
        inks = sum(sum(1 for v in r if v) for r in rows)
        print('%-14s %dx%d -> %s_mask.png  ink=%d' % (nm, W, H, nm, inks))
    open(os.path.join(a.o, 'README.txt'), 'w', encoding='utf-8').write(README)
    print('->', a.o)


README = '''Маски покрытия листов шрифтов Lethal Dreams (для апскейла альфы)
===============================================================

Каждый файл - 8-битный grayscale PNG, размер = родной лист:
  белый  = штрих (покрытие 255)
  чёрный = пусто (0)
  1..254 = сглаживание (это и есть тонкий край глифа)

Сетка ячеек та же, что в папке LethalDreams_fonts_src (см. _grid_*.png там же):
  ld_uncial20_mask.png   512x256  pitch 38
  ld_uncial12_mask.png   256x256  pitch 25
  ld_tahoma11_mask.png   256x256  pitch 19
  ld_numbers15_mask.png  128x32   pitch 16

Что нужно от апскейла (4x)
-------------------------
  1. Строго x4 по обеим оси: 2048x1024 / 1024x1024 / 1024x1024 / 512x128.
  2. Сетка ячеек ровно x4, ничего не сдвигать и не кропать.
  3. Формат PNG. Можно RGBA/RGB - я возьму один канал; важно, чтобы НЕ появилось
     «прозрачности» вместо белого и не было заливки фона в серый.
  4. Ничего не инвертировать: белое остаётся штрихом.

Модель: remacri (как и для RGB) - основной вариант. Если не лень, сделай заодно
high-fidelity: он обучен на «верность деталям», на бинарных масках часто чище
(у него хуже только цвет, а в маске цвета нет).
'''


main()
