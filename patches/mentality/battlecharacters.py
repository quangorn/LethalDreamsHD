import globalscript, gamemodule, whrandom, math, pbase_cast, battlemenu, go_blood, s_3d, overlandcharacters, formulas
from balltecharacters_constants import *
import g_defs, units_base
g_UnitHLMode = 1
HPIND_BIG_OUTER = [0.75, 0.12]
HPIND_BIG_INNER = [0.735, 0.1]
HPIND_SMALL_SIZEMULT = 0.7
HPIND_SMALL_OUTER = [0.35, 0.042]
HPIND_SMALL_INNER = [0.343, 0.035]
HPIND_SMALL_COLORMULT = 0.5

def TwoPointDistance2(p1, p2):
    return math.sqrt(math.pow(p1[0] - p2[0], 2) + math.pow(p1[2] - p2[2], 2))
    return


def TwoPointDistance2_2x(p1, p2):
    return math.pow(p1[0] - p2[0], 2) + math.pow(p1[2] - p2[2], 2)
    return


def FindChainInDefinitionsTable(Table, ChainType):
    i = 0
    while i < len(Table):
        if Table[i][0] == ChainType:
            return Table[i]
        i += 1

    return None
    return


class BBattleCharacter:

    def IsAlly(self, unit):
        if unit.Owner.IsFriend(self.Owner):
            return 1
        else:
            return 0
        return

    def IsImmuneTo(self, spell_name):
        if self.UnitInfo.BImmunities.count('all') or self.UnitInfo.BImmunities.count(spell_name):
            return 1
        else:
            return 0
        return

    def GetSpellLevelDec(self):
        return int(self.GetStat('RES') / 2)
        return

    def IsEnchantedBy(self, spell_name):
        i = len(self.ActiveSpells) - 1
        while i >= 0:
            if self.ActiveSpells[i].CSpellRef.KeyName == spell_name:
                return self.ActiveSpells[i].SpellLevel
            i += 1

        return -1
        return

    def OverrideInitBehaviour(self):
        self.InitBehaviourOverride = 0
        return

    def __init__(self, object):
        self.MentalityObject = object
        self.FleshType = FT_NONE
        self.InitBehaviourOverride = 1
        self.Owner = None
        self.Brains = None
        self.ActiveSpells = []
        self.Name = 'Base class'
        self.FLAGS = 0
        self.OverlandCharacter = None
        self.UnitInfo = None
        self.VisibilityRange = 0
        self.LEVEL = 11
        self.UPKEEP = 0
        self.Stats = {'MAX_HP': 15, 'HP': 15, 'ATT': 10, 'DEF': 3, 'SPD': 10, 'MST': 9, 'SKL': 10, 'RES': 1}
        self.StatMods = {'MAX_HP': 0, 'HP': 0, 'ATT': 0, 'DEF': 0, 'SPD': 0, 'MST': 0, 'SKL': 0, 'RES': 0}
        self.Effect = None
        self.APEffect = None
        self.DeathEffect = None
        self.DeathEffectObject = None
        self.HowISummoned = None
        self.HISfx = None
        self._Unsummon_Update = None
        self.DeathRefCount = 0
        self.SelectionHP = None
        self.SelectionMatPass = None
        self.Selection2 = None
        self.SelectionHP_small = None
        self.SelectionMatPass_small = None
        self.Selection2_small = None
        self.attachedobjects = []
        self.attachables = {}
        self.MentalityObject.SetVisibilityType(MENTALITY_OBJECTS)
        self.Selected = 0
        self.Alignment = 0
        self.SA_mod = 0
        self.SA_active = 0
        self.CD_Timer = -1
        self.CreateSelectionElements()
        self.ImpController = None
        self.GroupID = -1
        self.objects_list = []
        self.hate_cnt = 0
        self.fear_cnt = 0
        self.fury_cnt = 0
        self.madness_cnt = 0
        self.MirtisGO = None
        self.CruansGO = None
        self.VeritionsGO = None
        import go_actionradius
        self.ActionRadius = None
        self.ActionRadius = go_actionradius.go_ActionRadius(self.MentalityObject.GetGraphObject())
        self.ActionRadius.Hide()
        self.RunTimer = None
        self.DisableRun = 0
        self.EnableEnum = 0
        self.AdvName = ''
        self.EnumHash = {}
        self.Name = ''
        return

    def post__init__(self):
        import imp_controller
        self.FLAGS |= self.UnitInfo.BFlags
        self.SetVisibilityRange(g_defs.g_visibility_range * self.UnitInfo.GetRangeMul())
        self.CorrectFPS()
        if self.FLAGS & BCF_PLAYER_OR_NPC:
            self.ImpController = imp_controller.BImpController(self)
        self.Name = self.UnitInfo.Name
        return

    def GetRunTime(self):
        return formulas.GetPlayerRunStrength(self)
        return

    def CreateSelectionElements(self):
        bb = gamemodule.Billboard3D_CreateNew()
        bb.CreateInnerData()
        mat = gamemodule.LoadMaterial('bhud_health03')
        bb.SetMaterial(mat)
        mat = None
        bb.SetSize(HPIND_BIG_OUTER[0], HPIND_BIG_OUTER[1])
        bb.SetColor(0, 0, 0, 1.0)
        self.Selection2 = bb
        bb = gamemodule.Billboard3D_CreateNew()
        bb.CreateInnerData()
        mat = gamemodule.Material_CreateNew()
        mat.CreateInnerData()
        p = mat.GetPass(0)
        p.AddTexture('bhud_health01.tga')
        p.SetBlendFunc(globalscript.BBlend_Add)
        p.SetDepthMask(0)
        p.AddTCModShift(0, 0)
        bb.SetMaterial(mat)
        bb.SetSize(HPIND_BIG_INNER[0], HPIND_BIG_INNER[1])
        mat = None
        self.SelectionHP = bb
        self.SelectionMatPass = p
        bb = gamemodule.Billboard3D_CreateNew()
        bb.CreateInnerData()
        mat = gamemodule.LoadMaterial('bhud_health03')
        bb.SetMaterial(mat)
        mat = None
        bb.SetSize(HPIND_SMALL_OUTER[0], HPIND_SMALL_OUTER[1])
        bb.SetColor(0, 0, 0, 1.0)
        self.Selection2_small = bb
        bb = gamemodule.Billboard3D_CreateNew()
        bb.CreateInnerData()
        mat = gamemodule.Material_CreateNew()
        mat.CreateInnerData()
        p = mat.GetPass(0)
        p.AddTexture('bhud_health01.tga')
        p.SetBlendFunc(globalscript.BBlend_Add)
        p.SetDepthMask(0)
        p.AddTCModShift(0, 0)
        bb.SetMaterial(mat)
        bb.SetSize(HPIND_SMALL_INNER[0], HPIND_SMALL_OUTER[1])
        mat = None
        self.SelectionHP_small = bb
        self.SelectionMatPass_small = p
        return

    def Dest(self):
        gamemodule.Log('battlecharacters.Dest: name=' + self.Name)
        if self.Brains:
            self.int_DeathCommon()
        self.ActionRadius.Hide()
        self.ActionRadius.Dest()
        self.ActionRadius = None
        if self.CD_Timer != -1:
            gamemodule.KillTimer(self.CD_Timer)
        self.CD_Timer = -1
        self.DeathObject()
        if self.ImpController:
            self.ImpController.Dest()
        self.ImpController = None
        if self.Brains:
            self.Brains.Dest()
        self.Brains = None
        sz = len(self.ActiveSpells)
        i = 0
        while i < sz:
            self.ActiveSpells[i].Kill()
            i += 1

        self.ActiveSpells = []
        if self._Unsummon_Update:
            gamemodule.RemoveMessageHandler(globalscript.GT_GENERAL, globalscript.GM_SIMPLEUPDATE, self._Unsummon_Update, 0)
        self._Unsummon_Update = None
        if self.HISfx:
            self.HISfx.Dest()
        self.HISfx = None
        self.Detach(self.Selection2)
        self.Detach(self.Selection2_small)
        self.Detach(self.SelectionHP)
        self.Detach(self.SelectionHP_small)
        self.SelectionMatPass = None
        self.SelectionMatPass_small = None
        self.Selection2 = None
        self.Selection2_small = None
        self.SelectionHP = None
        self.SelectionHP_small = None
        if self.Effect:
            self.Effect.Kill()
        self.Effect = None
        if self.APEffect:
            self.APEffect.Dest()
        self.APEffect = None
        if self.DeathEffectObject:
            self.DeathEffectObject.Kill()
        self.DeathEffectObject = None
        if len(self.attachedobjects):
            gamemodule.Log('battlecharacters.Dest: whooa ! ' + self.Name + ' have ' + str(len(self.attachedobjects)) + ' attached object(s)')
        while len(self.attachedobjects):
            self.attachedobjects.remove(self.attachedobjects[0])

        self.attachedobjects = []
        bdom = gamemodule.GameManager_GetScene().GetBDomainObject()
        if bdom:
            bdom.RemoveCritterFromTrash(self)
        self.MentalityObject = None
        self.Owner = None
        self.OverlandCharacter = None
        self.UnitInfo = None
        self.objects_list = []
        self.MirtisGO = None
        self.CruansGO = None
        self.VeritionsGO = None
        if self.RunTimer:
            gamemodule.KillTimer(self.RunTimer)
        self.RunTimer = None
        self.EnableEnum = None
        self.AdvName = None
        self.EnumHash = None
        return

    def h_StatModVisible(self, objects):
        i = len(objects) - 1
        while i >= 0:
            goname = objects[i].GetName()
            if goname == units_base.UnitsBase['Hate'].ModelName:
                if not self.objects_list.count(objects[i]):
                    self.objects_list.append(objects[i])
                    if self.hate_cnt == 0:
                        self.ChangeStatMod('RES', formulas.HateModifier)
                        self.ChangeStatMod('MST', formulas.HateModifier)
                    self.hate_cnt += 1
            if goname == units_base.UnitsBase['Fear'].ModelName:
                if not self.objects_list.count(objects[i]):
                    self.objects_list.append(objects[i])
                    if self.fear_cnt == 0:
                        self.ChangeStatMod('ATT', formulas.FearModifier)
                    self.fear_cnt += 1
            if goname == units_base.UnitsBase['Fury'].ModelName:
                if not self.objects_list.count(objects[i]):
                    self.objects_list.append(objects[i])
                    if self.fury_cnt == 0:
                        self.ChangeStatMod('SKL', formulas.FuryModifier)
                    self.fury_cnt += 1
            i -= 1

        return

    def h_StatModVisibleAll(self, objects):
        if self.MirtisGO == None:
            if overlandcharacters.Players['Mirtis'].BattleUnit:
                self.MirtisGO = overlandcharacters.Players['Mirtis'].BattleUnit.MentalityObject.GetGraphObject()
            else:
                self.MirtisGO = 0
        if self.VeritionsGO == None:
            if overlandcharacters.Players['Verition'].BattleUnit:
                self.VeritionsGO = overlandcharacters.Players['Verition'].BattleUnit.MentalityObject.GetGraphObject()
            else:
                self.VeritionsGO = 0
        if self.CruansGO == None:
            if overlandcharacters.Players['Cruan'].BattleUnit:
                self.CruansGO = overlandcharacters.Players['Cruan'].BattleUnit.MentalityObject.GetGraphObject()
            else:
                self.CruansGO = 0
        i = len(objects) - 1
        while i >= 0:
            goname = objects[i].GetName()
            if goname == units_base.UnitsBase['Madness'].ModelName:
                if not self.objects_list.count(objects[i]):
                    self.objects_list.append(objects[i])
                    if self.madness_cnt == 0:
                        self.Brains.target_selection_rule |= SB_TSR_ATTACK_ALL
                        self.Brains.ext_Reset()
                    self.madness_cnt += 1
            if self.MirtisGO and goname == units_base.UnitsBase['Mirtis'].ModelName and overlandcharacters.Players['Mirtis'].IsFriend(self) and self.objects_list.count(objects[i]) == 0:
                self.objects_list.append(objects[i])
                self.ChangeStatMod('RES', formulas.MirtisModifier)
            if self.objects_list.count(objects[i]) == 0 and objects[i] != self.MentalityObject.GetGraphObject():
                if self.MirtisGO and goname == units_base.UnitsBase['Mirtis'].ModelName and overlandcharacters.Players['Mirtis'].IsFriend(self):
                    self.objects_list.append(objects[i])
                    self.ChangeStatMod('RES', formulas.MirtisModifier)
                if self.VeritionsGO and goname == units_base.UnitsBase['Verition'].ModelName and overlandcharacters.Players['Verition'].IsFriend(self):
                    self.objects_list.append(objects[i])
                    self.ChangeStatMod('SPD', formulas.VeritionModifier)
                if self.CruansGO and goname == units_base.UnitsBase['Cruan'].ModelName and overlandcharacters.Players['Cruan'].IsFriend(self):
                    self.objects_list.append(objects[i])
                    self.ChangeStatMod('MAX_HP', formulas.CruanModifier)
                    self.ChangeStatMod('HP', formulas.CruanModifier)
            i -= 1

        return

    def h_StatModHide(self, objects):
        i = len(objects) - 1
        while i >= 0:
            goname = objects[i].GetName()
            if goname == units_base.UnitsBase['Hate'].ModelName:
                if self.objects_list.count(objects[i]):
                    self.objects_list.remove(objects[i])
                    self.hate_cnt -= 1
                if self.hate_cnt == 0:
                    self.ChangeStatMod('RES', -formulas.HateModifier)
                    self.ChangeStatMod('MST', -formulas.HateModifier)
            if goname == units_base.UnitsBase['Fear'].ModelName:
                if self.objects_list.count(objects[i]):
                    self.objects_list.remove(objects[i])
                    self.fear_cnt -= 1
                if self.fear_cnt == 0:
                    self.ChangeStatMod('ATT', -formulas.FearModifier)
            if goname == units_base.UnitsBase['Fury'].ModelName:
                if self.objects_list.count(objects[i]):
                    self.objects_list.remove(objects[i])
                    self.fury_cnt -= 1
                if self.fury_cnt == 0:
                    self.ChangeStatMod('SKL', -formulas.FuryModifier)
            i -= 1

        return

    def h_StatModHideAll(self, objects):
        i = len(objects) - 1
        while i >= 0:
            goname = objects[i].GetName()
            if goname == units_base.UnitsBase['Madness'].ModelName:
                if self.objects_list.count(objects[i]):
                    self.objects_list.remove(objects[i])
                    self.madness_cnt -= 1
                if self.madness_cnt == 0:
                    self.Brains.target_selection_rule &= ~SB_TSR_ATTACK_ALL
                    self.Brains.ext_Reset()
            if self.MirtisGO and goname == units_base.UnitsBase['Mirtis'].ModelName:
                if self.objects_list.count(objects[i]):
                    self.objects_list.remove(objects[i])
                    self.ChangeStatMod('RES', -formulas.MirtisModifier)
            if self.VeritionsGO and goname == units_base.UnitsBase['Verition'].ModelName:
                if self.objects_list.count(objects[i]):
                    self.objects_list.remove(objects[i])
                    self.ChangeStatMod('SPD', -formulas.VeritionModifier)
            if self.CruansGO and goname == units_base.UnitsBase['Cruan'].ModelName:
                if self.objects_list.count(objects[i]):
                    self.objects_list.remove(objects[i])
                    self.ChangeStatMod('MAX_HP', -formulas.CruanModifier)
                    self.DamageObject(formulas.CruanModifier)
            i -= 1

        return

    def SetGroupID(self, id):
        self.GroupID = id
        if self.MentalityObject and self.MentalityObject.GetGraphObject():
            self.MentalityObject.GetGraphObject().SetGroupID(id)
        return

    def TargetDied(self, b_unit):
        if self.MentalityObject:
            self.MentalityObject.SetTarget(0)
            self.MentalityObject.SendOrder(TARGET_DIED, 0)
        return

    def WaitTargetDied(self, b_unit):
        if self.MentalityObject:
            self.MentalityObject.ResumeObject()
        return

    def h_EndAnim(self, idx):
        self.MentalityObject.SendOrder(END_ANIM_ORDER, 0)
        return

    def h_EndStandAnim(self, idx):
        self.MentalityObject.SendOrder(END_STAND_ANIM, 0)
        return

    def GetVisibilityRange(self):
        return self.VisibilityRange
        return

    def SetVisibilityRange(self, vr):
        self.MentalityObject.SetVisibilityRange(vr)
        self.VisibilityRange = vr
        return

    def SetPosition(self, x, y, z):
        self.MentalityObject.SetPosition(x, y, z)
        return

    def GetPosition(self):
        return self.MentalityObject.GetPosition()
        return

    def StopObject(self):
        self.MentalityObject.StopObject()
        return

    def AttachSpell(self, spell):
        self.ActiveSpells.append(spell)
        self.SpellChanged()
        return

    def DetachSpell(self, spell):
        if self.ActiveSpells.count(spell):
            self.ActiveSpells.remove(spell)
            self.SpellChanged()
        return

    def DetachAllSpells(self):
        sz = len(self.ActiveSpells)
        i = 0
        while i < sz:
            self.ActiveSpells[0].Uncast()
            i += 1

        return

    def CureObject(self, HPdelta):
        HPtemp = HPdelta
        if self.GetStat('MAX_HP') - self.GetStat('HP') < HPtemp:
            HPtemp = self.GetStat('MAX_HP') - self.GetStat('HP')
        self.ChangeStatBase('HP', HPtemp)
        self.HPChanged()
        return

    def CalcDist(self, unit):
        return TwoPointDistance2(self.GetPosition(), unit.GetPosition())
        return

    def CalcDist_Point(self, pt_xyz):
        return TwoPointDistance2(self.GetPosition(), pt_xyz)
        return

    def CalcCounterVector(self, unit, dist):
        mypos = self.GetPosition()
        enemypos = unit.GetPosition()
        vec = [0, 0]
        vec[0] = enemypos[0] - mypos[0]
        vec[1] = enemypos[2] - mypos[2]
        vec_len = math.sqrt(vec[0] * vec[0] + vec[1] * vec[1])
        vec[0] /= vec_len
        vec[1] /= vec_len
        vec[0] *= -dist
        vec[1] *= -dist
        return vec
        return

    def SetOwner(self, owner):
        if self.Owner:
            gamemodule.GameManager_GetScene().GetBDomainObject().ChangeUpkeep(self.Owner, -self.UPKEEP)
            if self.Owner == overlandcharacters.Players['Syrimbel']:
                self.SetVisibilityRange(self.GetVisibilityRange() - formulas.SyrimbelModifier)
        self.Owner = owner
        if owner:
            self.SetGroupID(owner.GroupID)
        if self.OverlandCharacter:
            self.OverlandCharacter.Owner = owner
        if self.Owner:
            gamemodule.GameManager_GetScene().GetBDomainObject().ChangeUpkeep(self.Owner, self.UPKEEP)
            if len(battlemenu.Battlemenu):
                self.SetAlignment(battlemenu.Battlemenu[0].GetCharAlignment(self))
                self.ShowSmallSelection(globalscript.PlayerConfig.R_ADV_SELECTION)
            if self.Owner == overlandcharacters.Players['Syrimbel']:
                self.SetVisibilityRange(self.GetVisibilityRange() + formulas.SyrimbelModifier)
        if self.EnableEnum:
            if self.Owner:
                if self.Owner == overlandcharacters.Player:
                    unit_num = 1
                    unit_key = self.UnitInfo.KeyName
                    if self.Owner.BattleUnit.EnumHash.has_key(unit_key):
                        self.Owner.BattleUnit.EnumHash[unit_key] += 1
                        unit_num = self.Owner.BattleUnit.EnumHash[unit_key]
                    else:
                        self.Owner.BattleUnit.EnumHash[unit_key] = unit_num
                    self.AdvName = str(unit_num)
        return

    def StatChanged(self):
        if len(battlemenu.Battlemenu) and battlemenu.Battlemenu[0].StatDisplayChar == self:
            battlemenu.Battlemenu[0].StatDisplayUpdate(self)
        return

    def SpellChanged(self):
        if len(battlemenu.Battlemenu) and battlemenu.Battlemenu[0].StatDisplayChar == self:
            battlemenu.Battlemenu[0].ShowSelectionDisplay(self)
        return

    def GetStat_RoTCS(self, statname):
        if self.FLAGS & BCF_PLAYER_OR_NPC:
            return 0
        else:
            return self.GetStat(statname)
        return

    def GetStat(self, statname):
        return self.Stats[statname] + self.StatMods[statname]
        return

    def SetStat(self, statname, value):
        self.Stats[statname] = value
        if statname == 'SPD':
            self.CorrectFPS()
        if statname == 'MAX_HP' or statname == 'HP':
            self.Stats[statname] = value * g_defs.g_HP_mul
            if self.FLAGS & BCF_PLAYER_OR_NPC:
                self.Stats[statname] += g_defs.g_HP_PC_Add
        return

    def GetStatBase(self, statname):
        return self.Stats[statname]
        return

    def ChangeStatBase(self, statname, val):
        self.Stats[statname] = self.Stats[statname] + val
        if statname == 'SPD' and self.MentalityObject and self.MentalityObject.GetGraphObject():
            self.CorrectFPS()
        self.StatChanged()
        return

    def GetStatMod(self, statname):
        return self.StatMods[statname]
        return

    def ChangeStatMod(self, statname, val):
        self.StatMods[statname] = self.StatMods[statname] + val
        if statname == 'SPD' and self.MentalityObject and self.MentalityObject.GetGraphObject():
            self.CorrectFPS()
        self.StatChanged()
        return

    def SA_Set(self, speed):
        if self.SA_active == 1:
            return
        self.SA_active = 1
        self.SA_mod = speed - self.GetStat('SPD')
        self.ChangeStatMod('SPD', self.SA_mod)
        return

    def SA_Reset(self):
        if self.SA_active == 0:
            return
        self.SA_active = 0
        self.ChangeStatMod('SPD', -self.SA_mod)
        return

    def FindAndAttach(self, flag, go):
        parent = self.MentalityObject.GetGraphObject()
        if self.attachables.has_key(flag):
            bone = parent.GetBoneByName(self.attachables[flag][1])
            if bone != 0:
                go.SetLocalPos(0, self.attachables[flag][0], 0)
                go.SetRotationInheritance(0)
                bone.AddChild(go)
                self.attachedobjects.append(go)
                return 0
        return 1
        return

    def AttachTo(self, flag, go):
        if self.FindAndAttach(flag, go):
            if self.FindAndAttach(RP_DEFAULT, go):
                self.MentalityObject.GetGraphObject().AddChild(go)
                self.attachedobjects.append(go)
        return

    def Detach(self, go):
        if not go:
            return
        go.DetachFromParent()
        if self.attachedobjects.count(go):
            self.attachedobjects.remove(go)
        return 0
        return

    def ParseAttachables(self, Default):
        i = 0
        while i < len(self.AttachablesDefinitionTable):
            self.attachables[self.AttachablesDefinitionTable[i][0]] = [self.AttachablesDefinitionTable[i][1], self.AttachablesDefinitionTable[i][2]]
            i += 1

        self.attachables[RP_DEFAULT] = self.attachables[Default]
        return

    def GetSelectionColor(self, alignment):
        color = [1, 1, 1, 1]
        if alignment == 0:
            color = BGUI_HEALTH_INDICATOR_FRIENDLY
        elif alignment == 1:
            color = BGUI_HEALTH_INDICATOR_ENEMY
        elif alignment == 2:
            color = BGUI_HEALTH_INDICATOR_NEUTRAL
        return color
        return

    def Select(self, alignment=None, mode=0):
        if self.Selected:
            return
        self.Selected = 1
        self.Alignment = alignment
        self.AttachTo(RP_OVERTOP, self.Selection2)
        color = self.GetSelectionColor(alignment)
        self.SelectionHP.SetColor(color[0], color[1], color[2], color[3])
        self.AttachTo(RP_OVERTOP, self.SelectionHP)
        self.ShowSmallSelection(mode)
        self.HPChanged()
        return

    def Unselect(self, mode):
        if not self.Selected:
            return
        self.Selected = 0
        self.Detach(self.SelectionHP)
        self.Detach(self.Selection2)
        self.ShowSmallSelection(mode)
        return
        return

    def ShowSmallSelection(self, mode):
        import units_base
        if self.UnitInfo == units_base.UnitsBase['Imp']:
            return
        if self.Selected == 1:
            self.Detach(self.SelectionHP_small)
            self.Detach(self.Selection2_small)
        elif mode == 1:
            self.AttachTo(RP_OVERTOP, self.Selection2_small)
            color = self.GetSelectionColor(self.Alignment)
            self.SelectionHP_small.SetColor(color[0] * HPIND_SMALL_COLORMULT, color[1] * HPIND_SMALL_COLORMULT, color[2] * HPIND_SMALL_COLORMULT, color[3])
            self.AttachTo(RP_OVERTOP, self.SelectionHP_small)
        else:
            self.Detach(self.SelectionHP_small)
            self.Detach(self.Selection2_small)
        return
        return

    def SetAlignment(self, alignment):
        self.Alignment = alignment
        return
        return

    def UpdateAlignment(self, alignment, mode):
        self.SetAlignment(alignment)
        self.ShowSmallSelection(mode)
        return
        return

    def ShowActionRadius(self, show, r=0, type=0):
        if self.ActionRadius != None:
            if show == 1:
                self.ActionRadius.Show(self.GetCastRange(), type)
            else:
                self.ActionRadius.Hide()
        return
        return

    def GetCastRange(self):
        return self.GetVisibilityRange()
        return

    def SelectionModeChanged(self, mode):
        return
        return

    def DamageObject(self, HPdelta, damager=None):
        if self == overlandcharacters.Player.BattleUnit:
            battlemenu.ExecBattleNotify(battlemenu.BHN_YOU_ARE_UNDER_ATTACK, 0, 1)
        pt = self.MentalityObject.GetGraphObject().GetPosition()
        hp = float(self.GetStat('HP')) - HPdelta
        if hp <= 0:
            factor = 1.0
        else:
            factor = HPdelta / hp
            factor = min(max(factor, 0.0), 1.0)
        go_blood.go_Blood(pt, factor, self.FleshType, self.GroupID)
        self.ChangeStatBase('HP', -HPdelta)
        self.HPChanged()
        if self.GetStat('HP') <= 0:
            self.Kill()
        else:
            self.MentalityObject.SendOrder(PAIN_ORDER, [HPdelta, damager])
        return

    def KickObject(self, Target):
        param = 21 + Target.GetStat('SKL') - self.GetStat('SKL')
        random = whrandom.randint(1, 40)
        damage = -1000
        rand = -1000
        if random > param or random == 40:
            if self.FLAGS & BCF_ABILITY_HATE and Target.Brains.GetCurrentMeleeTarget() != self:
                Target.Brains.ext_Reset()
                Target.Brains.bs_main.u_AttackTarget_Melee(self)
            rand = whrandom.randint(-1, 1)
            damage = self.GetStat('ATT') + rand - int(Target.GetStat('DEF') * 0.2)
            if damage < 0:
                damage = 0
            Target.DamageObject(damage, self)
            return 1
        else:
            Target.MissObject()
            return 0
        return

    def MissObject(self):
        self.MentalityObject.SendOrder(MISS_ORDER, 0)
        return

    def HPChanged(self):
        self.SelectionMatPass_small.ResetTCMods()
        self.SelectionMatPass_small.AddTCModShift(1.0 - self.GetStat('HP') / float(self.GetStat('MAX_HP')), 0)
        if not self.Selected:
            return
        battlemenu.Battlemenu[0].UpdateHP(self)
        self.SelectionMatPass.ResetTCMods()
        self.SelectionMatPass.AddTCModShift(1.0 - self.GetStat('HP') / float(self.GetStat('MAX_HP')), 0)
        return

    def int_DeathCommon(self):
        if self.FLAGS & BCF_UNSUMMON_AT_DEATH == 0:
            self.CD_Timer = gamemodule.SetTimer(g_defs.g_corpse_disappear_delay, self.CorpseDisappearTimer)
        self.FLAGS |= BCF_DYING
        self.DeathObject()
        if self.Brains:
            self.Brains.Dest()
            self.Brains = None
        if len(battlemenu.Battlemenu):
            battlemenu.Battlemenu[0].UnselectUnit(self)
        self.ShowSmallSelection(0)
        self.StopObject()
        self.DetachAllSpells()
        self.MentalityObject.SendMessage(globalscript.GT_MENTALITY, globalscript.GM_DIED, 0, tuple([self]))
        if self.Owner and self.Owner.BattleUnit and self.Owner.BattleUnit != self:
            self.Owner.BattleUnit.RemoveCritter(self)
            self.SetOwner(None)
        if self.MentalityObject.GetGraphObject():
            self.MentalityObject.GetGraphObject().ClearAnimationChains()
        self.MentalityObject.ClearBehaviour()
        self.MentalityObject.SetObjectType(globalscript.MT_DEAD_CHAR)
        import tutor_manager
        if tutor_manager.TutorialManager:
            tutor_manager.TutorialManager.RemoveUnit(self.MentalityObject)
        if self.APEffect:
            self.APEffect.Dest()
        self.APEffect = None
        if self.RunTimer:
            gamemodule.KillTimer(self.RunTimer)
        self.RunTimer = None
        return

    def Kill(self):
        if self.FLAGS & BCF_UNSUMMON_AT_DEATH:
            self.Unsummon()
            return
        self.int_DeathCommon()
        self.MentalityObject.GetGraphObject().SetAnimationChain(DIE)
        if self.Effect:
            self.Effect.Dest()
        if self.DeathEffect:
            self.DeathEffectObject = self.DeathEffect()
            self.DeathEffectObject.Apply(self.GetPosition())
            if self.FLAGS & BCF_PLAYER_OR_NPC:
                self.DeathEffectObject.SetDestCallback(self.DeathEffectEnd)
        return

    def CorpseDisappearTimer(self, tid):
        gamemodule.KillTimer(self.CD_Timer)
        self.CD_Timer = -1
        self.Annihil_End()
        return

    def DeathEffectEnd(self):
        import std_misc
        std_misc.CCDied(self)
        return

    def Unsummon(self):
        self.int_DeathCommon()
        self.DeathRefCount = 1
        if self.FLAGS & BCF_DIE_ANIM_AT_UNSUMMON and FindChainInDefinitionsTable(self.AnimationsDefinitionTable, DIE):
            self.DeathRefCount += 1
            self.MentalityObject.GetGraphObject().SetAnimationChain(DIE)
            self.MentalityObject.GetGraphObject().AddEndAnimationFunction(DIE, 0, self.Unsummon_End)
        if self.Effect:
            self.DeathRefCount += 1
            self.Effect.SetDestCallBack(self.Unsummon_End)
            self.Effect.Dest()
        if self.DeathEffect:
            self.DeathEffectObject = self.DeathEffect()
            self.DeathEffectObject.Apply(self.GetPosition())
            if self.FLAGS & BCF_PLAYER_OR_NPC:
                self.DeathEffectObject.SetDestCallback(self.DeathEffectEnd)
        if self.HowISummoned and not self.HISfx:
            self.HISfx = self.HowISummoned()
            self.HISfx.Apply(self.MentalityObject.GetGraphObject())
            if self._Unsummon_Update:
                gamemodule.RemoveMessageHandler(globalscript.GT_GENERAL, globalscript.GM_SIMPLEUPDATE, self._Unsummon_Update, 0)
            self._Unsummon_Update = self.Unsummon_Update
            gamemodule.AddMessageHandler(globalscript.GT_GENERAL, globalscript.GM_SIMPLEUPDATE, self._Unsummon_Update, 0)
        else:
            self.Unsummon_End()
        return

    def Unsummon_Update(self, TimePassed):
        if self.HISfx and self.HISfx.Update(TimePassed):
            gamemodule.RemoveMessageHandler(globalscript.GT_GENERAL, globalscript.GM_SIMPLEUPDATE, self._Unsummon_Update, 0)
            self._Unsummon_Update = None
            self.HISfx.Dest()
            self.HISfx = None
            self.Unsummon_End()
        return

    def Unsummon_End(self, param=0):
        self.DeathRefCount -= 1
        if self.DeathRefCount:
            return
        self.MentalityObject.GetGraphObject().RemoveEndAnimationFunction(DIE, 0, self.Unsummon_End)
        if self.FLAGS & BCF_PLAYER_OR_NPC:
            self.DeathEffectObject.Kill()
            self.DeathEffectObject = None
        gamemodule.GameManager_GetScene().RemoveGraphObject(self.MentalityObject.GetGraphObject())
        gamemodule.GameManager_GetScene().RemoveMentalObject(self.MentalityObject)
        return

    def Annihilate(self, remove_go=1):
        self.FLAGS |= BCF_UNSUMMON_AT_DEATH
        self.int_DeathCommon()
        self.Annihil_End(remove_go)
        return

    def Annihil_End(self, remove_go=1):
        if self.FLAGS & BCF_PLAYER_OR_NPC:
            if self.DeathEffectObject:
                self.DeathEffectObject.Kill()
                self.DeathEffectObject = None
            else:
                self.DeathEffectEnd()
        elif self.MentalityObject == None:
            return
        if remove_go:
            gamemodule.GameManager_GetScene().RemoveGraphObject(self.MentalityObject.GetGraphObject())
        gamemodule.GameManager_GetScene().RemoveMentalObject(self.MentalityObject)
        return

    def DeathObject(self):
        return
        return

    def CalcFPSChain(self, chain_name, mul):
        chain_string = FindChainInDefinitionsTable(self.AnimationsDefinitionTable, chain_name)
        if chain_string == None:
            return -1
        go = self.MentalityObject.GetGraphObject()
        V = self.GetStat('SPD') * mul
        S = go.GetChainTranslation(chain_name, 0)
        BFps = chain_string[4]
        Fr = go.GetChainFramesCount(chain_name, 0)
        V0 = S * BFps / Fr
        Fps = BFps + (V - V0) * Fr / S
        return Fps
        return

    def CorrectFPSChain(self, chain_name, fps):
        chain_string = FindChainInDefinitionsTable(self.AnimationsDefinitionTable, chain_name)
        if chain_string == None:
            return
        self.MentalityObject.GetGraphObject().SetChainFrameRate(chain_name, 0, fps)
        return

    def CorrectFPS(self):
        walk_mul = 0.14
        run_mul = 0.21
        walk_fps = self.CalcFPSChain(LOOPFLAG | MOVE, walk_mul)
        if walk_fps == -1:
            walk_fps = self.CalcFPSChain(MOVE, walk_mul)
        self.CorrectFPSChain(STARTFLAG | MOVE, walk_fps)
        self.CorrectFPSChain(LOOPFLAG | MOVE, walk_fps)
        self.CorrectFPSChain(ENDFLAG | MOVE, walk_fps)
        self.CorrectFPSChain(MOVE, walk_fps)
        run_fps = self.CalcFPSChain(LOOPFLAG | RUN, run_mul)
        if run_fps == -1:
            run_fps = self.CalcFPSChain(RUN, run_mul)
        self.CorrectFPSChain(STARTFLAG | RUN, run_fps)
        self.CorrectFPSChain(LOOPFLAG | RUN, run_fps)
        self.CorrectFPSChain(ENDFLAG | RUN, run_fps)
        self.CorrectFPSChain(RUN, run_fps)
        return

    def StartRunFunction(self, tid=None):
        if not self.MentalityObject or not self.MentalityObject.GetGraphObject():
            if self.RunTimer:
                gamemodule.KillTimer(self.RunTimer)
            self.RunTimer = None
            self.DisableRun = 0
            return
        if not tid:
            if not self.RunTimer:
                self.RunTimer = gamemodule.SetTimer(formulas.GetPlayersRunStrength(self), self.StartRunFunction)
            if self.DisableRun and self.MentalityObject.GetGraphObject().GetAnimationChain() & RUN:
                self.MentalityObject.GetGraphObject().SetAnimationChain(STARTFLAG | MOVE)
        if tid:
            if self.DisableRun == 0:
                gamemodule.KillTimer(self.RunTimer)
                self.RunTimer = gamemodule.SetTimer(formulas.GetPlayersRestoreSpeed(self), self.StartRunFunction)
                self.DisableRun = 1
                if self.MentalityObject.GetGraphObject().GetAnimationChain() & RUN:
                    self.MentalityObject.GetGraphObject().SetAnimationChain(STARTFLAG | MOVE)
            else:
                gamemodule.KillTimer(self.RunTimer)
                if self.MentalityObject.GetGraphObject().GetAnimationChain() & MOVE:
                    self.MentalityObject.GetGraphObject().SetAnimationChain(STARTFLAG | RUN)
                    self.RunTimer = gamemodule.SetTimer(formulas.GetPlayersRunStrength(self), self.StartRunFunction)
                else:
                    self.RunTimer = None
                self.DisableRun = 0
        return 1
        return

    def ProcessCommandAnimation(self, command_list):
        i = 0
        sz = len(command_list)
        while i < sz:
            if len(command_list[i]) < 4:
                command_list[i].append(None)
            if command_list[i][0] == DIRECT_OBJECT_TO_POINT_RUN or command_list[i][0] == DIRECT_OBJECT_TO_CHAR_RUN:
                command_list[i][1] |= RUN_FUNCTION
                command_list[i][3] = self.StartRunFunction
            if command_list[i][3]:
                self.MentalityObject.AddCommandAnimation(command_list[i][0], command_list[i][1], command_list[i][2], command_list[i][3])
            else:
                self.MentalityObject.AddCommandAnimation(command_list[i][0], command_list[i][1], command_list[i][2])
            i += 1

        return

    def ParseAnimationChainsInfo(self):
        cnt = len(self.AnimationsDefinitionTable)
        indexes = []
        go = self.MentalityObject.GetGraphObject()
        i = 0
        while i < cnt:
            row = self.AnimationsDefinitionTable[i]
            index = go.AddAnimationChain(row[0], row[1], row[2])
            indexes.append(index)
            go.SetChainDynamic(row[0], index, row[3])
            go.SetChainFrameRate(row[0], index, row[4])
            go.SetChainLooped(row[0], index, row[5])
            if row[7]:
                go.AddEndAnimationFunction(row[0], index, row[7])
            sound = row[8]
            j = len(sound)
            import sounds
            snd = sounds.GetSoundFromGroup('Units', self.UnitInfo.KeyName, row[0])
            if snd:
                self.SoundsDir = sounds.GetSoundFromGroup('Units', self.UnitInfo.KeyName, sounds.SOUND_DIR_KEY)
                sound = snd
            if len(sound) and len(sound[0]):
                snd = s_3d.Sound3D()
                snd.Create_go(go, self.SoundsDir)
                if j > 3:
                    snd.Set_MuteAtMaxDist(sound[3])
                if j > 2 and len(sound[2]):
                    snd.Set_Times(sound[2][0], sound[2][1])
                if j > 1 and len(sound[1]):
                    snd.Set_DistMuls(sound[1][0], sound[1][1])
                import string
                if len(sound[0]) and string.find(sound[0][0], '/') == -1:
                    snd.AddSounds(sound[0])
                elif len(sound[0]):
                    snd.BaseDir = ''
                    snd.AddSounds(sound[0])
                    snd.BaseDir = self.SoundsDir
                go.AssignChainSound(row[0], index, snd.SoundExInst)
            i += 1

        i = 0
        while i < cnt:
            go.SetChainNextChain(self.AnimationsDefinitionTable[i][0], indexes[i], self.AnimationsDefinitionTable[i][6])
            i += 1

        return

    def ApplyAstralProjectionEffect(self):
        import go_astralprojection
        self.APEffect = go_astralprojection.go_AProjFX()
        self.APEffect.Apply(self.MentalityObject.GetGraphObject())
        return


