# HD-масштабирование: обёртки над оригинальным ui/cursor, оригинальные .pyc не тронуты.
#
# Модель (замерено итерацией 1):
#   движковый масштаб s = w_окна/1024 (в полноэкране w=3840, s=3.75; в оконном
#   DPI-unaware app-пространстве s_app = w_cfg/1024);
#   движок отдаёт мышь в своих единицах: m = физ_px/s;
#   вся Python-логика раскладки - в канвас-единицах (1024x768), GetPos/param - канвас.
#
# Схема 16:9: UI в 4:3-область по центру, полосы по бокам.
#   движковые единицы e = k*c + dx (X), e = k*c (Y);
#   k = h*1024/(w*768), dx = (1024 - 1024k)/2; при 3840x2160: k=0.75, dx=128;
#   экранное UI-масштабирование = k*s = h/768 (высота канваса = высота окна),
#   полосы = (w - 1024*k*s)/2;
#   мышь: c = (m - dx)/k (X), c = m/k (Y);
#   шрифт/лидеры/строки передаём движку БЕЗ k (движок рендерит их своим s;
#   UI-бокс в движковом пространстве уже имеет размер 1024k x 768k);
#   метрики GetText*/GetFontSize движок отдаёт в движковых единицах -> /k.
#
# ЖЁСТКАЯ ГАРАНТИЯ 4:3: при HD_K == 1 все обёртки вызывают оригинал с оригинальными
# аргументами и без пост-фикса - поведение побитово исходное.
#
# w,h берутся из hd.cfg (gamescript-хук), НЕ из GetScreenSize() - в оконном режиме
# GetScreenSize() возвращает desktop (проверено), это ломало k прошлой реализации.

import ui, cursor, globalscript, gamemodule

HD_K = 1
HD_DX = 0
HD_F = 0
HD_SY = 1
DY_E = 0
HD_YM = 1
HD_LOG = 0    # 1 = писать зонды в hdlog.txt рядом с игрой (dev-сборка); 0 для установки
HD_FM = 1     # экспериментальный множитель аргумента SetFont (1 = текущее поведение)
HD_GTH_F = 0.7563  # калибровка GetRealTextHeight для 16:9 (эталон 4:3 h_text=456; см. GetRealTextHeight)
# Множитель метрики ширины текста. Листы 16:9 теперь сжаты ВМЕСТЕ С АДВАНСАМИ
# (rescale_font3.py: и ink, и adv ячейки * k), поэтому движок сам отдаёт ширину уже
# сжатой (k-кратной) строки -> дополнительный множитель не нужен (1.0).
# При k==1 лист родной, метрика тоже родная.
HD_FNT_F = 1.0
# EXE-патч exepatch8 уже умножает pitch и advance шрифта на k при первой отрисовке
# шрифта (FUN_0041cc90). Значит движок рисует глифы в масштабе канваса
# (px = канвас*sy), канвасный pitch = pitch_engine/HD_K, а межстрочный шаг движка
# (pitch_engine + RowS) уже равен нужному k*(pitch_канвас + RowS) при RowS_new = k*RowS.
# Компенсировать pitch в RowS больше НЕЛЬЗЯ (двойная компенсация давала шаг меньше на
# pitch*(1-k) -> текст в 16:9 слипался по вертикали).
HD_FNT_PATCHED = 1
_HLC = [0]

# ---- Квадратные полноэкранные диалоги (R/E/F1, диалог с NPC) ----
# research_dlg (re_bkg.tga), энциклопедия (encyclopedy_scr.tga), все help-экраны
# (qrs_*.tga) и диалог talk_dlg (player_dialog.tga) объявлены канвасным rect'ом
# 1024x1024, но их видимая композиция -
# обычный канвас 1024x768: в 4:3 они заполняют экран целиком. _is_bg pass-through
# растягивал такой квадрат на ВСЮ ширину 16:9 (движковый rect 0..1024 * s = 3840) -
# отсюда зум, обрезка низа и рассинхрон с дочерними контролами. Для этих листов
# pass-through отключается: они идут обычной k-схемой (вид как в 4:3, вписаны по
# высоте), но по центру экрана. При k==1 трансформ identity - 4:3 побитово исходный.
DIALOG_TEX = ['re_bkg.tga', 'encyclopedy_scr.tga', 'player_dialog.tga']


