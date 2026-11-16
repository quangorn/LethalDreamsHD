#!/usr/bin/env python3
"""Итерация 1: зонды значений на chokepoint'ах.

К-clean build/<module>.py (pycbuild.build_py, fixlits+fixints) добавляются
зонды записи в hdlog.txt в корень игры (paths.install_dir()), файл компилируется
2.2.3 и устанавливается в игру ТОЛЬКО для ui/ui, ui/cursor, gamescript.
Остальные .pyc в игре остаются оригинальными.

Зонды (только чтение, поведение не меняют):
  ui: INITBB (InitBillboard->BillBoard.Set), FONT/LEAD/ROWS/OFF (текст-параметры
      движку), SETPOS (BActionElement.SetPos->Set), SET2 (AutoPosition/SetPrior->Set),
      CHECK (CheckRefInPoint);
  cursor: MM/LBD/LBUP - сырые mx,my от движка в глобальных обработчиках;
  gamescript: SCREEN = gamemodule.GetScreenSize() после SetDisplayMode.

Команды (из корня репозитория):
  python3 tools/pyc/probe1.py            # собрать зондовые .py/.pyc, игру НЕ трогает
  python3 tools/pyc/probe1.py --install  # установить зонды в игру
"""
import os
import shutil
import sys

# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths
import pycbuild
import hdpatch

# каталог изменённых скриптов (в корне репозитория); build/ остаётся чистой нормализованной tree
PDIR = os.path.join(pycbuild.REPO_ROOT, 'patches')
# лог зондов - строка пути в корень игры, из install_dir (pycbuild.HDLOG)
HDLOG = pycbuild.HDLOG

UI_HELPER = (
    "_HDC = [0]\n"
    "def _hp(s):\n"
    "    _HDC[0] = _HDC[0] + 1\n"
    "    if _HDC[0] > 350:\n"
    "        return\n"
    "    f = open('%s', 'a')\n" % HDLOG +
    "    f.write(str(s) + chr(10))\n"
    "    f.close()\n"
)

CUR_HELPER = (
    "_HMC = [0]\n"
    "def hm(s):\n"
    "    _HMC[0] = _HMC[0] + 1\n"
    "    if _HMC[0] > 60:\n"
    "        return\n"
    "    f = open('%s', 'a')\n" % HDLOG +
    "    f.write(str(s) + chr(10))\n"
    "    f.close()\n"
)

GS_HELPER = (
    "_HSC = [0]\n"
    "def hp1(s):\n"
    "    _HSC[0] = _HSC[0] + 1\n"
    "    if _HSC[0] > 20:\n"
    "        return\n"
    "    f = open('%s', 'a')\n" % HDLOG +
    "    f.write(str(s) + chr(10))\n"
    "    f.close()\n"
)

# (target_line, probe_line, expected_count, where: 'after'|'before')
UI_EDITS = [
    ("import gamemodule, globalscript, copy, ui_macros", UI_HELPER, 1, 'after'),
    ("        BillBoard.Set(Parameters['x'], Parameters['y'], Parameters['prior'], Parameters['w'], Parameters['h'])",
     "        _hp('INITBB ' + repr((BillBoardName, Parameters['x'], Parameters['y'], Parameters['prior'], Parameters['w'], Parameters['h'])))", 1, 'after'),
    ("            BillBoard.SetFont(BillBoardFont, Parameters['TextParams']['Size'])",
     "            _hp('FONT ' + repr(Parameters['TextParams']['Size']))", 1, 'after'),
    ("            BillBoard.SetTextLeading(Parameters['TextParams']['Leading'])",
     "            _hp('LEAD ' + repr(Parameters['TextParams']['Leading']))", 1, 'after'),
    ("            BillBoard.SetTextRowSpacing(Parameters['TextParams']['RowS'])",
     "            _hp('ROWS ' + repr(Parameters['TextParams']['RowS']))", 1, 'after'),
    ("                BillBoard.SetTextOffset(Parameters['TextParams']['TextDX'], Parameters['TextParams']['TextDY'])",
     "                _hp('OFF ' + repr((Parameters['TextParams']['TextDX'], Parameters['TextParams']['TextDY'])))", 1, 'after'),
    ("        self.__dict__[BillBoardName].Set(x, y, param['prior'], w, h)",
     "        _hp('SETPOS ' + repr((BillBoardName, x, y, param['prior'], w, h)))", 1, 'before'),
    ("        self.__dict__[BillBoardName].Set(param['x'], param['y'], param['prior'], param['w'], param['h'])",
     "        _hp('SET2 ' + repr((BillBoardName, param['x'], param['y'], param['prior'], param['w'], param['h'])))", 2, 'before'),
    ("            return self.__dict__[BillBoardName].CheckRefInPoint(x, y)",
     "            _hp('CHECK ' + repr((BillBoardName, x, y)))", 1, 'before'),
]

