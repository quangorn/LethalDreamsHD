#!/usr/bin/env python3
"""
Генератор конфигурации разрешений для Lethal Dreams: The Circle Of Fate.

Механика: список разрешений игры живёт в Python-скрипте cfg_default.py
(дефолтный профиль, ExecConfig делает `import cfg_default`):
    R_RESOLUTION = [[w, h, default_flag], ...]
    R_BITPERPIXEL = [[bpp, ?, default_flag], ...]
    R_FULLSCREEN = 0/1
ui/bpref.py строит радиосписок настроек прямо из len(R_RESOLUTION),
а globalscript.UpdateConfig() при смене вызывает gamemodule.SetDisplayMode(w, h, bpp/8, fullscreen).
Значит достаточно положить cfg_default.py рядом с cfg_default.pyc: Python 2.2
видит .py новее .pyc и исполняет наш файл. Разрешения становятся конфигурируемыми.

Использование (из корня репозитория; файл лежит в tools/pyc/, декомпилят и
каталог cfg/ - в корне репозитория, игровые пути - из install_dir):
    python3 tools/pyc/make_cfg.py 3840 2160            # добавить 3840x2160 дефолтом, fullscreen=0
    python3 tools/pyc/make_cfg.py 1920 1080 --fs 1     # ... c полноэкранным режимом
    python3 tools/pyc/make_cfg.py --list              # показать, что получится
    python3 tools/pyc/make_cfg.py <w> <h> [--fs]      # сгенерировать в OUT_DIR (игру не трогает)
    python3 tools/pyc/make_cfg.py <w> <h> [--fs] --install  # записать cfg_default.py/globalscript.py
                                                      # в игровую установку (явный флаг)
"""
import os, re, shutil, sys

# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

REPO_ROOT = paths.win_path(_ROOT)
SRC = os.path.join(REPO_ROOT, 'decompiled_fixed', 'cfg', 'cfg_default.py')
GS_SRC = os.path.join(REPO_ROOT, 'decompiled_fixed', 'globalscript.py')
OUT_DIR = os.path.join(REPO_ROOT, 'cfg')
GAME_SCRIPTS = paths.game_path('data', 'scripts')
GAME_CFG = paths.game_path('data', 'scripts', 'cfg')
EXTRA = [(1920, 1080), (2560, 1440), (3840, 2160)]

def strip_header(text):
    text = re.sub(r'^# uncompyle6.*?\n# Compiled at:.*?\n', '', text, flags=re.S)
    # компилятор движка (стандартный парсер Python 2) не принимает return на уровне модуля:
    # декомпилятор его восстанавливает из RETURN_VALUE байткода
    return re.sub(r'^return\s*$\n', '', text, flags=re.M)

APPLY = """

import copy
import cfg_default

# захват списка до того, как UpdateConfig() заменит R_RESOLUTION движковскими претами
HD_RES = copy.deepcopy(cfg_default.R_RESOLUTION)
HD_BPP = copy.deepcopy(cfg_default.R_BITPERPIXEL)
HD_FS = cfg_default.R_FULLSCREEN
HD_APPLIED = 0

# масштаб UI: движок рисует канвас 1024x768 с коэффициентом s = w/1024.
# чтобы UI влезал по высоте, движку передаём координаты, умноженные на
# k = h*1024/(w*768): экранное масштабирование UI = k*s = h/768.
# HD_DX/HD_DY - сдвиг канваса в единицах канваса (полосы по краям).
HD_K = 1.0
HD_DX = 0.0
HD_DY = 0.0

def ApplyResolution():
    global HD_APPLIED, HD_K, HD_DX, HD_DY
    idx = 0
    i = 0
    for m in HD_RES:
        if m[2] == 1:
            idx = i
        i += 1
    w = HD_RES[idx][0]
    h = HD_RES[idx][1]
    b = 4
    j = 0
    for m in HD_BPP:
        if m[2] == 1:
            b = m[0] / 8
        j += 1
    HD_K = h * 1024.0 / (w * 768.0)
    HD_DX = (1024.0 - 1024.0 * HD_K) / 2.0
    # вертикаль: видимая высота канваса = h/s = h*1024/w = 768*k ровно,
    # поэтому сдвиг по Y = 0; полоса только по X (ширина 1024*k < 1024)
    HD_DY = 0.0
    try:
        gamemodule.SetDisplayMode(w, h, b, HD_FS)
        r = 'ok'
    except Exception, e:
        r = 'FAIL ' + str(e)
    HD_APPLIED += 1
    f = open('pydebug.log', 'a')
    f.write('ApplyResolution %d %d %d %d %s k=%.4f dx=%.2f (#%d)\\n' % (w, h, b, HD_FS, r, HD_K, HD_DX, HD_APPLIED))
    f.close()
    return

ApplyResolution()

import hdui
"""