def _is_dialog_tex(tex):
    if not tex:
        return 0
    t = str(tex).lower()
    if DIALOG_TEX.count(t) > 0:
        return 1
    if t[:4] == 'qrs_':
        return 1
    return 0


def _iround(v):
    if v >= 0:
        return int(v + 0.5)
    return -int(-v + 0.5)


def HD_Init(w, h, f):
    global HD_K, HD_DX, HD_F, HD_SY, DY_E
    # Пересчёт раскладки 4:3 (канвас 1024x768 из ui.BActionElement/COORDS_MM_*)
    # на 16:9. Движок отображает канвас на окно: окно - per-axis (sx=w/1024,
    # sy=h/768), fullscreen - единый s=w/1024. Контент (весь Python-UI) масштабируем
    # единообразно s_content = min(sx,sy) = h/768 и центрируем: канвасный центр
    # (512,384) -> центр экрана. Тогда
    #   EC(x) = k*x + DX, DX = 512*(1-k), k = h*1024/(w*768) = sy/sx;
    #   экран X = sx*EC = w/2 + sy*(x-512) - изотропно с Y;
    #   EY(y) = HD_SY*y: окно HD_SY=1 (движковый Y-квант = sy -> экран sy*y);
    #   fullscreen HD_SY=k (движковый квант s -> экран s*k*y = sy*y);
    # центрирующий сдвиг Y: движковый центр = h/(2*квант) = h/(2sy)=384 в окне,
    # h/(2s) = 512h/w = 384k в fullscreen - канвасный центр 384*HD_SY попадает
    # в него TOЖДЕСТВЕННО, DY_E = 0.
    k = float(h) * 1024 / (float(w) * 768)
    HD_K = k
    HD_F = f
    if f == 1:
        HD_SY = k
    else:
        HD_SY = 1.0
    DY_E = 0
    HD_DX = 512.0 - 512.0 * k
    if HD_LOG:
        f = open('hdlog.txt', 'a')
        f.write('HD w=' + repr(w) + ' h=' + repr(h) + ' k=' + repr(k) + ' dx=' + repr(HD_DX) + ' F=' + repr(HD_F) + ' SY=' + repr(HD_SY) + ' DY=' + repr(DY_E) + chr(10))
        f.close()


def EC(v):
    return int(v * HD_K + HD_DX + 0.5)


def EY(v):
    # ПОЗИЦИЯ Y: движковый квант = канвас в окне (HD_SY=1), k*c в fullscreen
    # (HD_SY=k) - экранная скорость h/768 в обоих режимах. DY_E - центрирующий
    # сдвиг, кольцо (канвасный центр RING_CY) в центр экрана. При k=1 identity.
    if HD_K == 1:
        return v
    return int(v * HD_SY + DY_E + 0.5)


def SY(v):
    # РАЗМЕР Y: движковый квант Y = канвас в окне (HD_SY=1), k*c в fullscreen.
    # Центрирующий сдвиг DY_E в размеры НЕ входит. При k=1 identity.
    if HD_K == 1:
        return v
    return int(v * HD_SY + 0.5)


def SC(v):
    # размер по X: только k, БЕЗ dx (dx - сдвиг позиции, к ширине не применяется)
    return int(v * HD_K + 0.5)


def HD_Shift():
    # Сдвиг прижатого к краю канваса контента к краю экрана - в КАНВАСНЫХ единицах.
    # Движковые единицы X: e = k*c + DX. Левый край экрана (e=0) <=> c = -DX/k;
    # правый край экрана (e=1024) <=> x+w = (1024-DX)/k, т.е. при x+w=1024 это +DX/k.
    # При 4:3 (k>=1, DX=0) сдвиг = 0 -> правок нет вообще.
    if HD_K >= 1.0:
        return 0
    return int(HD_DX / HD_K + 0.5)


