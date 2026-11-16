import gamemodule, gamescript, globalscript, spells, overlandcharacters, help, vkeys, ui, bgui, gui_templates, cursor, copy, units_base, std_misc, bgui_cash, bconsole, d_tools, bhud_sounds, tutor_manager
from bgui_constant import *
from battlecamera import *
import gamescript
from help_manager import *
import g_defs


def _plog(s):
    # лог правки 16:9 (абсолютный путь, латиница): канал наблюдения за созданием UI боя
    try:
        f = open('config[game.install_dir]/hdlog.txt', 'a')
        f.write(str(s) + chr(10))
        f.close()
    except Exception:
        pass


_plog('BM-MODULE-LOAD')


def _pexc(tag):
    # полное исключение с трассировкой (имя файла/строки/функции) - движок глотает
    # трейсбек, поэтому печатаем его сами: sys.exc_info()[2] -> traceback.extract_tb
    try:
        import sys, traceback
        ex = sys.exc_info()[1]
        s = tag + ' ' + str(ex)
        for it in traceback.extract_tb(sys.exc_info()[2]):
            s = s + ' | ' + str(it[0]) + ':' + str(it[1]) + ' ' + str(it[3])
        _plog(s)
    except Exception:
        pass

Select_Imp = 0
SelectedObjects = []
SelectedAlien = None
Battlemenu = []
BattlemenuScene = None
BattleCash = None
FONT_H = 18
LEFT_PART_PRIORITY = 2000
HL_LOG_PAUSE = 10
BHUD_ST_NORMAL = 0
BHUD_ST_WARNING = 1
BHUD_ST_INFO = 2
BHUD_STColors = {}
BHUD_STColors[BHUD_ST_NORMAL] = ui.GUI_TPOS
BHUD_STColors[BHUD_ST_WARNING] = ui.GUI_TWARN
BHUD_STColors[BHUD_ST_INFO] = ui.GUI_TNORMAL
MM_NAMES_LEN = 9
MM_DOMCENTER = 'mm_domcen'
MM_CAMERA = 'mm_camera'
MM_PLAYER = 'mm_player'
MM_PLAYER_UNIT = 'mm_plunit'
MM_OPPONENT = 'mm_oppont'
MM_OPPONENT_UNIT = 'mm_oppunt'
MM_RESPAWN = 'mm_respwn'
MM_MILITIA = 'mm_militi'
dict = globalscript.GetDictionary()
ResumeGame = 0
STATUS_TEXT_DELAY_TIME = 10

class BBattleCash:

    def __init__(self):
        self.TexturesCash = {}
        self.GraphObjectCash = {}
        self.SceneObjectCash = {}
        if globalscript.GUICash_Disabled:
            return
        TexturesList = []
        GraphObjectList = []
        SceneObjectList = []
        pl = overlandcharacters.Player
        bgui_cash.CreatePlayerCashList(pl, TexturesList, SceneObjectList)
        GraphObjectList.append('bhud_movehere')
        TexturesList.append('bhud_uniticonrim01.tga')
        TexturesList.append('bhud_health02.tga')
        TexturesList.append('bhud_health01.tga')
        prefix = 'battle'
        bgui_cash.CreateTexturesCash(TexturesList, self.TexturesCash, prefix)
        bgui_cash.CreateGraphObjectCash(GraphObjectList, self.GraphObjectCash, prefix)
        bgui_cash.CreateSceneObjectCash(SceneObjectList, self.SceneObjectCash, prefix)
        return
        return

    def Dest(self):
        if globalscript.GUICash_Disabled:
            return
        bgui_cash.FreeCash(self.SceneObjectCash)
        bgui_cash.FreeCash(self.GraphObjectCash)
        bgui_cash.FreeCash(self.TexturesCash)
        del self.TexturesCash
        del self.GraphObjectCash
        del self.SceneObjectCash
        return
        return


def ConfirmLooseBattleDialog():
    bgui.ShowYesNoDlg(dict.BATTLE_MSG_LooseBattle, LooseBattle, None)
    return
    return


def LooseBattle():
    if len(gamemodule.GameManager_GetScene().GetBDomainObject().Attackers):
        gamemodule.GameManager_GetScene().GetBDomainObject().KillOpponent(overlandcharacters.Player)
    else:
        gamemodule.GameManager_GetScene().GetBDomainObject().MiniDomain.CloseBattle()
    return
    return


def BackToMainMenu():
    global Battlemenu
    global ResumeGame
    import mainmenu
    if len(Battlemenu):
        Battlemenu[0].ResetCallBack(None, [0, 0, 0])
    ResumeGame = 0
    if gamemodule.GamePaused() == 0:
        gamemodule.PauseGame(1)
        ResumeGame = 1
    if len(Battlemenu) > 0:
        Battlemenu[0].UpdateSpellsHK(1)
    mainmenu.ShowMainmenu(1, [1, 1, 2, 2, 2, 3])
    return


def UpdateCamera():
    gamemodule.GameManager_GetScene().GetBDomainObject().Camera.UpdateFromConfig()
    return
    return


class CGaugeBillboard:

    def __init__(self, x, y, w, h, TextureName):
        import math
        self.Gauge = ui.BActionElement([x, y, w, h, [1, 1, 1, 1], TextureName, globalscript.BBlend_Add], [], [])
        self.Gauge.Unsubscribe()
        self.h = h
        self.HalfOfPI = math.pi / 2.0
        self.Value = -self.HalfOfPI
        self.ValueDelta = 0.0
        return

    def SetInitValue(self, val):
        self.Value = -self.HalfOfPI
        self.ValueDelta = val / self.HalfOfPI
        if self.ValueDelta == 0:
            self.ValueDelta = 1
        return

    def SetValue(self, val):
        self.Value = val / self.ValueDelta - self.HalfOfPI
        self.Gauge.RotateBillBoardAroundPt('MainBB', -self.Value, [0, self.h])
        return

    def Dest(self):
        self.Gauge.Dest()
        self.Gauge = None
        return