class Mage(BBattleCharacter):

    def __init__(self, object, OverlandChar=None):
        BBattleCharacter.__init__(self, object)
        import units_base
        self.OverlandCharacter = OverlandChar
        if not OverlandChar:
            self.OverlandCharacter = overlandcharacters.CCharacter(units_base.UnitsBase['Empty'], '')
            self.OverlandCharacter.BattleUnit = self
        self.NowCasting = None
        self.InterfaceCast = None
        self.LastCastedSpell = None
        self.CastedSpells = []
        self.Critters = []
        self.Imps = []
        self.MCEffects = []
        return

    def MCE_Add(self, MCEffect):
        self.MCEffects.append(MCEffect)
        return

    def MCE_Remove(self, MCEffect):
        if self.MCEffects.count(MCEffect):
            self.MCEffects.remove(MCEffect)
        return

    def Dest(self):
        if self.NowCasting:
            if self.InterfaceCast:
                self.InterfaceCast.Dest()
            else:
                self.NowCasting.BreakCast(0)
        self.NowCasting = None
        self.InterfaceCast = None
        self.LastCastedSpell = None
        while len(self.MCEffects):
            self.MCEffects[0].Kill()

        while len(self.CastedSpells):
            self.CastedSpells[0].Kill()

        sz = len(self.Critters)
        i = 0
        while i < sz:
            gamemodule.Log('Remove Critter : ' + self.Critters[0].Name)
            gamemodule.GameManager_GetScene().RemoveMentalObject(self.Critters[0].MentalityObject)
            i += 1

        self.Critters = []
        i = 0
        sz = len(self.Imps)
        while i < sz:
            gamemodule.Log('Remove Imp : ' + self.Imps[0].Name)
            gamemodule.GameManager_GetScene().RemoveMentalObject(self.Imps[0].MentalityObject)
            i += 1

        self.Imps = []
        ovch = self.OverlandCharacter
        BBattleCharacter.Dest(self)
        self.OverlandCharacter = ovch
        import units_base
        if self.OverlandCharacter and self.OverlandCharacter.UnitInfo == units_base.UnitsBase['Empty']:
            self.OverlandCharacter.Dest()
        self.OverlandCharacter = None
        return

    def SetOwner(self, owner):
        BBattleCharacter.SetOwner(self, owner)
        if owner:
            self.OverlandCharacter.CurrentLocation = owner.CurrentLocation
        return

    def SetOverlandCharacter(self, char):
        self.OverlandCharacter = char
        return

    def GetCastRange(self):
        return self.GetStat('MST') * g_defs.g_cast_range_mul + g_defs.g_cast_range_add
        return

    def GetCastSpeed(self):
        return float(self.GetStat('MST')) * g_defs.g_cast_speed_mul
        return

    def AddImp(self, imp):
        gamemodule.Log('[battlecharacters.py]AddImp()')
        self.Imps.append(imp)
        if self.Brains:
            self.Brains.bc_AddCritter(imp)
        return

    def RemoveImp(self, imp):
        gamemodule.Log('[battlecharacters.py]RemoveImp()')
        if self.Imps.count(imp):
            self.Imps.remove(imp)
            gamemodule.GameManager_GetScene().GetBDomainObject().AddCritterToTrash(imp)
            if self.Brains:
                self.Brains.bc_RemoveCritter(imp)
        return

    def GetAvailImp(self):
        i = len(self.Imps) - 1
        while i >= 0:
            if self.Imps[i].IsBuzzy == 0:
                return self.Imps[i]
            i -= 1

        return None
        return

    def GetNumAvailImps(self):
        i = len(self.Imps) - 1
        n = 0
        while i >= 0:
            if self.Imps[i].IsBuzzy == 0:
                n += 1
            i -= 1

        return n
        return

    def AddCastedSpell(self, spell):
        self.CastedSpells.append(spell)
        return

    def RemoveCastedSpell(self, spell):
        if self.CastedSpells.count(spell):
            self.CastedSpells.remove(spell)
        return

    def AddCritter(self, critter):
        import battlemenu
        if len(battlemenu.Battlemenu) > 0:
            if self.IsAlly(overlandcharacters.Player.BattleUnit):
                battlemenu.Battlemenu[0].AddElemToMM(battlemenu.MM_PLAYER_UNIT, critter)
            else:
                battlemenu.Battlemenu[0].AddElemToMM(battlemenu.MM_OPPONENT_UNIT, critter)
        if self.Brains:
            self.Brains.bc_AddCritter(critter)
        self.Critters.append(critter)
        return

    def RemoveCritter(self, critter):
        import battlemenu
        if len(battlemenu.Battlemenu) > 0:
            battlemenu.Battlemenu[0].RemoveElemFromMM(critter)
        if self.Brains:
            self.Brains.bc_RemoveCritter(critter)
        if self.Critters.count(critter):
            self.Critters.remove(critter)
            gamemodule.GameManager_GetScene().GetBDomainObject().AddCritterToTrash(critter)
        return

    def int_DeathCommon(self):
        import battlemenu
        if len(battlemenu.Battlemenu) > 0:
            battlemenu.Battlemenu[0].RemoveElemFromMM(self)
        if self.NowCasting:
            if self.InterfaceCast:
                self.InterfaceCast.Dest()
            else:
                self.NowCasting.BreakCast(0)
                self.NowCasting = None
        self.InterfaceCast = None
        self.LastCastedSpell = None
        i = len(self.CastedSpells) - 1
        while i >= 0:
            self.CastedSpells[i].Uncast()
            i -= 1

        while len(self.Critters):
            self.Critters[0].Unsummon()

        while len(self.Imps):
            self.Imps[0].Unsummon()

        BBattleCharacter.int_DeathCommon(self)
        return

    def DamageObject(self, HPdelta, damager=None):
        BBattleCharacter.DamageObject(self, HPdelta, damager)
        return

    def ai_CastObject(self, SpellName, Target, SpellLevel, KnownLevel=10):
        import ov_func_spell, spells
        if self.NowCasting or self.InterfaceCast:
            self.EndCastObject(0)
        spell = ov_func_spell.BOverlandFunctionalitySpell(spells.RegisteredSpells[SpellName])
        spell.ParentCharacter = self.OverlandCharacter
        spell.SetKnownLevel(KnownLevel)
        self.NowCasting = spell
        spell.StartCast(SpellLevel, Target)
        return

    def CastObject(self, SpellName, battletype=0):
        if self.NowCasting or self.InterfaceCast:
            self.EndCastObject(0)
        spell = None
        i = 0
        sz = len(self.OverlandCharacter.AvailableSpells['Known'])
        while i < sz:
            if self.OverlandCharacter.AvailableSpells['Known'][i].GetKeyName() == SpellName:
                spell = self.OverlandCharacter.AvailableSpells['Known'][i]
                break
            i += 1

        if not spell:
            return 0
        self.NowCasting = spell
        if battletype:
            self.InterfaceCast = pbase_cast.PBaseCast(self, spell)
        else:
            spell.StartCast()
        self.LastCastedSpell = spell.ParentSpell
        return 1
        return

    def CastObject_Name(self, SpellName, battletype=0, known_level=0):
        import ov_func_spell, spells
        if self.NowCasting or self.InterfaceCast:
            self.EndCastObject(0)
        spell = ov_func_spell.BOverlandFunctionalitySpell(spells.RegisteredSpells[SpellName])
        spell.ParentCharacter = self.OverlandCharacter
        spell.SetKnownLevel(known_level)
        self.NowCasting = spell
        if battletype:
            self.InterfaceCast = pbase_cast.PBaseCast(self, spell)
        else:
            spell.StartCast()
        self.LastCastedSpell = spell.ParentSpell
        return 1
        return

    def EndCastObject(self, CheckUtilSpell=1):
        if self.InterfaceCast:
            self.InterfaceCast.Dest()
            self.NowCasting = None
            self.InterfaceCast = None
            if CheckUtilSpell:
                if self.LastCastedSpell and self.LastCastedSpell.UtiliteSpell:
                    self.CastObject(self.LastCastedSpell.GetKeyName(), 1)
                else:
                    self.CastObject('DirectObjectRun', 1)
        elif self.NowCasting:
            self.NowCasting = None
        return