def HD_EdgeFix(templ, left_names, right_names):
    # 16:9: контролы шаблона, прижатые к краю КАНВАСА, придвигаются к краю ЭКРАНА
    # сдвигом (позиция bb[0]), размеры движку передаются как SC(w)=k*w, SY(h)=k*h
    # -> аспект текстуры сохраняется (растяжения нет), пропорции = 4:3.
    # Дети едут с родителем: BGCS_CHILD_COORD (rect = rect родителя + локальный
    # offset, UpdatePos) и BGCA_SHIFT (offset = дельта движения родителя) -
    # компенсация детей НЕ нужна, колонка кнопок/текста сохраняется как целое.
    # Правило применяем ТОЛЬКО по списку имён: generic-детект "bb[0]==0" ломает
    # детей центрированных групп (консоль оверленда mc_left при bb[0]=0 внутри
    # concole) и полноширинные ленты (selection_board [0,0,1024,5]).
    sh = HD_Shift()
    _log('HDEDGE-ENTER sh=' + repr(sh))
    if sh == 0:
        return
    try:
        ks = templ.keys()
        for i in range(len(ks)):
            e = templ[ks[i]]
            if not e.has_key('name'):
                continue
            bb = e.get('billboardmain', [])
            if len(bb) < 4:
                continue
            nm = e['name']
            if left_names.count(nm) > 0:
                d = -sh
            elif right_names.count(nm) > 0:
                d = sh
            else:
                continue
            bb[0] = int(bb[0]) + d
            _log('HDMOVE L/R ' + str(nm) + ' x=' + repr(bb[0]))
            # billboardhover - ОТДЕЛЬНЫЙ rect с теми же канвас-абсолютными
            # координатами (btn_pause hover [109,628] = main [109,628]). Если
            # сдвинуть только main, яркая hover-иконка остаётся на прежнем
            # месте ("висит в воздухе") - сдвигаем hover тем же delta.
            bh = e.get('billboardhover', [])
            if len(bh) >= 4:
                bh[0] = int(bh[0]) + d
                _log('HDMOVEH ' + str(nm) + ' x=' + repr(bh[0]))
    except Exception:
        import sys
        _log('HDEDGE-EXC ' + repr(sys.exc_info()))
    return


def MC(m):
    return (m - HD_DX) / HD_K


def MY(m):
    # Окно: движок отдаёт m = физ/sy = канвасная Y (квант sy). Fullscreen: m = физ/s,
    # канвасная Y = m/k. Позиция элемента в движковых единицах EY = HD_SY*c + DY_E;
    # hit-test движка сравнивает m с EY -> c = (m - DY_E)/HD_SY. При k=1 identity.
    if HD_K == 1:
        return m
    return (m - DY_E) / HD_SY


def _log_coords():
    # координаты глифов главного меню (COORDS_MM_*) живут только в dictionary_rus.pyc,
    # берём их из словаря в рантайме - нужно для съёмки контрольных точек
    try:
        d = globalscript.GetDictionary()
        for nm in ('COORDS_MM_Load', 'COORDS_MM_Save', 'COORDS_MM_NewGame', 'COORDS_MM_Quit',
                   'COORDS_MM_Options', 'COORDS_MM_Help', 'COORDS_MM_Resume'):
            _log('COORDS ' + nm + ' ' + repr(getattr(d, nm)))
    except Exception:
        _log('COORDS fail')


def _log(s):
    if not HD_LOG:
        return
    _HLC[0] = _HLC[0] + 1
    if _HLC[0] > 12000:
        return
    f = open('hdlog.txt', 'a')
    f.write(str(s) + chr(10))
    f.close()


def _fprobe(bb, name):
    # зонд движковых метрик текста: вернул ли движок размер/ширину/высоту после SetFont
    _FPC[0] = _FPC[0] + 1
    if _FPC[0] > 60:
        return
    try:
        fs = bb.GetFontSize()
        tw = bb.GetTextWidth()
        th = bb.GetTextHeight()
    except Exception:
        fs = tw = th = 'NA'
    _log('FPROBE ' + repr((name, fs, tw, th)))


_FPC = [0]


def _bb(self, BillBoardName):
    # billboard-объект движка и канвас-параметры элемента
    if not self.__dict__.has_key(BillBoardName) or not self.__dict__[BillBoardName]:
        return None, None
    p = self.__dict__[BillBoardName + 'Params']
    return self.__dict__[BillBoardName], p