class CBattlemenu:

    def CreateButton(self, x, y, w, h, action, tname, tt, spell=None, isspellicon=0, ttb='', priority=LEFT_PART_PRIORITY + 5, advname=''):
        tx = x + w / 2 - 50
        ty = y + h / 2 - 9
        tw = 100
        th = 18
        bbtt = []
        bbttf = []
        tp = 0
        if not tt == '':
            tp = 1
            bbtt = [1, 1, ui.GUI_TOOLTIP_MAX_WIDTH, 10, ui.GUI_TOOLTIP_SMALL_COLOR, ui.GUI_TOOLTIP_SMALL_MATERIAL, ui.GUI_TOOLTIP_SMALL_BLEND, ui.GUI_TOOLTIP_SMALL_TEXT]
            bbtt[7][0] = tt
        if not ttb == '':
            bbttf = [1, 1, ui.GUI_TOOLTIP_MAX_WIDTH, 10, ui.GUI_TOOLTIP_LARGE_COLOR, ui.GUI_TOOLTIP_LARGE_MATERIAL, ui.GUI_TOOLTIP_LARGE_BLEND, ui.GUI_TOOLTIP_LARGE_TEXT]
            bbttf[7][0] = ttb
        if isspellicon == 0:
            tmp = ui.BActionElement([x, y, w, h, [0.5, 0.5, 0.5, 1], tname, globalscript.BBlend_Add], [x, y, w, h, [1, 1, 1, 1], tname, globalscript.BBlend_Add], bbtt, bbttf, tp)
            tmp.SetPrior('MainBB', priority)
            tmp.SetPrior('HoverBB', priority + 1)
        if isspellicon == 1:
            templ = copy.deepcopy(gui_templates.battle_spell)
            spellname = advname + templ[0]['name']
            if advname == '':
                spellname += spell.GetKeyName()
            templ[0]['name'] = spellname
            templ[0]['billboardmain'][0] = x
            templ[0]['billboardmain'][1] = y
            templ[0]['billboardmain'][2] = w
            templ[0]['billboardmain'][3] = h
            templ[0]['billboardmain'][4] = [1, 1, 1, 0.65]
            templ[0]['billboardmain'][5] = tname
            templ[0]['billboardhover'][0] = x
            templ[0]['billboardhover'][1] = y
            templ[0]['billboardhover'][2] = w
            templ[0]['billboardhover'][3] = h
            templ[0]['billboardhover'][4] = [1, 1, 1, 1]
            templ[0]['billboardhover'][5] = tname
            templ[0]['billboardtooltip'] = [tt, ttb]
            templ[1]['name'] += spellname
            templ[1]['parent'] = spellname
            spell_c = bgui.CreateFromTemplate(templ, ModifyCallBackSpell, [self])
            spell_c[spellname].Param['castaction'] = action
            spell_c[spellname].Param['spell'] = spell
            return spell_c[spellname]
        if isspellicon == 2:
            tmp = ui.BActionElement([x, y, w, h, [0.5, 0.5, 0.5, 1], tname, globalscript.BBlend_Add], [x, y, w, h, [1, 1, 1, 1], tname, globalscript.BBlend_Add], bbtt, bbttf, tp)
            tmp.SetPrior('MainBB', priority)
            tmp.SetPrior('HoverBB', priority + 1)
            tmp.AddActionFunction('LBDown', self.SelectButton, ())
        if isspellicon == 3:
            tmp = ui.BActionElement([x, y, w, h, [1, 1, 1, 0.65], tname, globalscript.BBlend_Blend], [x, y, w, h, [1, 1, 1, 1], tname, globalscript.BBlend_Blend], bbtt, bbttf, tp)
            a = ui.BActionElement([x, y, w, h, [1, 1, 1, 1], 'ui_border.tga', globalscript.BBlend_Blend], [], [])
            a.SetPrior('MainBB', priority)
            tmp.SetActionSound('./data/sounds/effects/ui/button_press01.wav')
            return [tmp, a]
        if isspellicon == 4:
            tmp = ui.BActionElement([x, y, w, h, [1, 1, 1, 0.65], tname, globalscript.BBlend_Blend], [x, y, w, h, [1, 1, 1, 1], tname, globalscript.BBlend_Blend], bbtt, bbttf, tp)
        tmp.SetActionSound('./data/sounds/effects/ui/button_press01.wav')
        tmp.AddActionFunction('Action', action, ())
        self.buttons.append(tmp)
        return tmp
        return

    def CreateUISimple(self, x, y, w, h, tname, blend_func=0):
        if blend_func == 1:
            blend_func = globalscript.BBlend_Add
        else:
            blend_func = globalscript.BBlend_Blend
        tmp = ui.BActionElement([x, y, w, h, [1, 1, 1, 1], tname, blend_func], [], [])
        self.buttons.append(tmp)
        return tmp
        return

    def ResetCallBack(self, ae, mouse_pos):
        if self.SpellSelectedToCast != None and self.StartCast != 0:
            if overlandcharacters.Player.BattleUnit.NowCasting:
                overlandcharacters.Player.BattleUnit.NowCasting.BreakCast(1)
            self.SpellSelectedToCast = None
        self.StartCast = 0
        self.MUSprogress = 0
        self.MUS.Off()
        ui.ResetTooltip()
        return
        return

    def LPickDown(self, control, x, y, flags):
        if self.NeedToResetCapture:
            self.NeedToResetCapture = 0
            ui.ResetCapture(self.LastMousePos)
        if self.EnableSelection == 0:
            return
        bgc = control.GraphControl
        if bgc.GetControlFromPoint(ui.CreatePoint(x, y)) == None:
            self.MUSprogress = 1
            self.MUSstart[0] = x
            self.MUSstart[1] = y
        return

    def LPickUp(self, control, x, y, flags):
        global SelectedAlien
        global SelectedObjects
        if self.NeedToResetCapture:
            self.NeedToResetCapture = 0
            ui.ResetCapture(self.LastMousePos)
        if self.MUSprogress == 2:
            self.NeedToResetCapture = 1
            if self.MUSend[0] < self.MUSstart[0]:
                x = self.MUSend[0]
                w = self.MUSstart[0] - self.MUSend[0]
            else:
                x = self.MUSstart[0]
                w = self.MUSend[0] - self.MUSstart[0]
            if self.MUSend[1] < self.MUSstart[1]:
                y = self.MUSend[1]
                h = self.MUSstart[1] - self.MUSend[1]
            else:
                y = self.MUSstart[1]
                h = self.MUSend[1] - self.MUSstart[1]
            self.MUS.Off()
            if w > 5 or h > 5:
                objs = gamemodule.GameManager_GetScene().SelectObjects(x, y, w, h)
                if len(objs) == 0:
                    return
                if not gamemodule.IsKeyPressed(globalscript.PlayerConfig.bcam['selection_modifier'][0]):
                    self.UnselectAllUnits(0)
                sz = len(objs)
                i = 0
                while i < sz:
                    char = objs[i].GetPythonCharacter()
                    if gamemodule.IsKeyPressed(globalscript.PlayerConfig.bcam['selection_modifier'][0]) and char.Selected:
                        self.UnselectUnit(char, 0)
                    elif char.Owner == overlandcharacters.Player and not char.Selected:
                        self.SelectUnit(char, 0)
                    i += 1

                self.UpdateMS('reposition', None, 0)
                ui.ResetCapture()
                gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_UNITS, 0, tuple([SelectedObjects]))
                return
        ui.ResetCapture()
        self.ResetCallBack(None, self.LastMousePos)
        scene = gamemodule.GameManager_GetScene()
        scene.SetPickFilter(globalscript.MENTAL_OBJECT, {(globalscript.MENTAL_OBJECT): (globalscript.MT_CHAR)})
        selected = scene.PickObjectIntersection(x, y)
        if not selected:
            return
        char = selected[0].GetMentalityPart().GetPythonCharacter()
        if not char:
            return
        if not gamemodule.IsKeyPressed(globalscript.PlayerConfig.bcam['selection_modifier'][0]) or not char.Owner == overlandcharacters.Player or SelectedAlien:
            self.UnselectAllUnits(0)
        if gamemodule.IsKeyPressed(globalscript.PlayerConfig.bcam['selection_modifier'][0]) and char.Selected:
            self.UnselectUnit(char, 0)
        else:
            self.SelectUnit(char, 0)
            gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_UNITS, 0, tuple([[char]]))
        self.UpdateMS('reposition', None, 0)
        return

    def RPickDown(self, x, y, flags):
        if self.NeedToResetCapture:
            self.NeedToResetCapture = 0
            ui.ResetCapture(self.LastMousePos)
        gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_RPICKDOWN, 0, (x, y, flags))
        if self.SpellSelectedToCast != None:
            self.StartCast = 1
            self.NeedToResetCapture = 1
            ui.SetCapture(self.SpellSelectedToCast.Param['ae'], self._ResetCallBack)
        return

    def RPickUp(self, x, y, flags):
        if self.NeedToResetCapture:
            self.NeedToResetCapture = 0
            ui.ResetCapture(self.LastMousePos)
        self.StartCast = 0
        gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_RPICKUP, 0, (x, y, flags))
        return
        return

    def SpellRDown(self, x, y, flags):
        return
        return

    def SpellRUp(self, x, y, flags, send=1):
        if self.SpellSelectedToCast != None and self.StartCast != 0:
            self.SpellSelectedToCast = None
            if send:
                gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_RPICKUP, 0, (x, y, flags))
            self.StartCast = 0
            ui.ResetCapture(self.LastMousePos)
            self.CmdMoveWrap(0)
        return
        return

    def SpellRMove(self, control, x, y, flags):
        self.LastMousePos = [x, y, flags]
        if self.SpellSelectedToCast != None and self.StartCast != 0:
            gamemodule.SendMessage(globalscript.WT_MOUSE, globalscript.WM_PICKMOVE, 0, (x, y, flags))
        return
        return

    def PickMove(self, control, x, y, flags):
        self.LastMousePos = [x, y, flags]
        if self.NeedToResetCapture:
            self.NeedToResetCapture = 0
            ui.ResetCapture(self.LastMousePos)
        if self.MUSprogress == 1:
            if ui.ValidDADRect(ui.CreateRect(self.MUSstart[0], self.MUSstart[1], x, y)):
                ui.SetCapture(self.SelectionBoard['selection'].Param['ae'], self._ResetCallBack)
                self.MUS.On()
                self.MUSprogress = 2
            else:
                self.MUSprogress = 0
        if self.MUSprogress > 0:
            self.MUS.Redraw(self.MUSstart, self.MUSend)
        self.MUSend[0] = x
        self.MUSend[1] = y
        return

    def UnselectAllUnits(self, outside=1):
        global SelectedAlien
        i = len(SelectedObjects) - 1
        while i >= 0:
            SelectedObjects[0].Unselect(self.GetSelMode())
            SelectedObjects.remove(SelectedObjects[0])
            i -= 1

        self.StatDisplayChar = None
        if SelectedAlien:
            SelectedAlien.Unselect(self.GetSelMode())
            SelectedAlien = None
        self.UpdateMS('unselectall', None, outside)
        self.StatDisplayUpdate(None)
        self.HideSelectionDisplay()
        return

    def GetCharAlignment(self, char):
        enemy = 0
        if char.Owner == overlandcharacters.Player:
            enemy = 0
        else:
            enemy = 1
            if char.Owner == gamemodule.GameManager_GetScene().GetBDomainObject().RespawnsOwner:
                if char.IsAlly(overlandcharacters.Player.BattleUnit):
                    enemy = 2
                else:
                    enemy = 1
        return enemy
        return

    def SelectUnit(self, char, outside=1):
        global SelectedAlien
        if self.EnableSelection == 0:
            return
        enemy = self.GetCharAlignment(char)
        if SelectedAlien:
            SelectedAlien.Unselect(self.GetSelMode())
            SelectedAlien = None
        select = 1
        if enemy == 0:
            if not char.UnitInfo == units_base.UnitsBase['Imp'] or Select_Imp:
                SelectedObjects.append(char)
            else:
                select = 0
        elif not char.UnitInfo == units_base.UnitsBase['Imp'] or Select_Imp:
            SelectedAlien = char
        else:
            select = 0
        if select:
            char.Select(enemy, self.GetSelMode())
            self.UpdateMS('select', char, outside)
        if len(SelectedObjects) == 1:
            self.StatDisplayUpdate(SelectedObjects[0])
            self.ShowSelectionDisplay(SelectedObjects[0])
            return
        if SelectedAlien:
            self.StatDisplayUpdate(SelectedAlien)
            self.ShowSelectionDisplay(SelectedAlien)
            return
        self.StatDisplayUpdate(None)
        self.HideSelectionDisplay()
        return

    def UnselectUnit(self, char, outside=1):
        global SelectedAlien
        if char != None:
            char.Unselect(self.GetSelMode())
        if SelectedObjects.count(char):
            SelectedObjects.remove(char)
        if SelectedAlien == char:
            SelectedAlien = None
        self.UpdateMS('unselect', char, outside)
        if len(SelectedObjects) == 1:
            self.StatDisplayUpdate(SelectedObjects[0])
            self.ShowSelectionDisplay(SelectedObjects[0])
            return
        if SelectedAlien:
            self.StatDisplayUpdate(SelectedAlien)
            self.ShowSelectionDisplay(SelectedAlien)
            return
        self.StatDisplayUpdate(None)
        self.HideSelectionDisplay()
        return

    def GetCharLevel(self, char):
        bdomain = gamemodule.GameManager_GetScene().GetBDomainObject()
        if char != None:
            if bdomain.Attackers.count(char.OverlandCharacter) or bdomain.Defenders.count(char.OverlandCharacter):
                return 0
            if char.OverlandCharacter:
                return 1
        return 2
        return

    def SubSeq(self, seq1, seq2):
        res = []
        cnt = len(seq1)
        i = 0
        while i < cnt:
            if seq2.count(seq1[i]) == 0:
                res.append(seq1[i])
            i += 1

        return res
        return

    def SGSVK1(self):
        self.SaveGroupSelection(vkeys.VK_1)
        return
        return

    def SGSVK2(self):
        self.SaveGroupSelection(vkeys.VK_2)
        return
        return

    def SGSVK3(self):
        self.SaveGroupSelection(vkeys.VK_3)
        return
        return

    def SGSVK4(self):
        self.SaveGroupSelection(vkeys.VK_4)
        return
        return

    def SGSVK5(self):
        self.SaveGroupSelection(vkeys.VK_5)
        return
        return

    def SaveGroupSelection(self, key):
        if SelectedAlien:
            self.UnselectUnit(SelectedAlien, 0)
        if not self.GroupSaveMS.has_key(key):
            self.GroupSaveMS[key] = []
        show = not gamemodule.IsKeyPressed(globalscript.PlayerConfig.bcam['group_sel_mod'][0])
        if show:
            if not gamemodule.IsKeyPressed(globalscript.PlayerConfig.bcam['group_sel_add'][0]):
                unsel = self.SubSeq(SelectedObjects, self.GroupSaveMS[key])
                cnt = len(unsel)
                i = 0
                while i < cnt:
                    self.UnselectUnit(unsel[i], 0)
                    i += 1

            sel = self.SubSeq(self.GroupSaveMS[key], SelectedObjects)
            cnt = len(sel)
            i = 0
            while i < cnt:
                self.SelectUnit(sel[i], 0)
                i += 1

            self.UpdateMS('reposition', None, 0)
            gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GNT_SHOW_GROUP, 0, 0)
        else:
            self.GroupSaveMS[key] = []
            cnt = len(SelectedObjects)
            i = 0
            while i < cnt:
                self.GroupSaveMS[key].append(SelectedObjects[i])
                i += 1

            gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GNT_SAVE_GROUP, 0, 0)
        return
        return

    def UpdateHP(self, char):
        if char:
            if len(SelectedObjects) == 1 and char == SelectedObjects[0]:
                self.SelectionBoard['hp_progress'].InitProgressBar(self.GetCharAlignment(char), 0, char.GetStat('MAX_HP'), char.GetStat('HP'))
                return
            if char == SelectedAlien:
                self.SelectionBoard['hp_progress'].InitProgressBar(self.GetCharAlignment(char), 0, char.GetStat('MAX_HP'), char.GetStat('HP'))
                return
        self.SelectionBoard['hp_progress'].SetVisibility(0, 1)
        return
        return

    def UpdateMS(self, type, char, outside):
        objcnt = len(SelectedObjects)
        if not self.SelectionBoard:
            return
        box = self.SelectionBoard['selection_board']
        if not box:
            return
        if type == 'select':
            level = self.GetCharLevel(char)
            key = char.UnitInfo.KeyName
            if self.MSObjects[level].has_key(key):
                self.MSObjects[level][key][1] += 1
                cnt = self.MSObjects[level][key][1]
                obj = self.MSObjects[level][key][0]
                counter = bgui.GetControlByName(obj, 'counter' + key)
                counter.Param['ae'].SetText('MainBB', str(cnt))
                bk = bgui.GetControlByName(obj, 'back' + key)
                bk.SetControlRect(ui.CreateRect(0, 0, gui_templates.icon_back_dim, gui_templates.icon_back_dim))
            else:
                ps = 0
                i = 0
                while i <= level:
                    ps += len(self.MSObjects[i])
                    i += 1

                self.MSObjects[level][key] = []
                self.MSObjects[level][key].append(None)
                self.MSObjects[level][key].append(1)
                if self.IconsCash.has_key(key):
                    obj = self.IconsCash[key]
                    obj.SetVisibility(1, 1)
                    counter = bgui.GetControlByName(obj, 'counter' + key)
                    counter.Param['ae'].SetText('MainBB', '')
                else:
                    tm = {}
                    tm = copy.deepcopy(gui_templates.selection_icon)
                    i = 0
                    while i < 4:
                        tm[i]['name'] += key
                        if tm[i].has_key('parent'):
                            tm[i]['parent'] += key
                        i += 1

                    icon = bgui.CreateFromTemplate(tm, ModifyCallBack, [key, char.UnitInfo.BattleIcon, self._OnIconClick])
                    obj = icon['bkgrnd' + key]
                    self.IconsCash[key] = obj
                obj.SetParent(box, ps)
                self.MSObjects[level][key][0] = obj
                if outside:
                    box.RepositionScrollControls()
        if type == 'unselect':
            if outside:
                mskeys = self.GroupSaveMS.keys()
                mscnt = len(mskeys)
                i = 0
                while i < mscnt:
                    if self.GroupSaveMS[mskeys[i]].count(char) > 0:
                        self.GroupSaveMS[mskeys[i]].remove(char)
                    i += 1

            level = self.GetCharLevel(char)
            key = char.UnitInfo.KeyName
            if self.MSObjects[level].has_key(key):
                self.MSObjects[level][key][1] -= 1
                cnt = self.MSObjects[level][key][1]
                obj = self.MSObjects[level][key][0]
                if cnt == 0:
                    obj.SetVisibility(0, 1)
                    rt = obj.GetControlRect()
                    ui.OffsetRect(rt, -100, 100)
                    obj.SetControlRect(rt)
                    obj.SetParent(None)
                    del self.MSObjects[level][key]
                    if outside:
                        box.RepositionScrollControls()
                else:
                    txt = ''
                    vs = 0
                    if cnt > 1:
                        txt = str(cnt)
                        vs = 1
                    counter = bgui.GetControlByName(obj, 'counter' + key)
                    counter.Param['ae'].SetText('MainBB', txt)
                    bk = bgui.GetControlByName(obj, 'back' + key)
                    bk.SetControlRect(ui.CreateRect(0, 0, 0, 0))
        if type == 'unselectall':
            i = 0
            dc = []
            while i < len(box.Controls):
                dc.append(box.Controls[i])
                i += 1

            i = 0
            while i < len(dc):
                dc[i].SetParent(None)
                dc[i].SetVisibility(0, 1)
                i += 1

            del dc
            if outside:
                self.GroupSaveMS = {}
            self.MSObjects = {}
            i = 0
            while i < self.LevelCount:
                self.MSObjects[i] = {}
                i += 1

        if type == 'reposition':
            box.RepositionScrollControls()
        if objcnt < 2:
            obj = SelectedAlien
            if not SelectedAlien and objcnt == 1:
                obj = SelectedObjects[0]
            self.UpdateHP(obj)
            if obj:
                self.SelectionBoard['hp_progress'].SetVisibility(1, 1)
        else:
            self.SelectionBoard['hp_progress'].SetVisibility(0, 1)
        return
        return

    def EnableCamera(self, bEnable):
        gamemodule.GameManager_GetScene().GetBDomainObject().Camera.EnableCamera(bEnable)
        return
        return

    def OnIconClick(self, control, x, y, flags, param):
        ui.ResetCapture(self.LastMousePos)
        self.ResetCallBack(None, [0, 0, 0])
        i = 0
        shift = gamemodule.IsKeyPressed(globalscript.PlayerConfig.bcam['selection_modifier'][0])
        while i < len(SelectedObjects):
            char = SelectedObjects[i]
            if char.UnitInfo.KeyName == param:
                if shift:
                    self.UnselectUnit(char, 0)
                    continue
            elif not shift:
                self.UnselectUnit(char, 0)
                continue
            i += 1

        self.UpdateMS('reposition', None, 0)
        return
        return

    def ShadowSpell_add(self, spell):
        ui.ResetCapture(self.LastMousePos)
        self.ShadowSpell_spell = spell
        tmp = self.ShadowSpell_spell.CatchedSpell.CSpellRef
        tmp = self.CreateButton(845, 585, 32, 32, self.ShadowSpell_cast, tmp.IconName, tmp.CastName, tmp, 4, tmp.GetFullTooltip())
        self.buttons.remove(tmp)
        self.KnownSpells.append(tmp)
        self.ShadowSpell_button = tmp
        self.ShadowSpell_name = self.ShadowSpell_spell.CatchedSpell.CSpellRef.KeyName
        self.ShadowSpell_lvl = self.ShadowSpell_spell.CatchedSpell.SpellLevel
        return

    def ShadowSpell_remove(self, called_from_spell=0):
        ui.ResetCapture(self.LastMousePos)
        if self.KnownSpells.count(self.ShadowSpell_button):
            self.KnownSpells.remove(self.ShadowSpell_button)
            self.ShadowSpell_button.Dest()
        self.ShadowSpell_button = None
        if self.ShadowSpell_spell and called_from_spell == 0:
            self.ShadowSpell_spell.Uncast()
        self.ShadowSpell_spell = None
        return

    def ShadowSpell_cast(self):
        ui.ResetCapture(self.LastMousePos)
        self.ShadowSpell_casting = 1
        if self.ShadowSpell_spell and self.ShadowSpell_name:
            overlandcharacters.Player.CastObject_Name(self.ShadowSpell_name, 1, self.ShadowSpell_lvl)
        return

    def SelectButton(self, btn, x, y, flags, reset=1):
        if reset:
            ui.ResetCapture(self.LastMousePos)
        if self.ButtonSelection:
            self.ButtonSelection.HoverLock(0)
        if self.SpellSelection:
            self.SpellSelection.Dest()
            self.SpellSelection = None
        self.ButtonSelection = btn
        if self.ButtonSelection == None:
            return
        if not self._Paused:
            self.ButtonSelection.HoverLock(1)
        return

    def UnselectAllSpells(self):
        spells_parent = self.SelectionBoard['spells_border']
        i = 0
        cnt = len(spells_parent.Controls)
        while i < cnt:
            spells_parent.Controls[i].SetState(0)
            i += 1

        self.SpellSelectedToCast = None
        self.StartCast = 0
        return
        return

    def SelectSpell(self, icon, x, y, flags):
        if self.SpellSelectedToCast != None and self.StartCast == 1:
            return
        self.SelectMove = 0
        self.CmdMoveWrap()
        bgc = icon.GraphControl
        bgc.SetState(1)
        self.SpellSelectedToCast = bgc
        self.SelectMove = 0
        bgc.Param['castaction']()
        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple([bgc.Param['spell'].GetKeyName()]))
        return
        return

    def UnselectSpell(self, casted_or_breaked=0):
        self.UnselectAllSpells()
        return
        return

    def ShowSelectionDisplay(self, unit):
        if unit.UnitInfo.BattleIcon != '':
            mat = gamemodule.LoadMaterial(unit.UnitInfo.BattleIcon)
            self.SDico.SetMaterial('MainBB', mat)
            self.SDico.ChangeMaterialSet('MainBB', globalscript.BBlend_Blend)
            self.SDico.SetVisible('MainBB', 1)
            self.SDbkg.SetVisible('MainBB', 1)
        self.SDnbk.SetVisible('MainBB', 1)
        self.SDname.SetText('MainBB', unit.Name + ' ' + unit.AdvName)
        self.SDname.SetVisible('MainBB', 1)
        i = len(self.SDspells) - 1
        while i >= 0:
            self.SDspells[0].Dest()
            self.SDspells.remove(self.SDspells[0])
            i -= 1

        self.SDspells = []
        ico_x = 8 + 64 + 10
        ico_y = 5 + 32 + 2
        iconspacing = 3
        i = len(unit.ActiveSpells) - 1
        while i >= 0:
            cast = unit.ActiveSpells[i].CSpellRef
            if cast and cast.UtiliteSpell == 0 and unit.ActiveSpells[i].NoDisplay == 0:
                btns = self.CreateButton(ico_x, ico_y, 32, 32, self.Dispell, cast.IconName, cast.CastName, cast, 3)
                btns[0].SpellName = unit.ActiveSpells[i].CSpellRef.GetKeyName()
                btns[1].SpellName = unit.ActiveSpells[i].CSpellRef.GetKeyName()
                if unit.ActiveSpells[i].CasterCharacter.OverlandCharacter == overlandcharacters.Player:
                    btns[0].AddActionFunction('LBDown', self.Dispell, tuple([unit.ActiveSpells[i]]))
                else:
                    btns[1].SetColor('MainBB', 1, 0, 0, 1)
                self.SDspells.append(btns[0])
                self.SDspells.append(btns[1])
                ico_x += 32 + iconspacing
            i -= 1

        return

    def Dispell(self, btn, x, y, flags, param):
        ui.ResetCapture(self.LastMousePos)
        param.Uncast()
        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['dispell_' + btn.SpellName]))
        return

    def HideSelectionDisplay(self):
        self.SDico.SetVisible('MainBB', 0)
        self.SDbkg.SetVisible('MainBB', 0)
        self.SDname.SetVisible('MainBB', 0)
        self.SDnbk.SetVisible('MainBB', 0)
        i = len(self.SDspells) - 1
        while i >= 0:
            self.SDspells[0].Dest()
            self.SDspells.remove(self.SDspells[0])
            i -= 1

        return

    def StatDisplayUpdate(self, char):
        self.StatDisplayChar = char
        if char == None:
            self.StatDisplay.SetVisible('MainBB', 0)
            self.StatDisplayBk.SetVisible('MainBB', 0)
            return
        self.StatDisplay.SetVisible('MainBB', 1)
        self.StatDisplay.SetText('MainBB', dict.BATTLE_STAT_HP + ': ' + str(int(char.GetStat('HP'))) + '/' + str(int(char.GetStat('MAX_HP'))) + '|' + dict.BATTLE_STAT_ATT + ': ' + str(int(char.GetStat('ATT'))) + '|' + dict.BATTLE_STAT_DEF + ': ' + str(int(char.GetStat('DEF'))) + '|' + dict.BATTLE_STAT_SPD + ': ' + str(int(char.GetStat('SPD'))) + '|' + dict.BATTLE_STAT_MST + ': ' + str(int(char.GetStat('MST'))) + '|' + dict.BATTLE_STAT_SKL + ': ' + str(int(char.GetStat('SKL'))) + '|' + dict.BATTLE_STAT_RES + ': ' + str(int(char.GetStat('RES'))))
        self.StatDisplayBk.SetVisible('MainBB', 1)
        return

    def FreezeUI(self):
        spells_parent = self.SelectionBoard['spells_border']
        i = 0
        cnt = len(spells_parent.Controls)
        while i < cnt:
            spells_parent.Controls[i].Param['ae'].HoverLock(1, [0.5, 0.5, 0.5, 1])
            spells_parent.Controls[i].Param['ae'].Disable(1)
            i += 1

        btn = self.SelectionBoard['btn_disband']
        btn.Param['ae'].HoverLock(1, [0.5, 0.5, 0.5, 0.3])
        btn.Param['ae'].Disable(1)
        return
        return

    def UnfreezeUI(self):
        spells_parent = self.SelectionBoard['spells_border']
        i = 0
        cnt = len(spells_parent.Controls)
        while i < cnt:
            spells_parent.Controls[i].Param['ae'].Disable(0)
            spells_parent.Controls[i].Param['ae'].HoverLock(0)
            i += 1

        btn = self.SelectionBoard['btn_disband']
        btn.Param['ae'].Disable(0)
        btn.Param['ae'].HoverLock(0)
        return
        return

    def SetStatusText(self, text, type, time):
        if self.statustexttimer != None:
            gamemodule.KillTimer(self.statustexttimer)
        self.statustexttimer = gamemodule.SetTimer(time, self.pNoStatusText)
        self.statustext.SetText('MainBB', text)
        c = [1, 1, 1, 1]
        if BHUD_STColors.has_key(type):
            c = BHUD_STColors[type]
        self.statustext.SetColor('MainBB', c[0], c[1], c[2], c[3])
        self.statustextbkg.SetVisible('MainBB', 1)
        self.statustext.SetVisible('MainBB', 1)
        return

    def NoStatusText(self, tID):
        gamemodule.KillTimer(tID)
        self.statustextbkg.SetVisible('MainBB', 0)
        self.statustext.SetVisible('MainBB', 0)
        return

    def DM_UpdateFunc(self, TimePassed):
        self.DM_Pane.SetText('MainBB', 'Def: ' + str(int(self.DM_DefDomain.BattleMana)) + '/' + str(int(self.DM_DefDomain.BattleMaxMana)) + ' Up:' + str(int(self.DM_DefDomain.BattleUpkeep)) + '/' + str(int(self.DM_DefDomain.MagicPower)) + '|Att: ' + str(int(self.DM_AttDomain.BattleMana)) + '/' + str(int(self.DM_AttDomain.BattleMaxMana)) + ' Up:' + str(int(self.DM_AttDomain.BattleUpkeep)) + '/' + str(int(self.DM_AttDomain.MagicPower)))
        return

    def h_Toggle_HUD(self, stuff=0):
        if self.cg_drawHUD:
            self.cg_drawHUD = 0
        else:
            self.cg_drawHUD = 1
        gamemodule.r_billboards(self.cg_drawHUD)
        return

    def __init__(self):
        _plog('BM-INIT')
        import operator
        self.ForcePause = 3
        self.FirstSimpleUpdate = 0
        self.EnableSelection = 1
        self.cg_toggle_hud_key = vkeys.VK_0
        self.cg_drawHUD = 1
        self._h_Toggle_HUD = self.h_Toggle_HUD
        gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._h_Toggle_HUD, self.cg_toggle_hud_key)
        self.ShadowSpell_spell = None
        self.ShadowSpell_button = None
        self.ShadowSpell_casting = 0
        self.ShadowSpell_name = None
        self.ShadowSpell_lvl = 0
        if g_defs.dev_draw_char_info:
            self.DM_pUpdateFunc = self.DM_UpdateFunc
            gamemodule.AddMessageHandler(globalscript.GT_GENERAL, globalscript.GM_SIMPLEUPDATE, self.DM_pUpdateFunc, 0)
            self.DM_DefDomain = gamemodule.GameManager_GetScene().GetBDomainObject().MiniDomain
            if len(gamemodule.GameManager_GetScene().GetBDomainObject().Attackers):
                self.DM_AttDomain = gamemodule.GameManager_GetScene().GetBDomainObject().Attackers[0].CurrentLocation
            else:
                self.DM_AttDomain = self.DM_DefDomain
            self.DM_Pane = ui.BActionElement([750, 0, 244, 50, ui.GUI_TNORMAL, '', 0, [7, 8, FONT_H, 3, 2, 2]], [], [])
            self.DM_Pane.SetTextAlignment('MainBB', 0)
        self.buttons = []
        _plog('BM-B0')
        self.HPDelta = 0
        self.ManaDelta = 0
        self.MUSprogress = 0
        self.MUSstart = []
        self.MUSstart.append(0)
        self.MUSstart.append(0)
        self.MUSend = []
        self.MUSend.append(0)
        self.MUSend.append(0)
        self.SpellSelection = None
        self.ButtonSelection = None
        self.StatDisplayChar = None
        try:
            self.StatDisplay = ui.BActionElement([30, 100, 300, 300, ui.GUI_TNORMAL, '', 0, [7, 8, FONT_H, 3, 2, 2]], [], [])
            self.StatDisplay.SetPrior('MainBB', gui_templates.bm_mc_zorder - 609)
            self.StatDisplay.SetTextAlignment('MainBB', 0)
            self.StatDisplay.Unsubscribe()
            self.StatDisplay.SetVisible('MainBB', 0)
            self.buttons.append(self.StatDisplay)
            self.StatDisplayBk = ui.BActionElement([20, 90, 150, 200, [0.3, 0.3, 0.3, 0.3], 'ol_mc_black.tga', globalscript.BBlend_Blend, [7, 8, FONT_H, 3, 2, 2]], [], [])
            self.StatDisplayBk.SetPrior('MainBB', gui_templates.bm_mc_zorder - 610)
            self.StatDisplayBk.SetTextAlignment('MainBB', 0)
            self.StatDisplayBk.Unsubscribe()
            self.StatDisplayBk.SetVisible('MainBB', 0)
            self.buttons.append(self.StatDisplay)
            _plog('BM-B1')
            self.SDbkg = ui.BActionElement([5, 5, 70, 70, [1, 1, 1, 1], 'bhud_uniticonrim01.tga', globalscript.BBlend_Blend, [7, 8, FONT_H, 3, 2, 2]], [], [])
            self.SDbkg.SetVisible('MainBB', 0)
            self.SDbkg.SetPrior('MainBB', gui_templates.bm_mc_zorder)
            self.buttons.append(self.SDbkg)
            self.SDico = ui.BActionElement([8, 8, 64, 64, [1, 1, 1, 1], '', globalscript.BBlend_Blend, [7, 8, FONT_H, 3, 2, 2]], [], [])
            self.SDico.SetVisible('MainBB', 0)
            self.SDico.SetPrior('MainBB', gui_templates.bm_mc_zorder + 1)
            self.buttons.append(self.SDico)
            self.SDnbk = ui.BActionElement([80, 6, 200, 28, [0, 0, 0, 0.5], 'white.bmp', globalscript.BBlend_Blend, [7, 8, FONT_H, 3, 2, 2]], [], [])
            self.SDnbk.SetVisible('MainBB', 0)
            self.SDnbk.SetPrior('MainBB', gui_templates.bm_mc_zorder)
            self.buttons.append(self.SDnbk)
        except Exception:
            _pexc('BM-B0EXC')
        self.SDname = ui.BActionElement([88, 10, 128, 32, ui.GUI_TNORMAL, '', 0, [7, 8, FONT_H, 3, 2, 2]], [], [])
        self.SDname.SetTextAlignment('MainBB', 0)
        self.SDname.SetVisible('MainBB', 0)
        self.SDname.SetPrior('MainBB', gui_templates.bm_mc_zorder + 1)
        self.buttons.append(self.SDname)
        _plog('BM-P1')
        self.SDspells = []
        self.KnownSpells = []
        _plog('BM-P2')
        # 16:9: левый BHUD-блок (underlay/gauges/spgauge/frame_l) прижат к левому краю
        # канваса -> придвигаем весь блок к левому краю экрана СДВИГОМ XLeft.
        # Все элементы блока используют XLeft, блок едет целиком; размеры движку
        # передаются как SC/SY = k-масштаб -> аспект арта = 4:3, растяжения нет.
        # При 4:3 (k>=1) HD_Shift() = 0 -> побитово родная раскладка.
        # try/except: правка не должна уметь ронять создание боевого меню.
        sh = 0
        try:
            import hd_scale
            sh = hd_scale.HD_Shift()
        except Exception:
            _pexc('XLEFT-EXC')
        XLeft = 0 - sh
        YLeft = 512
        _plog('XLEFT sh=' + repr(sh) + ' XLeft=' + repr(XLeft))
        self.HPPosX = XLeft + 0
        self.HPPosY = YLeft + 73
        self.HPw = 130
        self.HPh = 157
        self.ManaPosX = XLeft + 26
        self.ManaPosY = YLeft + 126
        self.Manaw = 157
        self.Manah = 130
        try:
            self.BHUD_underlay_l = self.CreateUISimple(XLeft, YLeft, 256, 256, 'bhud_underlay_l.tga')
            self.BHUD_underlay_l.Unsubscribe()
            self.BHUD_underlay_l.SetPrior('MainBB', LEFT_PART_PRIORITY + 1)
            self.BHUD_hgauge = self.CreateUISimple(self.HPPosX, self.HPPosY, self.HPw, self.HPh, 'bhud_hgauge.bmp', 1)
            self.BHUD_hgauge.Unsubscribe()
            self.BHUD_hgauge.SetPrior('MainBB', LEFT_PART_PRIORITY + 2)
            self.BHUD_mgauge = self.CreateUISimple(self.ManaPosX, self.ManaPosY, self.Manaw, self.Manah, 'bhud_mgauge.bmp', 1)
            self.BHUD_hgauge.Unsubscribe()
            self.BHUD_mgauge.SetPrior('MainBB', LEFT_PART_PRIORITY + 2)
            self.BHUD_spgauge = CGaugeBillboard(XLeft + 0, YLeft + 44, 212, 212, 'bhud_spgauge.bmp')
            self.BHUD_hgauge.Unsubscribe()
            self.BHUD_spgauge.Gauge.SetPrior('MainBB', LEFT_PART_PRIORITY + 2)
            self.BHUD_frame_l = self.CreateUISimple(XLeft, YLeft, 256, 256, 'bhud_frame_l.tga')
            self.BHUD_frame_l.Unsubscribe()
            self.BHUD_frame_l.SetPrior('MainBB', LEFT_PART_PRIORITY + 3)
        except Exception:
            _pexc('BHUD-EXC')
        self._Paused = 0
        self.HLLog = 0
        self.HLLogStep = 0
        self.Player = overlandcharacters.Player.BattleUnit
        self.InitIndicators(self.Player.GetStat('MAX_HP'), overlandcharacters.Player.CurrentLocation.BattleMaxMana, 0)
        self.SetSP(0)
        self._h_Update = self.h_Update
        self.statustextbkg = ui.BActionElement([100, 746, 824, 22, [0, 0, 0, 0.5], 'white.bmp', globalscript.BBlend_Blend, [7, 8, FONT_H, 3, 2, 2]], [], [])
        self.statustextbkg.SetPrior('MainBB', LEFT_PART_PRIORITY - 2)
        self.statustext = ui.BActionElement([100, 743, 824, 32, ui.GUI_TNORMAL, '', 0, [7, 8, FONT_H, 3, 2, 2]], [], [])
        self.statustext.SetPrior('MainBB', LEFT_PART_PRIORITY - 1)
        self.statustextbkg.SetVisible('MainBB', 0)
        self.statustext.SetVisible('MainBB', 0)
        self.statustext.SetTextAlignment('MainBB', 1)
        self.pNoStatusText = self.NoStatusText
        self.statustexttimer = None
        self.sounds = bhud_sounds.BHUD_Sounds()
        self.MUS = cursor.BHUD_MultipleSelection()
        self.LastMousePos = [0, 0, 0]
        self.StartCast = 0
        self._OnChangePos = self.OnChangePos
        self._OnMMClick = self.OnMMClick
        self._LPickDown = self.LPickDown
        self._LPickUp = self.LPickUp
        self._PickMove = self.PickMove
        self._RPickDown = self.RPickDown
        self._RPickUp = self.RPickUp
        self._ResetCallBack = self.ResetCallBack
        self._CmdDisbandWrap = self.CmdDisbandWrap
        self._CmdRetreatWrap = self.CmdRetreatWrap
        self._CmdPauseWrap = self.CmdPauseWrap
        self._CmdDefAttWrap = self.CmdDefAttWrap
        self._CmdMoveWrap = self.CmdMoveWrap
        self._CmdConsole = self.CmdConsole
        self._CmdMMHighLight = self.CmdMMHighLight
        self._CmdMMShowUnits = self.CmdMMShowUnits
        self._CmdSelMode = self.CmdSelMode
        self._CmdHKSpell = self.CmdHKSpell
        self._CmdHKSpellCast = self.CmdHKSpellCast
        self._OnOpenInfo = self.OnOpenInfo
        self._Update = self.Update
        self.ConcoleStep = 60
        self.CurrentStep = 0
        self.MinConcoleWidth = gui_templates.bm_mc_min_width
        self.MessHeight = 50
        self.MessCount = 0
        self.NeedToResetCapture = 0
        self.DomCenterRadius = self.CalcDomCenterRadius(gamemodule.GameManager_GetScene().GetBDomainObject())
        try:
            self.SelectionBoard = bgui.CreateFromTemplate(gui_templates.selection_template, ModifyCallBackSel, [gamemodule.GameManager_GetScene().GetBDomainObject().MiniMapTexture.GetName(), self])
            _plog('SELB-OK')
        except Exception:
            _pexc('SELB-EXC')
        self.SelectionBoard['btn_minimap_hl'].SetState(globalscript.PlayerConfig.R_HIDE_MINIMAP + 1)
        self.UpdateMMHL()
        self.SelectionBoard['btn_minimap_units'].SetState(globalscript.PlayerConfig.R_ALL_UNITS + 1)
        self.UpdateMMUnits()
        self.CurrentStatusTime = 0
        self.ShowStatusText(0)
        self.SelectionBoard['btn_selection_mode'].SetState(globalscript.PlayerConfig.R_ADV_SELECTION + 1)
        self.UpdateSelectionMode()
        self.SelectionBoard['pause_mark'].SetVisibility(0, 1)
        gamemodule.GameManager_GetScene().GetBDomainObject().MiniMapTexture = None
        self.SelectionBoard['selection'].SetControlRect(ui.GetScreenRect())
        self._OnIconClick = self.OnIconClick
        self.LevelCount = 3
        self.MSObjects = {}
        i = 0
        while i < self.LevelCount:
            self.MSObjects[i] = {}
            i += 1

        self.GroupSaveMS = {}
        self.SelectionBoard['hp_progress'].SetVisibility(0, 1)
        self.IconsCash = {}
        self.Attacker = 1
        self.SelectMove = 0
        self.CmdDefAttWrap()
        self._SelectSpell = self.SelectSpell
        self._SpellRDown = self.SpellRDown
        self._SpellRUp = self.SpellRUp
        self._SpellRMove = self.SpellRMove
        self.SpellSelectedToCast = None
        self._UncastSpell = self.UncastSpell
        spells_parent = self.SelectionBoard['spells_border']
        localspells = overlandcharacters.Player.GetKnownBattleSpells()
        numberofspells = len(localspells)
        numberofhudlines = operator.div(numberofspells, 12) + 1
        iconspacing = 35
        sp_y = 714
        y_i = numberofhudlines - 1
        n = 0
        while y_i >= 0:
            x_i = 0
            spellsinline = numberofspells - y_i * 12
            if spellsinline > 12:
                spellsinline = 12
            sp_x = 512 - spellsinline * iconspacing / 2.0
            while x_i < spellsinline:
                if n >= len(localspells):
                    break
                tmp = localspells[n]
                spell_c = self.CreateButton(sp_x + x_i * iconspacing, sp_y - y_i * iconspacing, 32, 32, tmp.GetBattleFunction(), tmp.GetIconName(), tmp.GetCastName(), tmp, 1, tmp.ParentSpell.GetFullTooltip())
                spell_c.SetParent(spells_parent)
                n += 1
                x_i += 1

            y_i -= 1
            if spellsinline == 0:
                spellsinline = 1

        self.UpdateSpellsHK(0)
        self.MiniMapRTCount = 0
        self.AddElemToMM(MM_DOMCENTER, gamemodule.GameManager_GetScene().GetBDomainObject())
        self.AddElemToMM(MM_CAMERA, gamemodule.GameManager_GetScene().GetBDomainObject().Camera)
        groups = gamemodule.GameManager_GetScene().GetBDomainObject().OpponentGroups
        groups_ks = groups.keys()
        gcnt = len(groups_ks)
        gi = 0
        while gi < gcnt:
            if overlandcharacters.Player.GroupID == groups_ks[gi]:
                pcnt = len(groups[groups_ks[gi]])
                pi = 0
                while pi < pcnt:
                    if groups[groups_ks[gi]][pi] != gamemodule.GameManager_GetScene().GetBDomainObject().RespawnsOwner:
                        self.AddElemToMM(MM_PLAYER, groups[groups_ks[gi]][pi])
                    pi += 1

            pcnt = len(groups[groups_ks[gi]])
            pi = 0
            while pi < pcnt:
                if groups[groups_ks[gi]][pi] != gamemodule.GameManager_GetScene().GetBDomainObject().RespawnsOwner:
                    self.AddElemToMM(MM_OPPONENT, groups[groups_ks[gi]][pi])
                pi += 1

            gi += 1

        _plog('BM-P4')
        self.Subscribe()
        self.HotKeys()
        return
        return

    def AddZoneSpell(self, sfb_spell):
        spell = sfb_spell.ParentBaseSpell
        spells_parent = self.SelectionBoard['spells_border_zone']
        spell_c = self.CreateButton(0, 0, 32, 32, sfb_spell.Uncast, spell.GetIconName(), dict.TT_Battle_Uncast + ' ' + spell.GetCastName(), sfb_spell, 1, spell.ParentSpell.GetFullTooltip(), LEFT_PART_PRIORITY + 5, 'uncast' + spell.GetKeyName())
        spell_c.SetParent(spells_parent)
        self.ReposZoneSpells()
        return
        return

    def RemoveZoneSpell(self, sfb_spell):
        spell = sfb_spell.ParentBaseSpell
        spells_parent = self.SelectionBoard['spells_border_zone']
        cnt = len(spells_parent.Controls)
        i = 0
        while i < cnt:
            if spells_parent.Controls[i].Param['spell'] == sfb_spell:
                spells_parent.Controls[i].Dest()
                self.ReposZoneSpells()
                gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['dispell_' + spell.GetKeyName()]))
                return
            i += 1

        return
        return

    def ReposZoneSpells(self):
        spells_parent = self.SelectionBoard['spells_border_zone']
        rt = spells_parent.GetControlRect()
        right = rt['right']
        bottom = rt['bottom']
        cnt = len(spells_parent.Controls)
        i = 0
        while i < cnt:
            crt = ui.CreateRect(right - 32, bottom - 32, right, bottom)
            spells_parent.Controls[i].SetControlRect(crt)
            if bottom - 32 < rt['top']:
                bottom = rt['bottom']
                right -= 35
            else:
                bottom -= 35
            i += 1

        return
        return

    def UncastSpell(self, icon, x, y, flags):
        bgc = icon.GraphControl
        if bgc.Param['castaction'] != None:
            bgc.Param['castaction']()
        return
        return

    def CalcDomCenterRadius(self, domain):
        dp = [0, 0, 0]
        if len(domain.DomShields):
            dp = [domain.DomShields[0][0], domain.DomShields[0][1], domain.DomShields[0][2]]
        else:
            dp = [domain.DomCenter[0][0], domain.DomCenter[0][1], domain.DomCenter[0][2]]
        radiuss = [4, 5, 6, 7, 8, 9, 10]
        radius = radiuss[domain.MiniDomain.DomainSize]
        dp.append(radius)
        dp.append(domain.DomRadius)
        return dp
        return

    def CalcMMRect(self, b_rt, elem_pos, elem_sz, angle, dom_param, dom_w, dom_h):
        import math
        x1 = (elem_pos[0] - dom_param[0]) / dom_param[3] * (dom_w / 2)
        y1 = (elem_pos[2] - dom_param[2]) / dom_param[3] * (dom_h / 2)
        if angle > 1000 or angle < -1000:
            angle = 0
        x = x1 * math.cos(angle) + y1 * math.sin(angle) + b_rt['left'] + dom_w / 2
        y = -x1 * math.sin(angle) + y1 * math.cos(angle) + b_rt['top'] + dom_h / 2
        rt = ui.CreateRect(x, y, x + elem_sz[0], y + elem_sz[1])
        ui.OffsetRect(rt, -elem_sz[0] / 2, -elem_sz[1] / 2)
        return rt
        return

    def MapMMPoint(self, b_rt, mx, my, angle, dom_param, dom_w, dom_h):
        import math
        mx -= b_rt['left'] + dom_w / 2
        my -= b_rt['top'] + dom_h / 2
        x1 = mx * math.cos(angle) + my * math.sin(angle)
        y1 = -mx * math.sin(angle) + my * math.cos(angle)
        xx = x1 / (dom_w / 2) * dom_param[3]
        yy = y1 / (dom_h / 2) * dom_param[3]
        if xx * xx + yy * yy > dom_param[4] * dom_param[4]:
            return []
        xx += dom_param[0]
        yy += dom_param[2]
        return [xx, yy]
        return

    def GetCameraDirectionAngle(self):
        import math
        bdo = gamemodule.GameManager_GetScene().GetBDomainObject()
        if bdo != None:
            angle = bdo.Camera.GetDirectionAngle()
            angle = math.pi / 2.0 - angle
            return angle
        return 0.0
        return

    def OnChangePos(self, ae, TimePassed):
        import math
        if not overlandcharacters.Player.BattleUnit.MentalityObject:
            return
        angle = self.GetCameraDirectionAngle()
        ae.RotateBillBoard('MainBB', angle)
        control = ae.GraphControl
        rt = control.GetControlRect()
        w = gui_templates.mini_map_width
        h = gui_templates.mini_map_height
        i = 0
        e_cnt = len(control.Controls)
        rt = control.GetControlRect()
        angle *= -1
        while i < e_cnt:
            obj = control.Controls[i]
            nm = obj.Param['name'][0:MM_NAMES_LEN]
            rt_unit = obj.Param['mm_param']
            if nm == MM_DOMCENTER:
                obj.SetVisibility(globalscript.PlayerConfig.minimap[MM_DOMCENTER])
                obj.SetControlRect(self.CalcMMRect(rt, rt_unit.DomCenter[0], gui_templates.MM_DOMCENTER_SZ, angle, self.DomCenterRadius, w, h))
                i += 1
                continue
            if nm == MM_CAMERA:
                obj.SetVisibility(globalscript.PlayerConfig.minimap[MM_CAMERA])
                obj.SetControlRect(self.CalcMMRect(rt, rt_unit.GetPos(), gui_templates.MM_CAMERA_SZ, angle, self.DomCenterRadius, w, h))
                i += 1
                continue
            if nm == MM_PLAYER:
                obj.SetVisibility(globalscript.PlayerConfig.minimap[MM_PLAYER])
                if rt_unit.BattleUnit.MentalityObject and rt_unit.BattleUnit.MentalityObject.GetGraphObject().IsInFogOfWar():
                    obj.SetVisibility(0)
                obj.SetControlRect(self.CalcMMRect(rt, rt_unit.BattleUnit.GetPosition(), gui_templates.MM_PLAYER_SZ, angle, self.DomCenterRadius, w, h))
                i += 1
                continue
            if nm == MM_PLAYER_UNIT:
                obj.SetVisibility(globalscript.PlayerConfig.minimap[MM_PLAYER_UNIT])
                if rt_unit.MentalityObject and rt_unit.MentalityObject.GetGraphObject().IsInFogOfWar():
                    obj.SetVisibility(0)
                if rt_unit.MentalityObject:
                    obj.SetControlRect(self.CalcMMRect(rt, rt_unit.MentalityObject.GetPosition(), gui_templates.MM_PLAYER_UNIT_SZ, angle, self.DomCenterRadius, w, h))
                i += 1
                continue
            if nm == MM_OPPONENT:
                obj.SetVisibility(globalscript.PlayerConfig.minimap[MM_OPPONENT])
                if rt_unit.BattleUnit.MentalityObject.GetGraphObject().IsInFogOfWar():
                    obj.SetVisibility(0)
                obj.SetControlRect(self.CalcMMRect(rt, rt_unit.BattleUnit.GetPosition(), gui_templates.MM_OPPONENT_SZ, angle, self.DomCenterRadius, w, h))
                i += 1
                continue
            if nm == MM_OPPONENT_UNIT:
                obj.SetVisibility(globalscript.PlayerConfig.minimap[MM_OPPONENT_UNIT])
                if rt_unit.MentalityObject and rt_unit.MentalityObject.GetGraphObject().IsInFogOfWar():
                    obj.SetVisibility(0)
                if rt_unit.MentalityObject:
                    obj.SetControlRect(self.CalcMMRect(rt, rt_unit.MentalityObject.GetPosition(), gui_templates.MM_OPPONENT_UNIT_SZ, angle, self.DomCenterRadius, w, h))
                i += 1
                continue
            if nm == MM_RESPAWN:
                obj.SetVisibility(globalscript.PlayerConfig.minimap[MM_RESPAWN])
                if rt_unit.MentalityObject:
                    obj.SetControlRect(self.CalcMMRect(rt, rt_unit.MentalityObject.GetPosition(), gui_templates.MM_RESPAWN_SZ, angle, self.DomCenterRadius, w, h))
                i += 1
                continue
            if nm == MM_MILITIA:
                obj.SetVisibility(globalscript.PlayerConfig.minimap[MM_MILITIA])
                if rt_unit.MentalityObject and rt_unit.MentalityObject.GetGraphObject().IsInFogOfWar():
                    obj.SetVisibility(0)
                if rt_unit.MentalityObject:
                    obj.SetControlRect(self.CalcMMRect(rt, rt_unit.MentalityObject.GetPosition(), gui_templates.MM_MILITIA_SZ, angle, self.DomCenterRadius, w, h))
                i += 1
                continue
            i += 1

        return
        return

    def CalcMMCoord(self, x, y, control=None):
        if control == None:
            control = self.SelectionBoard['map']
        angle = self.GetCameraDirectionAngle()
        rt = control.GetControlRect()
        w = gui_templates.mini_map_width
        h = gui_templates.mini_map_height
        return self.MapMMPoint(rt, x, y, angle, self.DomCenterRadius, w, h)
        return

    def OnMMClick(self, ae, x, y, flags):
        ui.ResetCapture(self.LastMousePos)
        xz = self.CalcMMCoord(x, y, ae.GraphControl)
        if len(xz) > 0:
            gamemodule.GameManager_GetScene().GetBDomainObject().Camera.B_SetPosXZ([xz[0], 0, xz[1]])
        return
        return

    def CreateMMElem(self, name, color, parent, order, tex, blend, param):
        elem = copy.deepcopy(gui_templates.minimap_elem)
        elem[0]['name'] = name
        elem[0]['billboardmain'][4] = color
        elem[0]['billboardmain'][5] = tex
        elem[0]['billboardmain'][6] = blend
        brd = bgui.CreateFromTemplate(elem)
        brd[name].Param['mm_param'] = param
        brd[name].SetParent(parent)
        brd[name].Param['ae'].SetPrior('MainBB', order)
        return
        return

    def AddElemToMM(self, type, rt_unit):
        mm = self.SelectionBoard['map']
        if type == MM_DOMCENTER:
            self.CreateMMElem(type, ui.BGUI_DOMCENTER, mm, gui_templates.MM_DOMCENTER_ORDER, gui_templates.MM_DOMCENTER_TEX, gui_templates.MM_DOMCENTER_BLEND, rt_unit)
            return
        if type == MM_CAMERA:
            self.CreateMMElem(type, ui.BGUI_CAMERA, mm, gui_templates.MM_CAMERA_ORDER, gui_templates.MM_CAMERA_TEX, gui_templates.MM_CAMERA_BLEND, rt_unit)
            return
        if type == MM_PLAYER:
            self.CreateMMElem(type, ui.BGUI_PLAYER, mm, gui_templates.MM_PLAYER_ORDER, gui_templates.MM_PLAYER_TEX, gui_templates.MM_PLAYER_BLEND, rt_unit)
            return
        if type == MM_PLAYER_UNIT:
            self.MiniMapRTCount += 1
            self.CreateMMElem(type + str(self.MiniMapRTCount), ui.BGUI_PLAYER_UNIT, mm, gui_templates.MM_PLAYER_UNIT_ORDER, gui_templates.MM_PLAYER_UNIT_TEX, gui_templates.MM_PLAYER_UNIT_BLEND, rt_unit)
            return
        if type == MM_OPPONENT:
            self.MiniMapRTCount += 1
            self.CreateMMElem(type + str(self.MiniMapRTCount), ui.BGUI_OPPONENT, mm, gui_templates.MM_OPPONENT_ORDER, gui_templates.MM_OPPONENT_TEX, gui_templates.MM_OPPONENT_BLEND, rt_unit)
            return
        if type == MM_OPPONENT_UNIT:
            self.MiniMapRTCount += 1
            self.CreateMMElem(type + str(self.MiniMapRTCount), ui.BGUI_OPPONENT_UNIT, mm, gui_templates.MM_OPPONENT_UNIT_ORDER, gui_templates.MM_OPPONENT_UNIT_TEX, gui_templates.MM_OPPONENT_UNIT_BLEND, rt_unit)
            return
        if type == MM_RESPAWN:
            self.MiniMapRTCount += 1
            self.CreateMMElem(type + str(self.MiniMapRTCount), ui.BGUI_RESPAWN, mm, gui_templates.MM_RESPAWN_ORDER, gui_templates.MM_RESPAWN_TEX, gui_templates.MM_RESPAWN_BLEND, rt_unit)
            return
        if type == MM_MILITIA:
            self.MiniMapRTCount += 1
            self.CreateMMElem(type + str(self.MiniMapRTCount), ui.BGUI_MILITIA, mm, gui_templates.MM_MILITIA_ORDER, gui_templates.MM_MILITIA_TEX, gui_templates.MM_MILITIA_BLEND, rt_unit)
            return
        return
        return

    def RemoveElemFromMM(self, rt_unit):
        mm = self.SelectionBoard['map']
        i = 0
        e_cnt = len(mm.Controls)
        while i < e_cnt:
            obj = mm.Controls[i]
            unt = obj.Param['mm_param']
            if rt_unit == unt:
                obj.SetParent(None)
                obj.Dest()
                return
            i += 1

        return
        return

    def SetMMElemColor(self, rt_unit, color):
        mm = self.SelectionBoard['map']
        i = 0
        e_cnt = len(mm.Controls)
        while i < e_cnt:
            obj = mm.Controls[i]
            unt = obj.Param['mm_param']
            if rt_unit == unt:
                obj.Param['ae'].SetColor('MainBB', color[0], color[1], color[2], color[3])
                return
            i += 1

        return
        return

    def OnOpenInfo(self, reset=1):
        self.HLLog = 0
        self.HLLogStep = 0
        obj = self.SelectionBoard['mc_pullbtn']
        obj.Controls[0].Param['ae'].HoverLock(0)
        obj.Controls[1].Param['ae'].HoverLock(0)
        st = obj.GetState()
        self.CurrentStep = self.ConcoleStep * (1 - 2 * (st - 1))
        if st >= 2:
            st = 1
        else:
            st += 1
        obj.SetState(st)
        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['mc_pullbtn']))
        return
        return

    def AddMessageToLog(self, text, level, in_status=0, in_status_delay=STATUS_TEXT_DELAY_TIME):
        parent = self.SelectionBoard['mc_central']
        tmp = copy.deepcopy(gui_templates.bm_log_elem)
        if len(parent.Controls) <= 0:
            level = 0
        tmp[0]['name'] += str(len(parent.Controls))
        nm = tmp[0]['name']
        tmp[0]['billboardmain'][7][0] = text
        if level == 0:
            tmp[0]['billboardmain'][4] = ui.BGUI_MESSAGE_LEVEL0
        if level == 1:
            tmp[0]['billboardmain'][4] = ui.BGUI_MESSAGE_LEVEL1
        elem = bgui.CreateFromTemplate(tmp, ModifyCallBackLog, [nm])
        elem[nm].SetParent(parent)
        parent.ShowBottomIndex()
        if level == 1:
            obj = self.SelectionBoard['mc_pullbtn']
            st = obj.GetState()
            if st == 1:
                self.OnOpenInfo()
        if level == 0:
            self.HLLog = 1
            self.HLLogStep = 0
        if in_status == 1:
            self.ShowStatusText(1, text)
        return
        return

    def ShowStatusText(self, show, txt='', in_status_delay=STATUS_TEXT_DELAY_TIME):
        self.SelectionBoard['status_text'].Param['ae'].SetText('MainBB', txt)
        self.SelectionBoard['status_text'].Param['ae'].SetTextAlignment('MainBB', 1)
        self.SelectionBoard['status_text_bk'].SetVisibility(show, 1)
        if show:
            self.CurrentStatusTime = in_status_delay
        return
        return

    def Update(self, a, TimePassed):
        global BHUD_Redirect
        bh_keys = BHUD_Redirect.keys()
        hi = len(bh_keys) - 1
        while hi >= 0:
            if BHUD_Redirect[bh_keys[hi]][2] >= 0:
                BHUD_Redirect[bh_keys[hi]][2] += TimePassed
                if BHUD_Redirect[bh_keys[hi]][2] > BHUD_Redirect[bh_keys[hi]][3]:
                    BHUD_Redirect[bh_keys[hi]][2] = -1
            hi -= 1

        if self.CurrentStatusTime > 0:
            self.CurrentStatusTime -= TimePassed
        else:
            self.ShowStatusText(0)
        if self.ForcePause:
            self.ForcePause -= 1
            if self.ForcePause == 0 and gamemodule.GamePaused() == 0:
                self.CmdPauseWrap()
        if self.HLLog == 1:
            obj = self.SelectionBoard['mc_pullbtn']
            st = obj.GetState()
            if st == 1:
                self.HLLogStep += 1
                if self.HLLogStep % HL_LOG_PAUSE == 0:
                    lk = obj.Controls[0].Param['ae'].bHoverLock
                    obj.Controls[0].Param['ae'].HoverLock(1 - lk)
            else:
                self.HLLog = 0
                self.HLLogStep = 0
        if self.CurrentStep != 0:
            obj = self.SelectionBoard['console']
            rect = obj.GetControlRect()
            ui.OffsetRect(rect, self.CurrentStep, 0)
            sr = ui.GetScreenRect()
            if rect['left'] >= 0:
                ui.OffsetRect(rect, -rect['left'], 0)
                self.CurrentStep = 0
            dl = rect['right'] - self.MinConcoleWidth
            if dl <= 0:
                ui.OffsetRect(rect, -dl, 0)
                self.CurrentStep = 0
            obj.SetControlRect(rect)
        return
        return

    def CmdDisbandWrap(self):
        ui.ResetCapture(self.LastMousePos)
        i = 0
        while i < len(SelectedObjects):
            if SelectedObjects[i].OverlandCharacter == overlandcharacters.Player:
                i += 1
            else:
                SelectedObjects[i].Unsummon()

        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['btn_disband']))
        return
        return

    def CmdRetreatWrap(self):
        ui.ResetCapture(self.LastMousePos)
        ConfirmLooseBattleDialog()
        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['btn_retreat']))
        return
        return

    def CmdPauseWrap(self):
        self.SelectButton(None, 0, 0, 0, 0)
        lk = self.SelectionBoard['btn_pause'].Param['ae'].bHoverLock
        self.SelectionBoard['btn_pause'].Param['ae'].HoverLock(1 - lk)
        gamemodule.PauseGame(1 - lk)
        pause_state = gamemodule.GamePaused()
        if g_defs.g_show_pause_mark:
            self.SelectionBoard['pause_mark'].SetVisibility(pause_state, 1)
        else:
            self.SelectionBoard['pause_mark'].SetVisibility(0, 1)
        self.ShowActionRadiuses(pause_state)
        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['btn_pause']))
        return
        return

    def ShowActionRadiuses(self, show):
        objs = gamemodule.GameManager_GetScene().GetMentalityObjects(globalscript.MT_CHAR)
        i = len(objs) - 1
        while i >= 0:
            char = objs[i].GetPythonCharacter()
            if show == 1:
                import units_base
                if char.UnitInfo != units_base.UnitsBase['Imp']:
                    if char.OverlandCharacter == overlandcharacters.Player:
                        char.ShowActionRadius(show, char.GetVisibilityRange(), 0)
                        i -= 1
                        continue
                    enemy = self.GetCharAlignment(char)
                    if enemy == 0:
                        char.ShowActionRadius(show, char.GetVisibilityRange(), 1)
                        i -= 1
                        continue
                    if enemy == 2:
                        char.ShowActionRadius(show, char.GetVisibilityRange(), 2)
                        i -= 1
                        continue
                    char.ShowActionRadius(0)
            else:
                char.ShowActionRadius(show)
            i -= 1

        return
        return

    def CmdDefAttWrap(self):
        ui.ResetCapture(self.LastMousePos)
        self.Attacker = 1 - self.Attacker
        btn = self.SelectionBoard['btn_defatt']
        btn.SetState(1 + self.Attacker)
        if not self.Attacker:
            if self.Player.Brains:
                self.Player.Brains.SetAutoAttackRange(-10.0)
        elif self.Player.Brains:
            self.Player.Brains.SetAutoAttackRange(g_defs.g_player_auto_attack_range)
        self.SelectButton(None, 0, 0, 0)
        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['btn_defatt']))
        return
        return

    def RollState(self, state):
        if state > 1:
            state = 1
        else:
            state += 1
        return state
        return

    def CmdMoveWrap(self, reset=1):
        btn = self.SelectionBoard['btn_move']
        if self.StartCast == 0:
            st = btn.GetState()
            if self.SelectMove == 0:
                self.SelectMove = 1
                if reset:
                    ui.ResetCapture(self.LastMousePos)
                self.SelectButton(None, 0, 0, 0)
                self.UnselectAllSpells()
                gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['btn_move']))
            else:
                st = self.RollState(st)
                btn.SetState(st)
            if st == 1:
                spells.OnMoveToPoint()
            else:
                spells.OnMoveToPointRun()
        return
        return

    def CmdMMHighLight(self):
        st = self.SelectionBoard['btn_minimap_hl'].GetState()
        if st > 1:
            st = 1
        else:
            st += 1
        self.SelectionBoard['btn_minimap_hl'].SetState(st)
        self.UpdateMMHL()
        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['btn_minimap_hl']))
        return
        return

    def UpdateMMHL(self):
        st = self.SelectionBoard['btn_minimap_hl'].GetState()
        if st == 1:
            self.SelectionBoard['map'].Param['ae'].SetColor('MainBB', ui.BGUI_MM_HL[0], ui.BGUI_MM_HL[1], ui.BGUI_MM_HL[2], ui.BGUI_MM_HL[3])
        else:
            self.SelectionBoard['map'].Param['ae'].SetColor('MainBB', ui.BGUI_MM_LL[0], ui.BGUI_MM_LL[1], ui.BGUI_MM_LL[2], ui.BGUI_MM_LL[3])
        return
        return

    def CmdMMShowUnits(self):
        st = self.SelectionBoard['btn_minimap_units'].GetState()
        if st > 1:
            st = 1
        else:
            st += 1
        self.SelectionBoard['btn_minimap_units'].SetState(st)
        self.UpdateMMUnits()
        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['btn_minimap_units']))
        return
        return

    def UpdateMMUnits(self):
        st = self.SelectionBoard['btn_minimap_units'].GetState()
        globalscript.PlayerConfig.minimap[MM_PLAYER_UNIT] = st - 1
        globalscript.PlayerConfig.minimap[MM_OPPONENT_UNIT] = st - 1
        globalscript.PlayerConfig.minimap[MM_MILITIA] = st - 1
        return
        return

    def CmdSelMode(self):
        st = self.SelectionBoard['btn_selection_mode'].GetState()
        if st > 1:
            st = 1
        else:
            st += 1
        self.SelectionBoard['btn_selection_mode'].SetState(st)
        self.UpdateSelectionMode()
        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GMT_SELECT_BUTTON, 0, tuple(['btn_selection_mode']))
        return
        return

    def UpdateSelectionMode(self):
        globalscript.PlayerConfig.R_ADV_SELECTION = self.GetSelMode()
        objs = gamemodule.GameManager_GetScene().GetMentalityObjects(globalscript.MT_CHAR)
        i = len(objs) - 1
        while i >= 0:
            char = objs[i].GetPythonCharacter()
            char.UpdateAlignment(self.GetCharAlignment(char), self.GetSelMode())
            i -= 1

        return
        return

    def GetSelMode(self):
        return self.SelectionBoard['btn_selection_mode'].GetState() - 1
        return

    def CmdCenterOnPlayer(self):
        ui.ResetCapture(self.LastMousePos)
        gamemodule.GameManager_GetScene().GetBDomainObject().Camera.B_CenterOnUnit(self.Player.MentalityObject)
        return

    def CmdConsole(self):
        self.OnOpenInfo()
        return

    def UpdateSpellsHK(self, bSave):
        PrefObj = gamemodule.PPref_CreateNew()
        PrefObj.CreateInnerData()
        preferences = copy.deepcopy(PrefObj.ExportPref())
        if preferences != None:
            if bSave:
                preferences['SpellsHK'] = {}
            spells_parent = self.SelectionBoard['spells_border']
            i = 0
            cnt = len(spells_parent.Controls)
            while i < cnt:
                bgc = spells_parent.Controls[i]
                sn = bgc.Param['spell'].GetKeyName()
                if bSave:
                    if bgc.Param.has_key('hkspell'):
                        preferences['SpellsHK'][sn] = bgc.Param['hkspell']
                else:
                    if preferences['SpellsHK'].has_key(sn):
                        bgc.Param['hkspell'] = preferences['SpellsHK'][sn]
                    else:
                        bgc.Param['hkspell'] = -1
                    self.UpdateSpellHKTT(bgc)
                i += 1

            if bSave:
                preferences['R_HIDE_MINIMAP'] = self.SelectionBoard['btn_minimap_hl'].GetState() - 1
                preferences['R_ALL_UNITS'] = self.SelectionBoard['btn_minimap_units'].GetState() - 1
                preferences['R_ADV_SELECTION'] = self.SelectionBoard['btn_selection_mode'].GetState() - 1
                PrefObj.ImportPref(preferences)
        if PrefObj != None:
            PrefObj.DeleteInnerData()
            PrefObj = None
        if preferences != None:
            preferences = None
        return
        return

    def CmdHKSpell(self, param):
        key = -2
        if param[0] == 260 and param[2] != vkeys.VK_MENU and param[2] != vkeys.VK_F4:
            key = param[2]
        else:
            return
        if self.SpellSelectedToCast != None:
            if self.StartCast == 0:
                spells_parent = self.SelectionBoard['spells_border']
                i = 0
                cnt = len(spells_parent.Controls)
                while i < cnt:
                    icon = spells_parent.Controls[i]
                    if icon == self.SpellSelectedToCast:
                        icon.Param['hkspell'] = key
                        self.UpdateSpellHKTT(icon)
                        gamemodule.SendMessage(globalscript.GT_TUTORIAL, globalscript.GNT_SPELL_ALT_SYNC, 0, 0)
                    elif icon.Param.has_key('hkspell'):
                        if icon.Param['hkspell'] == key:
                            icon.Param['hkspell'] = -1
                            self.UpdateSpellHKTT(icon)
                    i += 1

        return
        return

    def CmdHKSpellCast(self, param):
        key = param[2]
        if self.StartCast == 0:
            spells_parent = self.SelectionBoard['spells_border']
            i = 0
            cnt = len(spells_parent.Controls)
            while i < cnt:
                icon = spells_parent.Controls[i]
                if icon.Param.has_key('hkspell'):
                    if icon.Param['hkspell'] == key:
                        self.SelectSpell(icon.Param['ae'], 0, 0, 0)
                i += 1

        return
        return

    def UpdateSpellHKTT(self, icon):
        advstr = ''
        if icon.Param.has_key('hkspell'):
            if icon.Param['hkspell'] != -1:
                advstr = d_tools.GetKeyViewEx(icon.Param['hkspell'])
            else:
                del icon.Param['hkspell']
        tt = icon.Param['spell'].GetCastName() + advstr
        icon.Param['ae'].RecalcTTRect(tt, 0, 1)
        return
        return

    def UpdateButtonHKTT(self):
        import d_tools
        PrefObj = gamemodule.PPref_CreateNew()
        PrefObj.CreateInnerData()
        preferences = PrefObj.ExportPref()
        if preferences != None:
            self.SelectionBoard['btn_mainmenu'].Param['ae'].RecalcTTRect(dict.TT_BattleBackToMenu + d_tools.GetKeyViewEx(preferences['bcam']['main_menu'][0]), 0, 1)
            self.SelectionBoard['btn_pause'].Param['ae'].RecalcTTRect(dict.TT_BattlePause + d_tools.GetKeyViewEx(preferences['bcam']['pause_game'][0]), 0, 1)
            self.SelectionBoard['btn_disband'].Param['ae'].RecalcTTRect(dict.TT_BattleSpells + d_tools.GetKeyViewEx(preferences['bcam']['disband_wrap'][0]), 0, 1)
            self.SelectionBoard['btn_retreat'].Param['ae'].RecalcTTRect(dict.BATTLE_TT_LooseBattle + d_tools.GetKeyViewEx(preferences['bcam']['retreat'][0]), 0, 1)
            self.SelectionBoard['btn_move'].Controls[0].Param['ae'].RecalcTTRect(dict.TT_BattleMove + d_tools.GetKeyViewEx(preferences['bcam']['move_wrap'][0]), 0, 1)
            self.SelectionBoard['btn_move'].Controls[1].Param['ae'].RecalcTTRect(dict.TT_BattleRun + d_tools.GetKeyViewEx(preferences['bcam']['move_wrap'][0]), 0, 1)
            self.SelectionBoard['btn_defatt'].Controls[0].Param['ae'].RecalcTTRect(dict.TT_BattleDefend + d_tools.GetKeyViewEx(preferences['bcam']['def_att_wrap'][0]), 0, 1)
            self.SelectionBoard['btn_defatt'].Controls[1].Param['ae'].RecalcTTRect(dict.TT_BattleAttack + d_tools.GetKeyViewEx(preferences['bcam']['def_att_wrap'][0]), 0, 1)
            self.SelectionBoard['btn_minimap_hl'].Controls[0].Param['ae'].RecalcTTRect(dict.TT_BattleMM_LL + d_tools.GetKeyViewEx(preferences['bcam']['hl_map'][0]), 0, 1)
            self.SelectionBoard['btn_minimap_hl'].Controls[1].Param['ae'].RecalcTTRect(dict.TT_BattleMM_HL + d_tools.GetKeyViewEx(preferences['bcam']['hl_map'][0]), 0, 1)
            self.SelectionBoard['btn_minimap_units'].Controls[0].Param['ae'].RecalcTTRect(dict.TT_BattleMM_UH + d_tools.GetKeyViewEx(preferences['bcam']['show_mm_units'][0]), 0, 1)
            self.SelectionBoard['btn_minimap_units'].Controls[1].Param['ae'].RecalcTTRect(dict.TT_BattleMM_UL + d_tools.GetKeyViewEx(preferences['bcam']['show_mm_units'][0]), 0, 1)
            self.SelectionBoard['btn_selection_mode'].Controls[0].Param['ae'].RecalcTTRect(dict.TT_Battle_Adv_Sel + d_tools.GetKeyViewEx(preferences['bcam']['selection_mode'][0]), 0, 1)
            self.SelectionBoard['btn_selection_mode'].Controls[1].Param['ae'].RecalcTTRect(dict.TT_Battle_SEL + d_tools.GetKeyViewEx(preferences['bcam']['selection_mode'][0]), 0, 1)
        if PrefObj != None:
            PrefObj.DeleteInnerData()
            PrefObj = None
        if preferences != None:
            preferences = None
        return
        return

    def h_TrickPoint(self):
        scene = gamemodule.GameManager_GetScene()
        scene.SetPickFilter(globalscript.POINT_OBJECT, 0)
        selected = scene.PickObjectIntersection(self.MUSend[0], self.MUSend[1])
        pos = selected[1]
        gamemodule.CopyTextToClipboard('[' + str(round(pos[0], 1)) + ', ' + str(round(pos[1], 1)) + ', ' + str(round(pos[2], 1)) + ']')
        return

    def Subscribe(self, i=0):
        self._h_TrickPoint = self.h_TrickPoint
        gamemodule.AddMessageHandler(globalscript.GT_GENERAL, globalscript.GM_SIMPLEUPDATE, self._h_Update, 0)
        gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._h_TrickPoint, vkeys.VK_F12)
        return

    def Unsubscribe(self, i=0):
        gamemodule.RemoveMessageHandler(globalscript.GT_GENERAL, globalscript.GM_SIMPLEUPDATE, self._h_Update, 0)
        gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._h_TrickPoint, vkeys.VK_F12)
        del self._h_TrickPoint
        return

    def HotKeys(self, set=1):
        if set == 1:
            self._CmdCenterOnPlayer = self.CmdCenterOnPlayer
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_UP, self._CmdPauseWrap, globalscript.PlayerConfig.bcam['pause_game'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdDefAttWrap, globalscript.PlayerConfig.bcam['def_att_wrap'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdMoveWrap, globalscript.PlayerConfig.bcam['move_wrap'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdDisbandWrap, globalscript.PlayerConfig.bcam['disband_wrap'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdCenterOnPlayer, globalscript.PlayerConfig.bcam['center_on_player'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdConsole, globalscript.PlayerConfig.bcam['bm_msgcon'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, BackToMainMenu, globalscript.PlayerConfig.bcam['main_menu'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdRetreatWrap, globalscript.PlayerConfig.bcam['retreat'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdMMHighLight, globalscript.PlayerConfig.bcam['hl_map'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdMMShowUnits, globalscript.PlayerConfig.bcam['show_mm_units'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdSelMode, globalscript.PlayerConfig.bcam['selection_mode'][0])
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_SYSKBRD, self._CmdHKSpell, None)
            gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_CAST_SPELL, self._CmdHKSpellCast, None)
            fns = []
            fns.append(self.SGSVK1)
            fns.append(self.SGSVK2)
            fns.append(self.SGSVK3)
            fns.append(self.SGSVK4)
            fns.append(self.SGSVK5)
            gs = globalscript.PlayerConfig.bcam['group_sel_save']
            cnt = len(gs)
            i = 0
            while i < cnt:
                if i < len(fns):
                    gamemodule.AddMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, fns[i], gs[i])
                i += 1

        else:
            fns = []
            fns.append(self.SGSVK1)
            fns.append(self.SGSVK2)
            fns.append(self.SGSVK3)
            fns.append(self.SGSVK4)
            fns.append(self.SGSVK5)
            gs = globalscript.PlayerConfig.bcam['group_sel_save']
            cnt = len(gs)
            i = 0
            while i < cnt:
                if i < len(fns):
                    gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, fns[i], gs[i])
                i += 1

            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdSelMode, globalscript.PlayerConfig.bcam['selection_mode'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdMMHighLight, globalscript.PlayerConfig.bcam['hl_map'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdMMShowUnits, globalscript.PlayerConfig.bcam['show_mm_units'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdRetreatWrap, globalscript.PlayerConfig.bcam['retreat'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, BackToMainMenu, globalscript.PlayerConfig.bcam['main_menu'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdConsole, globalscript.PlayerConfig.bcam['bm_msgcon'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_UP, self._CmdPauseWrap, globalscript.PlayerConfig.bcam['pause_game'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdDefAttWrap, globalscript.PlayerConfig.bcam['def_att_wrap'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdMoveWrap, globalscript.PlayerConfig.bcam['move_wrap'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdDisbandWrap, globalscript.PlayerConfig.bcam['disband_wrap'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._CmdCenterOnPlayer, globalscript.PlayerConfig.bcam['center_on_player'][0])
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_SYSKBRD, self._CmdHKSpell, None)
            gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_CAST_SPELL, self._CmdHKSpellCast, None)
            del self._CmdHKSpell
            del self._CmdHKSpellCast
            del self._CmdConsole
            del self._CmdPauseWrap
            del self._CmdDefAttWrap
            del self._CmdMoveWrap
            del self._CmdDisbandWrap
            del self._CmdCenterOnPlayer
        return

    def h_Update(self, time):
        if self.FirstSimpleUpdate == 0:
            self.FirstSimpleUpdate = 1
            self.CmdMoveWrap()
        if self.Player.NowCasting != None:
            if self.Player.NowCasting.SpellLevel == None:
                self.BHUD_spgauge.Gauge.SetColor('MainBB', ui.BGUI_MANA_NOT_ENOUGH[0], ui.BGUI_MANA_NOT_ENOUGH[1], ui.BGUI_MANA_NOT_ENOUGH[2], ui.BGUI_MANA_NOT_ENOUGH[3])
            else:
                self.BHUD_spgauge.Gauge.SetColor('MainBB', ui.BGUI_MANA_NORMAL[0], ui.BGUI_MANA_NORMAL[1], ui.BGUI_MANA_NORMAL[2], ui.BGUI_MANA_NORMAL[3])
            self.SetSPMax(self.Player.NowCasting.ParentSpell.GetManaLimitAtMaxLevel())
            self.SetSP(self.Player.NowCasting.AssignedManaToCast)
        else:
            self.SetSPMax(0)
            self.SetSP(0)
        self.SetHP(self.Player.GetStat('HP'))
        self.SetMana(overlandcharacters.Player.CurrentLocation.BattleMana)
        return
        return

    def Dest(self):
        self.UpdateSpellsHK(1)
        gamemodule.RemoveMessageHandler(globalscript.WT_KEYBOARD, globalscript.KB_DOWN, self._h_Toggle_HUD, self.cg_toggle_hud_key)
        self._h_Toggle_HUD = None
        if g_defs.dev_draw_char_info:
            gamemodule.RemoveMessageHandler(globalscript.GT_GENERAL, globalscript.GM_SIMPLEUPDATE, self.DM_pUpdateFunc, 0)
            self.DM_pUpdateFunc = None
            self.DM_DefDomain = None
            self.DM_AttDomain = None
            self.DM_Pane.Dest()
            self.DM_Pane = None
        self.ShadowSpell_spell = None
        self.ShadowSpell_button = None
        self.UnselectAllUnits(0)
        ks = self.IconsCash.keys()
        i = 0
        while i < len(ks):
            self.IconsCash[ks[i]].Dest()
            i += 1

        self.IconsCash = None
        self.SelectionBoard['selection'].Dest()
        self.SelectionBoard = None
        self.MSObjects = None
        scene = gamemodule.GameManager_GetScene()
        self.MUS.Dest()
        self.StatDisplay.Dest()
        self.StatDisplayBk.Dest()
        if self.SpellSelection:
            self.SpellSelection.Dest()
        self.HideSelectionDisplay()
        i = len(self.KnownSpells) - 1
        while i >= 0:
            self.KnownSpells[0].Dest()
            del self.KnownSpells[0]
            i -= 1

        i = 0
        l = len(self.buttons)
        while i < l:
            self.buttons[0].Dest()
            del self.buttons[0]
            i += 1

        self.Unsubscribe()
        del self._LPickDown
        del self._LPickUp
        del self._PickMove
        del self._RPickDown
        del self._RPickUp
        del self._ResetCallBack
        self.HotKeys(0)
        self.BHUD_underlay_l = None
        self.BHUD_hgauge = None
        self.BHUD_mgauge = None
        self.BHUD_spgauge.Dest()
        self.BHUD_spgauge = None
        self.BHUD_frame_l = None
        self.statustextbkg.Dest()
        self.statustextbkg = None
        self.statustext.Dest()
        self.statustext = None
        gamemodule.KillTimer(self.statustexttimer)
        self.statustexttimer = None
        self.pNoStatusText = None
        self.sounds.Dest()
        self.sounds = None
        self.Player = None
        return
        return

    def SetSPMax(self, max_sp):
        self.MaxSP = max_sp
        self.BHUD_spgauge.SetInitValue(max_sp)
        return

    def SetSP(self, sp):
        if sp < 0 or sp > self.MaxSP:
            return 0
        self.BHUD_spgauge.SetValue(sp)
        return

    def SetHP(self, hp):
        if hp < 0 or hp > self.MaxHP:
            return 0
        self.BHUD_hgauge.SetPos('MainBB', self.HPPosX - 100 * (self.MaxHP - hp) / self.MaxHP, self.HPPosY + 100 * (self.MaxHP - hp) / self.MaxHP, self.HPw, self.HPh)
        return

    def SetMana(self, mana):
        if mana < 0 or mana > self.MaxMana:
            return 0
        self.BHUD_mgauge.SetPos('MainBB', self.ManaPosX - 100 * (self.MaxMana - mana) / self.MaxMana, self.ManaPosY + 100 * (self.MaxMana - mana) / self.MaxMana, self.Manaw, self.Manah)
        return

    def InitIndicators(self, max_hp, max_mana, max_sp):
        self.MaxHP = max_hp
        self.MaxMana = max_mana
        self.SetSPMax(max_sp)
        return


def ShowBattlemenu():
    global BattlemenuScene
    if BattlemenuScene:
        gamemodule.GameManager_SetScene(BattlemenuScene)
    cursor.SetScene(BattlemenuScene, cursor.CUR_ARROW)
    if ResumeGame:
        gamemodule.PauseGame(0)
    globalscript.UpdateConfig(1)
    UpdateCamera()
    if len(Battlemenu) > 0:
        Battlemenu[0].UpdateSpellsHK(0)
        Battlemenu[0].UpdateButtonHKTT()
    import tutor_manager
    tutor_manager.StartTutorial()
    return
    return


def CreateBattlemenu(mini_domain, attackers):
    _plog('BM-CREATE')
    global BattleCash
    global BattlemenuScene
    import overland_map
    gamemodule.Log('Attackers' + repr(attackers))
    gamemodule.Log('Player' + repr(overlandcharacters.Player))
    gamemodule.Log('Owner' + repr(mini_domain.Owner))
    gamemodule.Log('BattleMapFilesInfo' + repr(mini_domain.BattleMapFilesInfo))
    gamemodule.Log('mini_domain.DomainType == overland_map.OL_RANDOM_TYPE' + repr(mini_domain.DomainType == overland_map.OL_RANDOM_TYPE))
    if tutor_manager.EnableTutorial and len(attackers) == 0 or attackers.count(overlandcharacters.Player) != 0 or mini_domain.Owner == overlandcharacters.Player and mini_domain.Owner.AcceptFight(mini_domain):
        BattleCash = BBattleCash()
        BattlemenuScene = gamemodule.Scene_CreateNew()
        BattlemenuScene.CreateInnerData()
        gamemodule.GameManager_SetScene(BattlemenuScene)
        bconsole.AllocConsole(BattlemenuScene)
        gamemodule.AddMessageHandler(globalscript.GT_GENERAL, globalscript.GM_HALTGAME, hHaltGame, 0)
        if mini_domain.DomainType == overland_map.OL_RANDOM_TYPE:
            if not len(mini_domain.BattleMapFilesInfo):
                mini_domain.BattleMapFilesInfo = gamemodule.GetRandomMap('rand', mini_domain.ProcLandScript, mini_domain.I * 1000 + mini_domain.J)
                mini_domain.BattleMapName = mini_domain.BattleMapFilesInfo[0]
                del mini_domain.BattleMapFilesInfo[0]
                globalscript.NodesMapDomainName = 'rand_' + str(mini_domain.I * 1000 + mini_domain.J)
        result = gamemodule.GameManager_LoadScene(mini_domain.BattleMapName, [mini_domain, attackers])
        if mini_domain.DomainType == overland_map.OL_RANDOM_TYPE and result == 0:
            mini_domain.BattleMapFilesInfo = []
            mini_domain.BattleMapFilesInfo = gamemodule.GetRandomMap('rand', mini_domain.ProcLandScript, mini_domain.I * 1000 + mini_domain.J)
            mini_domain.BattleMapName = mini_domain.BattleMapFilesInfo[0]
            del mini_domain.BattleMapFilesInfo[0]
            globalscript.NodesMapDomainName = 'rand_' + str(mini_domain.I * 1000 + mini_domain.J)
            gamemodule.GameManager_LoadScene(mini_domain.BattleMapName, [mini_domain, attackers])
        globalscript.NodesMapDomainName = ''
        Battlemenu.append(CBattlemenu())
        _plog('BM-DONE')
        return BattlemenuScene.GetBDomainObject()
    bdom = None
    if mini_domain.DomainType != overland_map.OL_RANDOM_TYPE:
        exec 'import ' + mini_domain.BattleMapName
        MapScript = eval(mini_domain.BattleMapName)
        bdom = MapScript.Initialize([mini_domain, attackers])
    else:
        import bdomain
        bdom = bdomain.BDomain()
        bdom.SetMiniDomain(mini_domain)
        i = 0
        sz = len(attackers)
        while i < sz:
            bdom.Attackers.append(attackers[i])
            i += 1

        bdom.SetInterfaceMode(0)
        bdom.InitializeData()
        return bdom
    return


def HaltGame():
    DeleteBattlemenu()
    import olmenu
    olmenu.DeleteOLScene()
    gamemodule.SendMessage(globalscript.GT_GENERAL, globalscript.GM_HALTGAME, 0, 0)
    return
    return


def DeleteBattlemenu(bdomain=None):
    global BattleCash
    global BattlemenuScene
    if not bdomain:
        if BattlemenuScene == None:
            return 1
        gamemodule.GameManager_SetScene(BattlemenuScene)
        if len(Battlemenu) > 0:
            import tutor_manager
            tutor_manager.FinalizeTutor()
            gamemodule.RemoveMessageHandler(globalscript.GT_GENERAL, globalscript.GM_HALTGAME, hHaltGame, 0)
            bconsole.FreeConsole(BattlemenuScene)
            Battlemenu[0].Dest()
            del Battlemenu[0]
            BattlemenuScene.DeleteInnerData()
            BattlemenuScene = None
        if BattleCash:
            BattleCash.Dest()
        BattleCash = None
        gamemodule.GameManager_SetScene(0)
    else:
        bdomain.Dest()
    return 1
    return


hHaltGame = HaltGame

def ModifyCallBack(type, param, vars):
    if type == BCCT_ELEMENT:
        if param['name'] == 'icon' + vars[0]:
            param['billboardmain'][5] = vars[1]
            param['events'].append(['LBDown', vars[2], [vars[0]]])
        if param['name'] == 'counter' + vars[0]:
            param['billboardmain'][7][0] = ''
    if type == BCCT_OBJECT:
        param['key'] = vars[0]
        if param['name'] == 'back' + vars[0]:
            param['object'].SetVisibility(0)
    return
    return


def ModifyCallBackSel(type, param, vars):
    if type == BCCT_TEMPLATE:
        # 16:9: контролы, прижатые к краю канваса, придвигаем к краю экрана (см.
        # hd_scale.HD_EdgeFix: сдвиг позиции, размеры SC/SY = k-масштаб -> аспект
        # арта = 4:3; дети едут с родителем). При 4:3 сдвиг = 0, правки нет.
        # Левый список = блок BHUD (XLeft, см. __init__) + колонка кнопок и консоль,
        # прижатая к левому краю; правый = миникарта (frame/map/кнопки - дети mini_map)
        # и зона сброса заклинаний у правого края. try/except: патч не должен уметь
        # ронять создание боевого меню (в 4:3 он и так no-op).
        try:
            import hd_scale
            hd_scale.HD_EdgeFix(param,
                ['console', 'mc_pullbtn', 'mc_pullbtn_bkg', 'btn_mainmenu', 'btn_disband',
                 'btn_retreat', 'btn_pause', 'btn_defatt', 'btn_move'],
                ['mini_map', 'frame', 'spells_border_zone'])
        except Exception:
            pass
        return
    if type == BCCT_ELEMENT:
        if param['name'] == 'map':
            param['billboardmain'][5] = vars[0]
            param['events'].append(['Update', vars[1]._OnChangePos, []])
            param['events'].append(['LBUp', vars[1]._OnMMClick, []])
        if param['name'] == 'btn_mainmenu':
            param['events'].append(['Action', BackToMainMenu, []])
        if param['name'] == 'btn_disband':
            param['events'].append(['Action', vars[1]._CmdDisbandWrap, []])
        if param['name'] == 'btn_retreat':
            param['events'].append(['Action', vars[1]._CmdRetreatWrap, []])
        if param['name'] == 'btn_pause':
            param['events'].append(['Action', vars[1]._CmdPauseWrap, []])
        if param['name'] == 'btn_defatt':
            param['events'].append(['Action', vars[1]._CmdDefAttWrap, []])
        if param['name'] == 'btn_move':
            param['events'].append(['Action', vars[1]._CmdMoveWrap, []])
        if param['name'] == 'btn_minimap_hl':
            param['events'].append(['Action', vars[1]._CmdMMHighLight, []])
        if param['name'] == 'btn_minimap_units':
            param['events'].append(['Action', vars[1]._CmdMMShowUnits, []])
        if param['name'] == 'btn_selection_mode':
            param['events'].append(['Action', vars[1]._CmdSelMode, []])
        if param['name'] == 'selection':
            param['events'].append(['LBDown', vars[1]._LPickDown, []])
            param['events'].append(['LBUp', vars[1]._LPickUp, []])
            param['events'].append(['MouseMove', vars[1]._PickMove, []])
            param['events'].append(['RBDown', vars[1]._RPickDown, []])
            param['events'].append(['RBUp', vars[1]._RPickUp, []])
            param['events'].append(['Update', vars[1]._Update, ()])
        if param['name'] == 'mc_pullbtn':
            param['events'].append(['Action', vars[1]._OnOpenInfo, ()])
    if type == BCCT_OBJECT:
        if param['name'] == 'btn_pause':
            if gamemodule.GamePaused() != 0:
                param['object'].Param['ae'].HoverLock(1)
    return
    return


def ModifyCallBackSpell(type, param, vars):
    if type == BCCT_ELEMENT:
        if param['name'][0:5] == 'spell':
            param['events'].append(['LBUp', vars[0]._SelectSpell, []])
            param['events'].append(['RBDown', vars[0]._SpellRDown, []])
            param['events'].append(['RBUp', vars[0]._SpellRUp, []])
            param['events'].append(['MouseMove', vars[0]._SpellRMove, []])
        if param['name'][0:6] == 'uncast':
            param['events'].append(['LBDown', vars[0]._UncastSpell, []])
    return
    return


def ModifyCallBackLog(type, param, vars):
    if type == BCCT_OBJECT:
        if param['name'] == vars[0]:
            obj = param['object']
            ae = obj.Param['ae']
            h = ui.GetRealTextHeight(ae, 'MainBB')
            rt = obj.GetControlRect()
            rt['bottom'] = rt['top'] + h
            obj.SetControlRect(rt)
    return
    return


def ExtractAE(bm, tp, name):
    if tp == 0:
        if bm.SelectionBoard.has_key(name):
            ctrl = []
            ctrl.append(bm.SelectionBoard[name])
            i = 0
            cnt = len(bm.SelectionBoard[name].Controls)
            while i < cnt:
                ctrl.append(bm.SelectionBoard[name].Controls[i])
                i += 1

            return ctrl
        return None
    if tp == 1:
        spell_border = bm.SelectionBoard['spells_border']
        cnt = len(spell_border.Controls)
        i = 0
        while i < cnt:
            if spell_border.Controls[i].Param['spell'].GetKeyName() == name:
                return [spell_border.Controls[i]]
            i += 1

        return None
    if tp == 2:
        spell_border = bm.SelectionBoard['spells_border_zone']
        cnt = len(spell_border.Controls)
        i = 0
        while i < cnt:
            if spell_border.Controls[i].Param['spell'].ParentBaseSpell.GetKeyName() == name:
                return [spell_border.Controls[i]]
            i += 1

        return None
    if tp == 3:
        if name == '1':
            return [bm.SelectionBoard['selection']]
        return None
    if tp == 4:
        if name == '1':
            bm.EnableCamera(1)
        else:
            bm.EnableCamera(0)
        return None
    if tp == 5:
        if name == '1':
            if gamemodule.GamePaused() == 0:
                bm.CmdPauseWrap()
        elif gamemodule.GamePaused() == 1:
            bm.CmdPauseWrap()
        return None
    if tp == 6:
        if name == '1':
            bm.EnableSelection = 1
            return [bm.SelectionBoard['selection']]
        else:
            bm.EnableSelection = 0
            return None
        return None
    if tp == 7:
        if name == '1':
            return [bm.SelectionBoard['map']]
        return None
    if tp == 8:
        ctrl = []
        cnt = len(bm.SDspells)
        i = 0
        while i < cnt:
            if name == bm.SDspells[i].SpellName:
                ctrl.append(bm.SDspells[i])
            i += 1

        return ctrl
    return None
    return


def ExtractAEList(list):
    res = []
    if len(Battlemenu) > 0:
        b_keys = list.keys()
        icnt = len(b_keys)
        i = 0
        while i < icnt:
            jcnt = len(list[b_keys[i]])
            j = 0
            while j < jcnt:
                el = ExtractAE(Battlemenu[0], b_keys[i], list[b_keys[i]][j])
                if el != None:
                    res += el
                j += 1

            i += 1

    return res
    return


def ResetSelection():
    if len(Battlemenu) > 0:
        Battlemenu[0].ResetCallBack(None, [0, 0, 0])
    return
    return


BHN_DOMAIN_SCENTER_IN_DANGER = 1
BHN_POWER_SPOT_IS_LOST = 2
BHN_MANA_RES_IS_RUN_LOW = 5
BHN_MOREIMPS = 6
BHN_YOU_ARE_UNDER_ATTACK = 7
BHN_HOLD_POS_TO_CAPTURE = 8
BHUD_Redirect = {BHN_DOMAIN_SCENTER_IN_DANGER: [bhud_sounds.BHS_DOMAIN_SCENTER_IN_DANGER, globalscript.GetDictionary().BM_DomCenterIsInDanger, -1, STATUS_TEXT_DELAY_TIME / 2], BHN_POWER_SPOT_IS_LOST: [bhud_sounds.BHS_POWER_SPOT_IS_LOST, globalscript.GetDictionary().BM_PowerSpotIsLost, -1, STATUS_TEXT_DELAY_TIME / 2], BHN_MANA_RES_IS_RUN_LOW: [bhud_sounds.BHS_MANA_RES_IS_RUN_LOW, globalscript.GetDictionary().BM_ManaResourcesIsRunningLow, -1, STATUS_TEXT_DELAY_TIME / 2], BHN_MOREIMPS: [bhud_sounds.BHS_MOREIMPS, globalscript.GetDictionary().BM_CastMoreImps, -1, STATUS_TEXT_DELAY_TIME / 4], BHN_YOU_ARE_UNDER_ATTACK: [bhud_sounds.BHS_YOU_ARE_UNDER_ATTACK, globalscript.GetDictionary().BM_YouAreUnderAttack, -1, STATUS_TEXT_DELAY_TIME / 2], BHN_HOLD_POS_TO_CAPTURE: [-1, globalscript.GetDictionary().BM_HoldPosition1 + str(g_defs.g_domain_capture_delay) + globalscript.GetDictionary().BM_HoldPosition2, -1, STATUS_TEXT_DELAY_TIME / 2]}

def ExecBattleNotify(id, level, in_status=0, in_status_delay=STATUS_TEXT_DELAY_TIME):
    if len(Battlemenu) > 0:
        if not BHUD_Redirect.has_key(id):
            return
        sound_id = BHUD_Redirect[id][0]
        mess_text = BHUD_Redirect[id][1]
        if BHUD_Redirect[id][2] < 0:
            BHUD_Redirect[id][2] = 0
            if sound_id != -1 and Battlemenu[0].sounds:
                Battlemenu[0].sounds.Play(sound_id)
            if mess_text != '':
                Battlemenu[0].AddMessageToLog(mess_text, level, in_status, in_status_delay)
    return
    return


def BreakBattleSpellCast():
    if len(Battlemenu) > 0:
        Battlemenu[0].SpellRUp(0, 0, 0, 0)
    return
    return


def MapMovePoint(x, y):
    if len(Battlemenu) > 0:
        return Battlemenu[0].CalcMMCoord(x, y)
    return None
    return