CUR_EDITS = [
    ("import ui, globalscript, gamemodule, copy", CUR_HELPER, 1, 'after'),
    ("    def h_MouseMove(self, mx, my, flags):", "        hm('MM ' + repr((mx, my)))", 1, 'after'),
    ("    def h_MouseLBDown(self, mx, my, flags):", "        hm('LBD ' + repr((mx, my)))", 1, 'after'),
    ("    def h_MouseLBUp(self, mx, my, flags):", "        hm('LBUP ' + repr((mx, my)))", 1, 'after'),
]

GS_EDITS = [
    ("import globalscript, gamemodule, overlandcharacters, olmenu, vkeys, enmenu", GS_HELPER, 1, 'after'),
    ("    gamemodule.SetDisplayMode(w, h, bpp // 8, f)",
     "    hp1('SCREEN ' + repr(gamemodule.GetScreenSize()))", 1, 'after'),
]


def apply_edits(text, edits):
    lines = text.split('\n')
    for target, probe, count, where in edits:
        n = 0
        out = []
        for l in lines:
            if where == 'before' and l == target:
                n += 1
                out.append(probe)
            out.append(l)
            if where == 'after' and l == target:
                n += 1
                out.append(probe)
        if n != count:
            raise Exception('EDIT count=%d want %d for %r' % (n, count, target[:60]))
        lines = out
    return '\n'.join(lines)


def build_probe(module, edits, min_patch=False):
    """Чистый нормализованный .py из snapshot (build/ не меняется в содержательном
    смысле: build_py генерирует его заново), правки пишутся в patches/<module>.py,
    компиляция 2.2.3 -> patches/<module>.pyc."""
    py = pycbuild.build_py(module, fixlits=True, fixints=True)
    text = open(py, encoding='utf-8').read()
    if min_patch:
        if text.count(pycbuild.MIN_OLD) != 1:
            raise Exception('MIN_OLD not found exactly once in gamescript')
        text = text.replace(pycbuild.MIN_OLD, pycbuild.MIN_NEW)
    text = apply_edits(text, edits)
    dst = os.path.join(PDIR, module.replace('/', os.sep) + '.py')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'w', encoding='utf-8', newline='\n').write(text)
    out, rc = pycbuild.run_worker('compile', dst.replace(os.sep, '/'))
    if 'COMPILE-OK' not in out:
        raise Exception('COMPILE FAIL %s: %s' % (module, out.strip()[:300]))
    pyc = os.path.join(PDIR, module.replace('/', os.sep) + '.pyc')
    out, rc = pycbuild.run_worker('build', dst.replace(os.sep, '/'), pyc.replace(os.sep, '/'))
    if 'BUILD-OK' not in out:
        raise Exception('BUILD FAIL %s: %s' % (module, out.strip()[:300]))
    return pyc


def install(module, pyc, to_game=True):
    out = os.path.join(pycbuild.GAME_SCRIPTS, module.replace('/', os.sep))
    if not to_game:
        print('built', pyc, '; установка не тронута (нужен --install); цель:', out + '.pyc')
        return
    os.makedirs(os.path.dirname(out), exist_ok=True)
    shutil.copyfile(pyc, out + '.pyc')
    os.utime(out + '.pyc', None)
    if os.path.exists(out + '.py'):
        os.remove(out + '.py')
    print('installed', out + '.pyc')


if __name__ == '__main__':
    to_game = '--install' in sys.argv[1:]
    for m, pyc in (('ui/ui', build_probe('ui/ui', UI_EDITS)),
                   ('ui/cursor', build_probe('ui/cursor', CUR_EDITS)),
                   ('gamescript', build_probe('gamescript', GS_EDITS, min_patch=True))):
        install(m, pyc, to_game)
    print('итерация 1: зонды %s; остальные - оригиналы' %
          ('установлены (ui.pyc, cursor.pyc, gamescript.pyc)' if to_game else 'собраны (запись не выполнена)'))