def _is_bg(p):
    # фон (splash/сцена) покрывает канвас почти целиком: передаём движку без масштаба,
    # канвас 1024x768 в движковом пространстве = весь экран (мир на весь экран);
    # масштабирование UI не трогает фон
    return p['w'] >= 1000 and p['h'] >= 700


def _bg_fix(self, BillBoardName, tag):
    # фон: PASS-THROUGH без масштаба (1024x1024 канвас). Движок в fullscreen растягивает
    # текстуру по rect единым s=w/1024 и клиппит по окну; blit инвертирует строки
    # (как qrs_battle). 2048x2048 с артом в строках 896..2047 -> видимые канвасные
    # строки 0..h/s = текстура rows 2047..(2048-2h/s): арт заполняет окно ровно,
    # aspect един (3840/2160 = 2048/1152), чёрных полос нет. sy2-сжатие давало
    # неединый Y-масштаб и чёрный прямоугольник снизу. В окне и при k=1 - identity.
    return


def _push(self, BillBoardName, tag):
    # пост-фикс: применить движку канвас-параметры, пересчитанные в движковые единицы
    bb, p = _bb(self, BillBoardName)
    if bb is None:
        return
    if not self.__dict__.get('HDDBG', 0) and _is_bg(p):
        # фон (splash/сцена) - pass-through; фон квадратного диалога (HDDBG) идёт
        # обычной k-схемой, см. DIALOG_TEX
        _bg_fix(self, BillBoardName, tag)
        return
    e = (EC(p['x']), EY(p['y']), p['prior'], SC(p['w']), SY(p['h']))
    bb.Set(e[0], e[1], e[2], e[3], e[4])
    _log(tag + ' SY=' + repr(HD_SY) + ' ' + repr((BillBoardName, e)))


# ---- обёртки BActionElement ----

_orig_InitBillboard = ui.BActionElement.InitBillboard
_orig_SetPos = ui.BActionElement.SetPos
_orig_AutoPosition = ui.BActionElement.AutoPosition
_orig_CalcPopupPos = ui.CalcPopupPos
_orig_SetPrior = ui.BActionElement.SetPrior
_orig_SetTextOffset = ui.BActionElement.SetTextOffset
_orig_CheckRefInPoint = ui.BActionElement.CheckRefInPoint
_orig_GetTextWidth = ui.BActionElement.GetTextWidth
_orig_GetTextHeight = ui.BActionElement.GetTextHeight
_orig_GetFontSize = ui.BActionElement.GetFontSize
_orig_MouseMove = ui.BActionElement.MouseMove
_orig_LBDown = ui.BActionElement.LBDown
_orig_RBDown = ui.BActionElement.RBDown
_orig_LBUp = ui.BActionElement.LBUp
_orig_RBUp = ui.BActionElement.RBUp