def build_globalscript():
    text = strip_header(open(GS_SRC, encoding='utf-8').read())
    # ExecConfig() = import cfg_<UserName> + UpdateConfig(); навешиваем применённый режим
    text, n = re.subn(r'(?m)^def ExecConfig\(\):.*?UpdateConfig\(\)\n',
                      lambda m: m.group(0) + '    ApplyResolution()\n', text, flags=re.S)
    assert n == 1, 'ExecConfig patch failed'
    return text + APPLY

GAME_UI = GAME_SCRIPTS  # тот же каталог игровых скриптов, из install_dir

# Monkeypatch: оригинальные ui.pyc/cursor.pyc остаются, подменяем только методы
# Python-класса, которые говорят движку координаты. Декомпилированные ui.py/cursor.py
# целиком движок не принимает (проверено: серый экран), поэтому правки точечные.
HDUI_SRC = '''
import ui, globalscript, gamemodule, copy, ui_macros, cursor, bgui
dict = globalscript.GetDictionary()

HD_LOG = 0

def _log(s):
    f = open('pydebug.log', 'a')
    f.write(s + chr(10))
    f.close()

def _m(self):
    return (globalscript.HD_K, globalscript.HD_DX, globalscript.HD_DY)

# BActionElement.InitBillboard: позиции/размеры и текстовые метрики уходят
# в движок в k*c + d, координаты param-словаря остаются канвасными.
def InitBillboard(self, InitParams, ParametersName, BillBoardName, BillBoardMaterialName, BillBoardFontName):
    if len(InitParams) == 0:
        return
    k, dx, dy = _m(self)
    scene = gamemodule.GameManager_GetScene()
    Parameters = {'x': (InitParams[0]), 'y': (InitParams[1]), 'w': (InitParams[2]), 'h': (InitParams[3]), 'TextureName': (InitParams[5]), 'MaterialParam': (InitParams[6])}
    if len(InitParams) > 7:
        Parameters['TextParams'] = InitParams[7]
    else:
        Parameters['TextParams'] = []
    Parameters['prior'] = 0
    if len(InitParams) > 8:
        Parameters['Sound'] = InitParams[8]
    else:
        Parameters['Sound'] = ''
    self.__dict__[ParametersName] = Parameters
    Parameters['color'] = copy.deepcopy(InitParams[4])
    BillBoard = gamemodule.Billboard_CreateNew()
    self.__dict__[BillBoardName] = BillBoard
    BillBoard.CreateInnerData()
    sx = int(Parameters['x'] * k + dx)
    sy = int(Parameters['y'] * k + dy)
    sw = int(Parameters['w'] * k)
    sh = int(Parameters['h'] * k)
    global HD_LOG
    if HD_LOG < 15:
        HD_LOG += 1
        _log('InitBB ' + str(Parameters['x']) + ',' + str(Parameters['y']) + ' ' + str(Parameters['w']) + 'x' + str(Parameters['h']) + ' -> ' + str(sx) + ',' + str(sy) + ' ' + str(sw) + 'x' + str(sh))
    BillBoard.Set(sx, sy, Parameters['prior'], sw, sh)
    BillBoard.SetColor(Parameters['color'][0], Parameters['color'][1], Parameters['color'][2], Parameters['color'][3])
    if len(Parameters['TextureName']) != 0:
        tex = gamemodule.LoadTexture(Parameters['TextureName'])
        if tex:
            BillBoardMaterial = gamemodule.Material_CreateNew()
            BillBoardMaterial.CreateInnerData()
            BillBoardMaterial.GetPass(0).AddTexture(Parameters['TextureName'])
        else:
            tmp_mat = gamemodule.LoadMaterial(Parameters['TextureName'])
            BillBoardMaterial = tmp_mat.Duplicate()
            tmp_mat = None
        tex = None
        BillBoard.SetMaterial(BillBoardMaterial)
        BillBoardMaterial.GetPass(0).SetBlendFunc(InitParams[6])
    if len(Parameters['TextParams']) != 0:
        x = copy.deepcopy(Parameters['TextParams'])
        length = len(x)
        Parameters['TextParams'] = {}
        Parameters['TextParams']['TextString'] = ui_macros.RT_ParseText(x[0])
        Parameters['TextParams']['FontName'] = dict.FONTS_LIST['default']
        if length > 1:
            tmp_fntname = x[1]
            if dict.FONTS_LIST.has_key(tmp_fntname):
                Parameters['TextParams']['FontName'] = dict.FONTS_LIST[tmp_fntname]
            else:
                Parameters['TextParams']['FontName'] = dict.FONTS_LIST['default']
        Parameters['TextParams']['Size'] = 18
        if length > 2:
            Parameters['TextParams']['Size'] = x[2]
        Parameters['TextParams']['Alignment'] = 0
        if length > 3:
            Parameters['TextParams']['Alignment'] = x[3]
        tmp_fntparms = dict.FONT_PARAMS[Parameters['TextParams']['FontName']]
        Parameters['TextParams']['Leading'] = tmp_fntparms[0]
        if length > 4:
            Parameters['TextParams']['Leading'] += x[4]
        Parameters['TextParams']['RowS'] = tmp_fntparms[1]
        if length > 5:
            Parameters['TextParams']['RowS'] += x[5]
        offset = 0
        if length > 7:
            offset = 1
            Parameters['TextParams']['TextDX'] = x[6]
            Parameters['TextParams']['TextDY'] = x[7]
        BillBoardFont = gamemodule.LoadFont(Parameters['TextParams']['FontName'])
        self.__dict__[BillBoardFontName] = BillBoardFont
        BillBoard.SetFont(BillBoardFont, int(Parameters['TextParams']['Size'] * k))
        BillBoard.SetTextLeading(int(Parameters['TextParams']['Leading'] * k))
        BillBoard.SetText(Parameters['TextParams']['TextString'])
        BillBoard.SetTextAlignment(Parameters['TextParams']['Alignment'])
        BillBoard.SetTextRowSpacing(int(Parameters['TextParams']['RowS'] * k))
        BillBoard.SetTextColor(Parameters['color'][0], Parameters['color'][1], Parameters['color'][2], Parameters['color'][3])
        if offset:
            BillBoard.SetTextOffset(int(Parameters['TextParams']['TextDX'] * k), int(Parameters['TextParams']['TextDY'] * k))
    if len(Parameters['Sound']) > 0:
        Parameters['Sound'] = gamemodule.LoadSound(Parameters['Sound'])
    else:
        Parameters['Sound'] = 0
    scene.AddBillboard(BillBoard)
    return

def SetPos(self, BillBoardName, x, y, w=-1, h=-1):
    if not self.__dict__.has_key(BillBoardName) or not self.__dict__[BillBoardName]:
        return
    k, dx, dy = _m(self)
    param = self.__dict__[BillBoardName + 'Params']
    param['x'] = x
    param['y'] = y
    if w == -1:
        w = param['w']
    else:
        param['w'] = w
    if h == -1:
        h = param['h']
    else:
        param['h'] = h
    sx = int(x * k + dx)
    sy = int(y * k + dy)
    sw = int(w * k)
    sh = int(h * k)
    global HD_LOG
    if HD_LOG < 30:
        HD_LOG += 1
        _log('SetPos ' + str(BillBoardName) + ' ' + str(x) + ',' + str(y) + ' ' + str(w) + 'x' + str(h) + ' -> ' + str(sx) + ',' + str(sy) + ' ' + str(sw) + 'x' + str(sh))
    self.__dict__[BillBoardName].Set(sx, sy, param['prior'], sw, sh)
    if self.ToolTipNew:
        if BillBoardName == 'ToolTipBB':
            self.AutoPosition('ToolTipBB')
    return

def SetPrior(self, BillBoardName, prior):
    if not self.__dict__.has_key(BillBoardName) or not self.__dict__[BillBoardName]:
        return
    k, dx, dy = _m(self)
    param = self.__dict__[BillBoardName + 'Params']
    param['prior'] = prior
    self.__dict__[BillBoardName].Set(int(param['x'] * k + dx), int(param['y'] * k + dy), param['prior'], int(param['w'] * k), int(param['h'] * k))
    return

def AutoPosition(self, BillBoardName):
    if not self.__dict__.has_key(BillBoardName) or not self.__dict__[BillBoardName]:
        return
    k, dx, dy = _m(self)
    param = self.__dict__[BillBoardName + 'Params']
    if param['x'] + param['w'] > 1004:
        param['x'] = 1004 - param['w']
    if param['x'] < 20:
        param['x'] = 20
    if param['y'] + param['h'] > 748:
        param['y'] = 748 - param['h']
    if param['y'] < 20:
        param['y'] = 20
    self.__dict__[BillBoardName].Set(int(param['x'] * k + dx), int(param['y'] * k + dy), param['prior'], int(param['w'] * k), int(param['h'] * k))
    return

def SetTextOffset(self, BillBoardName, tdx, tdy):
    if not self.__dict__.has_key(BillBoardName) or not self.__dict__[BillBoardName]:
        return
    k, dx, dy = _m(self)
    self.__dict__[BillBoardName].SetTextOffset(int(tdx * k), int(tdy * k))
    return

ui.BActionElement.InitBillboard = InitBillboard
ui.BActionElement.SetPos = SetPos
ui.BActionElement.SetPrior = SetPrior
ui.BActionElement.AutoPosition = AutoPosition
ui.BActionElement.SetTextOffset = SetTextOffset

# мышиные координаты движка (канвас-пространство k*c + d) -> канвас-единицы UI
def _wrap(fn, wheel=0):
    def w(self, mx, my, flags, *rest):
        k = globalscript.HD_K
        dx = globalscript.HD_DX
        dy = globalscript.HD_DY
        mx = (mx - dx) / k
        my = (my - dy) / k
        if wheel:
            return fn(self, mx, my, flags, rest[0])
        return fn(self, mx, my, flags)
    return w

for name in ('h_MouseMove', 'h_MouseLBDown', 'h_MouseRBDown', 'h_MouseLBUp', 'h_MouseRBUp', 'h_MouseLDblClk'):
    fn = getattr(cursor.BCursor, name)
    setattr(cursor.BCursor, name, _wrap(fn))
fn = cursor.BCursor.h_MouseWheel
setattr(cursor.BCursor, 'h_MouseWheel', _wrap(fn, 1))
'''

