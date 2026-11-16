# restore_fonts.py - вернуть в игру РОДНЫЕ шрифты (лист + таблица парой) и убрать сайдкары.
#   python3 tools/fonts/restore_fonts.py            -> проверка: шаблоны, план; игра не трогается
#   python3 tools/fonts/restore_fonts.py --install  -> load-имена = родные, *_169/*_43 удалены
# Запись в установку — только с явным --install (правило репозитория).
# База: cache/btf_backup/*_orig.btf|.fdf (сняты с CAB-оригиналов; восстанавливаются:
#       python3 tools/extract_pristine.py templates).
import os, shutil, hashlib, sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

BAK = paths.btf_backup_dir()
FONTS = ['ld_uncial12', 'ld_uncial20', 'ld_tahoma11', 'ld_numbers15']
TEMPLATES_CMD = 'python3 tools/extract_pristine.py templates'


def md5(p):
    return hashlib.md5(open(p, 'rb').read()).hexdigest()


def main():
    install = "--install" in sys.argv[1:]
    for n in FONTS:
        for ext, sub in (('.btf', 'textures'), ('.fdf', 'fonts')):
            src = os.path.join(BAK, n + '_orig' + ext)
            if not os.path.isfile(src):
                sys.exit("missing template %s\nrebuild the cache: %s" % (src, TEMPLATES_CMD))
            dst = paths.game_path('data', sub) + '/' + n + ext
            sides = [paths.game_path('data', sub) + '/' + n + sfx + ext for sfx in ('_169', '_43')]
            if install:
                shutil.copyfile(src, dst)
                for side in sides:
                    if os.path.exists(side):
                        os.remove(side)
                print('%-13s %s %s %s' % (n, ext, md5(dst)[:12], 'INSTALLED'))
            else:
                present = [os.path.basename(s) for s in sides if os.path.exists(s)]
                print('%-13s %s %s CHECK: %s%s' % (
                    n, ext, md5(src)[:12], dst,
                    (' (удалить %s)' % ', '.join(present)) if present else ''))


if __name__ == '__main__':
    main()