def InitBillboard(self, InitParams, ParametersName, BillBoardName, BillBoardMaterialName, BillBoardFontName):
    if HD_K == 1 or len(InitParams) < 4:
        _orig_InitBillboard(self, InitParams, ParametersName, BillBoardName, BillBoardMaterialName, BillBoardFontName)
        return
    dbg = 0
    if len(InitParams) > 5 and _is_bg({'w': InitParams[2], 'h': InitParams[3]}) and _is_dialog_tex(InitParams[5]):
        # фон квадратного диалога (R/E/F1): НЕ pass-through, обычная k-схема.
        # Метка на элементе: последующие _push (SetPos/AutoPosition/SetPrior)
        # тоже идут k-схемой (см. DIALOG_TEX).
        dbg = 1
        self.__dict__['HDDBG'] = 1
    # фон - pass-through: оригинальный rect (1024x1024) без пересчёта
    if not dbg and _is_bg({'w': InitParams[2], 'h': InitParams[3]}):
        _orig_InitBillboard(self, InitParams, ParametersName, BillBoardName, BillBoardMaterialName, BillBoardFontName)
        _log('HDBGINIT BG ' + repr((BillBoardName, InitParams[0], InitParams[1], InitParams[2], InitParams[3])))
        return
    # Движок ФИКСИРУЕТ ширину текстовой раскладки при создании билборда
    # (SetText/SetFont): последующий Set() её НЕ пересчитывает. Поэтому центрирование
    # и перенос строк всегда считались по исходной канвасной ширине, хотя рендерится
    # бокс уже сжатым -> центрированный текст уезжал вправо (напр. заголовок «Настройки»).
    # Передаём движку масштабированные координаты/размер СРАЗУ, а каноничные канвасные
    # значения возвращаем в Params (bgui-раскладка работает в канвасе).
    cx, cy, cw, ch = InitParams[0], InitParams[1], InitParams[2], InitParams[3]
    eip = list(InitParams)
    eip[0] = EC(cx)
    eip[1] = EY(cy)
    eip[2] = SC(cw)
    eip[3] = SY(ch)
    _orig_InitBillboard(self, eip, ParametersName, BillBoardName, BillBoardMaterialName, BillBoardFontName)
    if dbg:
        _log('HDDLGBG ' + repr((BillBoardName, eip[0], eip[1], eip[2], eip[3])))
    par = self.__dict__[ParametersName]
    par['x'] = cx
    par['y'] = cy
    par['w'] = cw
    par['h'] = ch
    bb = self.__dict__.get(BillBoardName)
    if bb is None:
        return
    tp = par.get('TextParams')
    if tp:
        # шрифт/лидеры/строки - движковые единицы (движок рендерит их своим s)
        fnt = self.__dict__.get(BillBoardFontName)
        if fnt:
            bb.SetFont(fnt, int(tp['Size'] * HD_K * HD_FM + 0.5))
        # Межстрочный шаг движка = GetFontSize() + RowS, где GetFontSize() - pitch шрифта.
        # После EXE-патча (HD_FNT_PATCHED) pitch в движке уже k-кратный, канвасный
        # pitch = pitch_engine/HD_K, поэтому шаг в канвасных единицах =
        # pitch_канвас + RowS_new/HD_K -> RowS_new = k*RowS. Компенсация pitch не нужна.
        # Без патча (HD_FNT_PATCHED=0) pitch нативный: нужен движковый шаг
        # k*(pitch+RowS) -> RowS_new = k*(pitch+RowS) - pitch.
        pitch = bb.GetFontSize()
        if HD_FNT_PATCHED:
            rs_new = _iround(HD_K * tp['RowS'])
        elif pitch and pitch != 'NA':
            rs_new = _iround(HD_K * (pitch + tp['RowS']) - pitch)
        else:
            rs_new = SY(tp['RowS'])
        bb.SetTextLeading(SY(tp['Leading']))
        bb.SetTextRowSpacing(rs_new)
        if tp.has_key('TextDX'):
            bb.SetTextOffset(int(tp['TextDX'] * HD_K + 0.5), SY(tp['TextDY']))
        _log('HDINITBB ' + repr((BillBoardName, EC(cx), EY(cy), SC(cw), SY(ch))) + ' size=' + repr(int(tp['Size'] * HD_K * HD_FM + 0.5)) + ' pitch=' + repr(pitch) + ' RowS=' + repr(tp['RowS']) + ' Lead=' + repr(tp['Leading']) + ' rs=' + repr(rs_new) + ' txt=' + repr(str(tp['TextString'])[:32]))
        _fprobe(bb, BillBoardName)


def SetPos(self, BillBoardName, x, y, w=-1, h=-1):
    _orig_SetPos(self, BillBoardName, x, y, w, h)
    if HD_K == 1:
        return
    _push(self, BillBoardName, 'HDSETPOS')


def AutoPosition(self, BillBoardName):
    _orig_AutoPosition(self, BillBoardName)
    if HD_K == 1:
        return
    _push(self, BillBoardName, 'HDAUTOP')