def build(w, h, fullscreen=0):
    text = strip_header(open(SRC, encoding='utf-8').read())
    modes = [[640, 480, 0], [800, 600, 0], [1024, 768, 0], [1280, 960, 0]]
    for (mw, mh) in EXTRA:
        modes.append([mw, mh, 1 if (mw, mh) == (w, h) else 0])
    # пользовательское разрешение (если не входит в типовые)
    if (w, h) not in [(m[0], m[1]) for m in modes]:
        modes.append([w, h, 1])
    res_line = 'R_RESOLUTION = ' + repr(modes).replace('(', '[').replace(')', ']')
    text = re.sub(r'^R_RESOLUTION = .*$', res_line, text, flags=re.M)
    text = re.sub(r'^R_FULLSCREEN = .*$', 'R_FULLSCREEN = %d' % fullscreen, text, flags=re.M)
    return text

if __name__ == '__main__':
    args = sys.argv[1:]
    if not args:
        w, h, fs = 3840, 2160, 0
    elif args[0] == '--list':
        print(build(3840, 2160, 0)); sys.exit(0)
    else:
        w, h = int(args[0]), int(args[1])
        fs = 1 if '--fs' in args else 0
    text = build(w, h, fs)
    gs = build_globalscript()
    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.join(OUT_DIR, 'cfg_default.py')
    open(out, 'w', encoding='utf-8', newline='\r\n').write(text)
    gs_out = os.path.join(OUT_DIR, 'globalscript.py')
    open(gs_out, 'w', encoding='utf-8', newline='\r\n').write(gs)
    print('generated', out, 'and', gs_out)
    if '--install' in args:
        dst = os.path.join(GAME_CFG, 'cfg_default.py')
        shutil.copyfile(out, dst)
        os.utime(dst, None)  # свежая mtime -> Python 2.2 перекомпилирует .py
        dst2 = os.path.join(GAME_SCRIPTS, 'globalscript.py')
        shutil.copyfile(gs_out, dst2)
        os.utime(dst2, None)
        print('installed', dst, 'and', dst2)
        ui_text = HDUI_SRC
        o = os.path.join(OUT_DIR, 'hdui.py')
        open(o, 'w', encoding='utf-8', newline='\r\n').write(ui_text)
        d = os.path.join(GAME_SCRIPTS, 'hdui.py')
        shutil.copyfile(o, d)
        os.utime(d, None)
        print('installed', d)