# Тултип: оригинальный ui.CalcPopupPos проверяет 4 кандидата (право-низ, право-верх,
# лево-верх, лево-низ от курсора) против канвасного бокса GetScreenRect() =
# (0,0,1024,768). В 16:9 видимый канвас-бокс - x = -DX/k .. (1024-DX)/k (EdgeFix
# уводит левую панель в x < 0), y = 0..768; для иконок левой панели (pt.x < 0)
# оригинальный тест даёт отказ для всех кандидатов, ArrangeTooltip оставляет
# rect (1,1), и тултип рисуется у верха экрана. Правило: кандидаты проверяются
# против ВИДИМОГО канвас-бокса; если ни один не влез - тултип ставится у курсора
# (кандидат право-низ) и клампится в бокс. При k==1 - оригинал (4:3 побитово).
# Границы бокса строже, чем <= (IsRectInRect: right < rightD, bottom < bottomD).
def CalcPopupPos(pt, rect):
    if HD_K == 1:
        return _orig_CalcPopupPos(pt, rect)
    sz = ui.GetRectSize(rect)
    cw = 40
    dl = ui.CreateSize(pt['x'] - rect['left'], pt['y'] - rect['top'])
    lx = -HD_DX / HD_K
    rx = (1024.0 - HD_DX) / HD_K
    pairs = [(dl['sx'], dl['sy'] + cw),
             (dl['sx'], dl['sy'] - sz['sy'] - cw / 4),
             (dl['sx'] - sz['sx'], dl['sy'] - sz['sy'] - cw / 4),
             (dl['sx'] - sz['sx'], dl['sy'] + cw)]
    i = 0
    while i < len(pairs):
        rt = {'left': rect['left'] + pairs[i][0], 'right': rect['right'] + pairs[i][0],
              'top': rect['top'] + pairs[i][1], 'bottom': rect['bottom'] + pairs[i][1]}
        if rt['left'] >= lx and rt['right'] < rx and rt['top'] >= 0 and rt['bottom'] < 768:
            _log('HDPOP cand=' + repr(i) + ' rect=' + repr((rt['left'], rt['top'], rt['right'], rt['bottom'])))
            return rt
        i += 1
    # ни один кандидат не влез: тултип у курсора (право-низ), клампим в бокс
    w = sz['sx']
    h = sz['sy']
    left = rect['left'] + dl['sx']
    top = rect['top'] + dl['sy'] + cw
    bw = rx - lx
    if w >= bw:
        cx = (lx + rx) / 2
        left = cx - w / 2
    else:
        if left < lx:
            left = lx
        if left + w > rx:
            left = rx - 1 - w
    if h >= 768:
        top = 0
    else:
        if top < 0:
            top = 0
        if top + h > 768:
            top = 767 - h
    rt = {'left': left, 'right': left + w, 'top': top, 'bottom': top + h}
    _log('HDPOP clamp rect=' + repr((rt['left'], rt['top'], rt['right'], rt['bottom'])))
    return rt


def SetPrior(self, BillBoardName, prior):
    _orig_SetPrior(self, BillBoardName, prior)
    if HD_K == 1:
        return
    _push(self, BillBoardName, 'HDSET2')


def SetTextOffset(self, BillBoardName, dx, dy):
    _orig_SetTextOffset(self, BillBoardName, dx, dy)
    # смещение текста (канвас) -> движку в движковых единицах по X
    if HD_K == 1:
        return
    bb, p = _bb(self, BillBoardName)
    if bb is None:
        return
    # offset текста - внутри rect: X масштабируется k, Y - HD_SY
    bb.SetTextOffset(int(dx * HD_K + 0.5), int(dy * HD_SY + 0.5))


def CheckRefInPoint(self, BillBoardName, x, y):
    if HD_K == 1:
        return _orig_CheckRefInPoint(self, BillBoardName, x, y)
    if not self.__dict__.has_key(BillBoardName) or not self.__dict__[BillBoardName]:
        return -1
    if self.IsRefFuncValid(BillBoardName):
        return self.__dict__[BillBoardName].CheckRefInPoint(EC(x), EY(y))
    return -1


def GetTextWidth(self, BillBoardName):
    r = _orig_GetTextWidth(self, BillBoardName)
    if HD_K == 1:
        return r
    # движковая ширина - по advance оригинального листа (ink сжат, advance не тронут);
    # раскладка (центрирование строк) должна считать width сжатого текста
    return r / HD_K * HD_FNT_F


def GetTextHeight(self, BillBoardName):
    # движковая Y = HD_SY * канвасная Y -> делим обратно (в окне SY=1 - identity)
    r = _orig_GetTextHeight(self, BillBoardName)
    if HD_SY == 1:
        return r
    return r / HD_SY


def GetFontSize(self, BillBoardName):
    r = _orig_GetFontSize(self, BillBoardName)
    if HD_K == 1:
        return r
    return r / HD_K


# раскладка читает метрики текста: ui.GetRealTextHeight(ptr, bbname) вызывает движковые
# ptr.GetFontSize/GetTextParams['RowS'] НАПРЯМУЮ (не через обёртки выше) и использует
# результат как канвас. Движок вернул значения в движковых единицах (Size*k, RowS*HD_SY),
# поэтому коробки строк/тултипов в 16:9 получаются в k раз короче -> текст их переполняет.
# Пересчёт в канвас: ht/k (аргумент SetFont всегда = Size*k), RowS/HD_SY (ведущий/межстрочное
# передаём через SY), число строк - без масштаба. При k==1 identity.
def GetRealTextHeight(ptr, bbname):
    if HD_K == 1:
        r = ui.GetRealTextHeight_orig(ptr, bbname)
        _log('HDGTH43 raw=' + repr(ptr.GetTextHeight(bbname)) + ' fs=' + repr(_orig_GetFontSize(ptr, bbname)) + ' RowS=' + repr(ptr.GetTextParams(bbname)['RowS']) + ' -> ' + repr(r))
        return r
    lines = ptr.GetTextHeight(bbname) - 1
    # GetFontSize у BE уже обёрнут и отдаёт движковую метрицу /k; берем оригинал
    ht = _orig_GetFontSize(ptr, bbname) / HD_K
    rs = ptr.GetTextParams(bbname)['RowS'] / HD_SY
    # Калибровка 16:9: коробка текста по движковому GetTextHeight выходит в 1.322 раза
    # выше эталона 4:3 (BTRECT: 4K h_text=602.99, 2K-F1 603.56 против 456 канвас в 4:3;
    # движковый raw от s не зависит). f = 456/603.3; при k==1 ветка выше - identity.
    r = lines * (ht + rs) * HD_GTH_F
    _log('HDGTH raw=' + repr(ptr.GetTextHeight(bbname)) + ' fs=' + repr(_orig_GetFontSize(ptr, bbname)) + ' RowS=' + repr(ptr.GetTextParams(bbname)['RowS']) + ' -> ' + repr(r))
    return r


_orig_GetRealTextHeight = ui.GetRealTextHeight
ui.GetRealTextHeight_orig = _orig_GetRealTextHeight


# обработчики мыши элемента: движок присылает движковые единицы, Python-логика - канвас

def MouseMove(self, mx, my, flags):
    if HD_K == 1:
        return _orig_MouseMove(self, mx, my, flags)
    return _orig_MouseMove(self, MC(mx), MY(my), flags)


def LBDown(self, mx, my, flags):
    if HD_K == 1:
        return _orig_LBDown(self, mx, my, flags)
    return _orig_LBDown(self, MC(mx), MY(my), flags)


def RBDown(self, mx, my, flags):
    if HD_K == 1:
        return _orig_RBDown(self, mx, my, flags)
    return _orig_RBDown(self, MC(mx), MY(my), flags)


def LBUp(self, mx, my, flags):
    if HD_K == 1:
        return _orig_LBUp(self, mx, my, flags)
    return _orig_LBUp(self, MC(mx), MY(my), flags)


def RBUp(self, mx, my, flags):
    if HD_K == 1:
        return _orig_RBUp(self, mx, my, flags)
    return _orig_RBUp(self, MC(mx), MY(my), flags)


# глобальные обработчики курсора (ui/cursor.BCursor): движковые единицы -> канвас;
# SendMessage отправляет движковые единицы (движок трактует их как свои)

_orig_h_MouseMove = cursor.BCursor.h_MouseMove
_orig_h_MouseLBDown = cursor.BCursor.h_MouseLBDown
_orig_h_MouseRBDown = cursor.BCursor.h_MouseRBDown
_orig_h_MouseLBUp = cursor.BCursor.h_MouseLBUp
_orig_h_MouseRBUp = cursor.BCursor.h_MouseRBUp
_orig_h_MouseWheel = cursor.BCursor.h_MouseWheel
_orig_h_MouseLDblClk = cursor.BCursor.h_MouseLDblClk


_SCENE = ['']


def _log_scene():
    # имя текущей сцены ('menu'/'overland'/'battle'): пробам съёмки нужно дождаться
    # строки "SCENE battle" в hdlog.txt, чтобы снять снимок именно в бою.
    # Имя сцены у движка не читается (GetName нет), поэтому определяем по живым
    # спискам UI: olmenu.OLmenu / battlemenu.Battlemenu непусты ровно когда
    # соответствующий меню-объект создан. Логируем только смену имени.
    nm = 'menu'
    try:
        import olmenu
        if len(olmenu.OLmenu) > 0:
            nm = 'overland'
        import battlemenu
        if len(battlemenu.Battlemenu) > 0:
            nm = 'battle'
    except Exception:
        nm = '?'
    if nm != _SCENE[0]:
        _SCENE[0] = nm
        _log('SCENE ' + nm)


def h_MouseMove(self, mx, my, flags):
    _log_scene()
    if HD_K == 1:
        return _orig_h_MouseMove(self, mx, my, flags)
    cx = MC(mx)
    cy = MY(my)
    self.ms_x = cx
    self.ms_y = cy
    self.ms_ox = cx
    self.ms_oy = cy
    self.UpdatePos()
    if self.Scene != -1:
        id = self.Get()
        if id != cursor.CUR_NONE:
            self.ms_x += self.cur_table[id][3]
            self.ms_y += self.cur_table[id][4]
    gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_MOUSEMOVE, 0, (EC(self.ms_x), EY(self.ms_y), flags))


def h_MouseLBDown(self, mx, my, flags):
    if HD_K == 1:
        return _orig_h_MouseLBDown(self, mx, my, flags)
    gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_LBDOWN, 0, (EC(self.ms_x), EY(self.ms_y), flags))


def h_MouseRBDown(self, mx, my, flags):
    if HD_K == 1:
        return _orig_h_MouseRBDown(self, mx, my, flags)
    gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_RBDOWN, 0, (EC(self.ms_x), EY(self.ms_y), flags))


def h_MouseLBUp(self, mx, my, flags):
    if HD_K == 1:
        return _orig_h_MouseLBUp(self, mx, my, flags)
    gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_LBUP, 0, (EC(self.ms_x), EY(self.ms_y), flags))


def h_MouseRBUp(self, mx, my, flags):
    if HD_K == 1:
        return _orig_h_MouseRBUp(self, mx, my, flags)
    gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_RBUP, 0, (EC(self.ms_x), EY(self.ms_y), flags))


def h_MouseWheel(self, mx, my, flags, wheel):
    if HD_K == 1:
        return _orig_h_MouseWheel(self, mx, my, flags, wheel)
    gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_MOUSEWHEEL, 0, (EC(self.ms_x), EY(self.ms_y), flags, wheel))


def h_MouseLDblClk(self, mx, my, flags):
    if HD_K == 1:
        return _orig_h_MouseLDblClk(self, mx, my, flags)
    gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_LBDBLCLK, 0, (EC(self.ms_x), EY(self.ms_y), flags))


def install():
    BE = ui.BActionElement
    BE.InitBillboard = InitBillboard
    BE.SetPos = SetPos
    BE.AutoPosition = AutoPosition
    BE.SetPrior = SetPrior
    BE.SetTextOffset = SetTextOffset
    BE.CheckRefInPoint = CheckRefInPoint
    BE.GetTextWidth = GetTextWidth
    BE.GetTextHeight = GetTextHeight
    BE.GetFontSize = GetFontSize
    ui.GetRealTextHeight = GetRealTextHeight
    ui.CalcPopupPos = CalcPopupPos
    BE.MouseMove = MouseMove
    BE.LBDown = LBDown
    BE.RBDown = RBDown
    BE.LBUp = LBUp
    BE.RBUp = RBUp
    BC = cursor.BCursor
    BC.h_MouseMove = h_MouseMove
    BC.h_MouseLBDown = h_MouseLBDown
    BC.h_MouseRBDown = h_MouseRBDown
    BC.h_MouseLBUp = h_MouseLBUp
    BC.h_MouseRBUp = h_MouseRBUp
    BC.h_MouseWheel = h_MouseWheel
    BC.h_MouseLDblClk = h_MouseLDblClk
    _log('HD install ok K=' + repr(HD_K) + ' DX=' + repr(HD_DX))
    _log_coords()
