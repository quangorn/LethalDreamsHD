import gamemodule, globalscript, battlecharacters, overlandcharacters
from balltecharacters_constants import *
from spell_constants import *
import std_bstates, spells, whrandom, math, g_defs, units_base
import formulas
AI_LOGGING = 1

def LOG(who, what):
    if AI_LOGGING:
        gamemodule.Log('% ' + who.unit.Name + ':' + what)
    return

# Радиус агро (обнаружения) юнита в бою; задаёт план 10.2.
AGGRO_RANGE_ALLY = 4.0
AGGRO_RANGE_ENEMY = 6.0

def IsEnemyOwner(owner):
    if owner == None:
        return 0
    if owner == overlandcharacters.Player:
        return 0
    p = overlandcharacters.Player
    if p == None:
        return 0
    return owner.GroupID & p.GetOpponentSides() != 0


ESCORT_POS = [[[]], [[0.0, 1.0]], [[-1.0, 0.0], [1.0, 0.0]], [[-1.0, 0.0], [0.0, 1.0], [1.0, 0.0]], [[-1.0, 0.0], [0.0, 1.0], [1.0, 0.0], [0.0, -1.0]], [[-1.0, 0.0], [0.0, 1.0], [1.0, 0.0], [-0.5, -1.0], [0.5, -1.0]], [[-1.5, 0.0], [1.5, 0.0], [-1.0, 1.0], [1.0, 1.0], [-1.0, -1.0], [1.0, -1.0]]]
ESCORT_MUL = 1.5

class BBrain_Base:

    def __init__(self, bunit):
        self.unit = bunit
        self.ment = self.unit.MentalityObject
        self.bs_main = None
        self.bs_attack = None
        self.bs_kick = None
        self.bs_lockedanim = None
        self.bdomain = gamemodule.GameManager_GetScene().GetBDomainObject()
        self.mo_Group = None
        self.grouping_flags = SB_GF_NONE
        self.loiter_type = SB_LOITER_NONE
        self.loiter_radius = 10
        self.loiter_frequency = 10
        self.attack_type = SB_AT_MELEE
        self.kick_n_run_dist = 4.0
        self.remote_ctl_mode = SB_RCM_ALWAYS
        self.target_selection_rule = SB_TSR_MAXATT
        self.ext_amb_func = None
        self.target_selection_flags = SB_TSF_EXCEPT_ATTACK
        self.remote_ctl_override = 1
        self.LoiterFunc = None
        return

    def GetAggroRange(self):
        vr = AGGRO_RANGE_ENEMY
        if not IsEnemyOwner(self.unit.Owner):
            vr = AGGRO_RANGE_ALLY
        if self.unit.Owner == overlandcharacters.Players['Syrimbel']:
            vr = vr + formulas.SyrimbelModifier
        return vr

    def IsWithinAggro(self, unit):
        r = self.GetAggroRange()
        if battlecharacters.TwoPointDistance2_2x(self.unit.GetPosition(), unit.GetPosition()) > r * r:
            return 0
        return 1

    def FilterAggroObjs(self, objs):
        # Предикат правила отбора (CHAR_FUNCTION): оставляет кандидатов в радиусе
        # агро. Пока идёт приказ перемещения игрока, не оставляет никого (план 10.2/10.3).
        ret = []
        if self.bs_main and self.bs_main.move_order:
            return ret
        r = self.GetAggroRange()
        r2 = r * r
        mypos = self.unit.GetPosition()
        i = len(objs) - 1
        while i >= 0:
            ment = objs[i].GetMentalityPart()
            if ment and ment.GetPythonCharacter():
                u = ment.GetPythonCharacter()
                if battlecharacters.TwoPointDistance2_2x(mypos, u.GetPosition()) <= r2:
                    ret.append(objs[i])
            i -= 1
        return ret

    def Dest(self):
        LOG(self, 'BBrain_Base.Dest: dying ....')
        self.BStates_Term()
        if not self.grouping_flags & SB_GF_ONE_GROUP_PER_CLASS:
            self.DetachFromGroup_self()
        self.bdomain = None
        self.mo_Group = None
        self.unit = None
        self.ment = None
        self.ext_amb_func = None
        self.LoiterFunc = None
        return

    def h_EnemyPlayerFound(self, players_go):
        if players_go[0].GetMentalityPart().GetObjectType() == globalscript.MT_CHAR:
            allied_commanders = self.bdomain.GetCharactersAtGroups(players_go[0].GetMentalityPart().GetPythonCharacter().Owner.GetOpponentSides())
            i = 0
            sz = len(allied_commanders)
            while i < sz:
                if allied_commanders[i].BattleUnit and allied_commanders[i].BattleUnit.MentalityObject:
                    allied_commanders[i].BattleUnit.MentalityObject.SendOrder(B_PLAYER_FOUND, players_go[0].GetMentalityPart().GetPythonCharacter())
                i += 1

        return

    def AttachToGroup_unit(self, unit):
        if unit.Brains.mo_Group:
            self.mo_Group = unit.Brains.mo_Group
            self.mo_Group.AttachObject(self.ment)
        else:
            self.mo_Group = self.ment.CreateGroupObject('grp_light')
            self.mo_Group.GetPythonCharacter().Setup(self)
            self.mo_Group.GetPythonCharacter().StartThinking()
        if self.grouping_flags & SB_GF_FLOCK:
            self.ment.SendOrder(DIRECT_TO, [None, self.mo_Group.GetPosition()])
        return

    def AttachToGroup_group(self, mo_group):
        if mo_group:
            self.mo_Group = mo_group
            self.mo_Group.AttachObject(self.ment)
        else:
            self.mo_Group = self.ment.CreateGroupObject('grp_light')
            self.mo_Group.GetPythonCharacter().Setup(self)
            self.mo_Group.GetPythonCharacter().StartThinking()
        if self.grouping_flags & SB_GF_FLOCK:
            self.ment.SendOrder(DIRECT_TO, [None, self.mo_Group.GetPosition()])
        return

    def DetachFromGroup_self(self):
        if self.mo_Group == None:
            return
        self.mo_Group.DetachObject(self.ment)
        self.mo_Group = None
        return

    def Loiter_Radius(self):
        pos = self.ment.GetRandomPointInRadius(self.loiter_radius)
        self.ment.SendOrder(DIRECT_TO, [None, pos])
        return

    def SelectLoiterStrategy(self):
        self.LoiterFunc = None
        if self.loiter_type == SB_LOITER_RADIUS:
            self.LoiterFunc = self.Loiter_Radius
            return
        return

    def SetRoTCS(self, bstate):
        rule = DONT_SELECT_TARGET
        parm = {}
        rule |= DISTANCEMIN
        rule |= OBJECTS_FILTER
        rule |= CHAR_FUNCTION
        parm[CHAR_FUNCTION] = self.FilterAggroObjs
        if self.target_selection_rule & SB_TSR_ATTACK_ALL:
            parm[OBJECTS_FILTER] = [OBJECT_CLASS | INCLUDE, {OBJECT_CLASS: [MENTALITY_OBJECT, globalscript.MT_CHAR]}]
        else:
            parm[OBJECTS_FILTER] = [OBJECT_CLASS | OBJECT_OWNER | INCLUDE, {OBJECT_OWNER: NO_MY_OWNER, OBJECT_CLASS: [MENTALITY_OBJECT, globalscript.MT_CHAR]}]
        if self.target_selection_rule & SB_TSR_MASK == SB_TSR_MAXATT:
            rule |= PARAMETERMAX
            parm[PARAMETERMAX] = ['GetStat_RoTCS', 'ATT']
        if self.target_selection_rule & SB_TSR_MASK == SB_TSR_MAXHP:
            rule |= PARAMETERMAX
            parm[PARAMETERMAX] = ['GetStat_RoTCS', 'HP']
        if self.target_selection_rule & SB_TSR_AMBIGUITY:
            rule |= AMBIGUITYFUNC
            parm[AMBIGUITYFUNC] = self.ext_amb_func
        bstate.SetRuleOfTargetCharSelection(rule, parm)
        bstate.SetRuleOfTargetPointSelection(DONT_SELECT_TARGET, 0)
        return

    def StartThinking(self):
        LOG(self, 'StartThinking()')
        if self.unit.InitBehaviourOverride == 0:
            return
        self.BStates_Init()
        opponents = self.bdomain.GetCharactersAtGroups(self.unit.Owner.GetOpponentSides())
        opponentsGO = []
        i = 0
        sz = len(opponents)
        while i < sz:
            bu = opponents[i].BattleUnit
            if bu and bu.MentalityObject and bu.MentalityObject.GetGraphObject():
                opponentsGO.append(bu.MentalityObject.GetGraphObject())
            i += 1

        if len(opponentsGO):
            self.bs_main.bstate.SetRuleOfTargetCharSelection(OBJECTS, {OBJECTS: opponentsGO}, self.h_EnemyPlayerFound)
        self.SelectLoiterStrategy()
        self.SetStatModsTraps()
        self.ment.SetCurrentBehaviourState(self.bs_main.bstate)
        import g_defs
        self.ment.EnableNotifications(g_defs.dev_enable_notifications)
        return

    def SetStatModsTraps(self):
        tfilter = 0
        tparms = {}
        tfilter = OBJECTS_FILTER
        tparms[OBJECTS_FILTER] = [OBJECT_TYPE, {OBJECT_TYPE: 1}]
        self.ment.SetRuleOfTargetCharSelection(tfilter, tparms, self.unit.h_StatModVisibleAll)
        self.ment.SetHideTargetCharSelection(tfilter, tparms, self.unit.h_StatModHideAll)
        tfilter = 0
        tparms = {}
        tfilter = OBJECTS_FILTER
        tparms[OBJECTS_FILTER] = [OBJECT_OWNER | INCLUDE, {OBJECT_OWNER: NO_MY_OWNER}]
        self.ment.SetRuleOfTargetCharSelection(tfilter, tparms, self.unit.h_StatModVisible)
        self.ment.SetHideTargetCharSelection(tfilter, tparms, self.unit.h_StatModHide)
        return

    def SetCtlOverride(self):
        self.remote_ctl_override = 0
        return

    def ResetCtlOverride(self):
        self.remote_ctl_override = 1
        return

    def TestOrderObeyance(self, param0=0, param1=0):
        return self.remote_ctl_override
        return

    def ext_Reset(self):
        # Сброс подавления агро от приказа игрока (план 10.3).
        if self.bs_main:
            self.bs_main.move_order = 0
        self.bs_main.u_RewindStateStack()
        self.ment.StopObject()
        self.ment.SetTarget(0)
        self.ment.ClearEventsQueue(0, 0)
        self.ment.EndCurrentBehaviourState()
        self.ment.SetCurrentBehaviourState(self.bs_main.bstate)
        self.ment.GetGraphObject().SetAnimationChain(STAND)
        self.ment.SendOrder(NEXT_FRAME, 0)
        return

    def GetCurrentMeleeTarget(self):
        target = self.ment.GetTarget()
        if target == None:
            return None
        if target[2] == None:
            return None
        return target[2].GetPythonCharacter()
        return

    def BStates_Init(self):
        self.ment.ClearBehaviour()
        self.bs_main = std_bstates.BattleMain(self)
        self.bs_attack = std_bstates.BattleAttack(self)
        self.bs_kick = std_bstates.BattleKick(self)
        self.bs_lockedanim = std_bstates.LockedAnim(self)
        return

    def BStates_Term(self):
        self.ment.ClearBehaviour()
        if self.bs_main:
            self.bs_main.Dest()
            self.bs_main = None
        if self.bs_attack:
            self.bs_attack.Dest()
            self.bs_attack = None
        if self.bs_kick:
            self.bs_kick.Dest()
            self.bs_kick = None
        if self.bs_lockedanim:
            self.bs_lockedanim.Dest()
            self.bs_lockedanim = None
        return


class BMage_Brain(BBrain_Base):

    def __init__(self, bunit):
        BBrain_Base.__init__(self, bunit)
        self.bs_cast2 = None
        self.bs_magic_attack2 = None
        self.bs_enchant2 = None
        self.grouping_flags = SB_GF_NONE
        self.loiter_type = SB_LOITER_NONE
        self.loiter_radius = 10
        self.loiter_frequency = 10
        self.attack_type = SB_AT_CAST
        self.kick_n_run_dist = 4.0
        self.remote_ctl_mode = SB_RCM_ALWAYS
        self.target_selection_rule = SB_TSR_AMBIGUITY
        self.ext_amb_func = self.h_TargetSelector
        self.target_selection_flags = SB_TSF_EXCEPT_ATTACK
        self.OoCRT = 0.0
        self.danger_range = 2.5
        self.disengage_dist = 10.0
        self.disengage_tries_max = 2
        self.CCL_mana = 0.4
        self.CCL_upkeep = 0.1
        self.ADL_mana = 0.3
        self.ADL_upkeep = 0.3
        self.l_venemys = []
        self.unit.Owner.CurrentLocation.AddCasterCharacter(self.unit)
        return

    def Dest(self):
        if self.unit.Owner.CurrentLocation:
            self.unit.Owner.CurrentLocation.RemoveCasterCharacter(self.unit)
        self.l_venemys = []
        spells = self.unit.OverlandCharacter.AvailableSpells['Known']
        i = len(spells) - 1
        while i >= 0:
            if spells[i].AICast:
                spells[i].AICast.Dest()
            spells[i].AICast = None
            i -= 1

        BBrain_Base.Dest(self)
        return

    def h_TargetSelector(self, objs):
        self.l_venemys = objs
        self.ment.SendOrder(B_ENEMY_FOUND, objs)
        return 0
        return

    def u_GetEnchantLevel(self, spell_name, unit):
        i = len(unit.ActiveSpells) - 1
        while i >= 0:
            if unit.ActiveSpells[i].CSpellRef.GetKeyName() == spell_name:
                return unit.ActiveSpells[i].SpellLevel
            i -= 1

        return -1
        return

    def u_CanCastDist(self, target):
        if self.unit.CalcDist(target) > self.unit.GetCastRange():
            return 0
        else:
            return 1
        return

    def u_GetMaxUpkeep(self):
        return self.unit.Owner.CurrentLocation.GetCasterUpkeepLimit(self.unit)
        return

    def u_GetUpkeep(self):
        upkeep = 0
        i = 0
        sz = len(self.unit.CastedSpells)
        while i < sz:
            upkeep += self.unit.CastedSpells[i].Result[len(self.unit.CastedSpells[i].Result) - 1]
            i += 1

        return upkeep
        return

    def u_GetUpkeepStatus(self):
        ms = self.unit.Owner.CurrentLocation
        return float(self.u_GetMaxUpkeep() - self.u_GetUpkeep()) / self.u_GetMaxUpkeep()
        return

    def u_GetManaStatus(self):
        ms = self.unit.Owner.CurrentLocation
        return float(ms.BattleMana) / ms.BattleMaxMana
        return

    def u_ObtainManaLimit(self):
        if self.u_GetManaStatus() < self.ADL_mana or self.u_GetUpkeepStatus() < self.ADL_upkeep:
            if len(self.unit.CastedSpells):
                self.unit.CastedSpells[0].Uncast()
            return 1
        return 0
        return

    def u_ManaCheck_Level(self):
        if self.u_GetUpkeepStatus() < self.CCL_upkeep:
            return 0
        if self.u_GetManaStatus() < self.CCL_mana:
            return 0
        return 1
        return

    def u_GetGroupUpkeep(self, cl):
        group_upkeep = 0
        i = len(cl) - 1
        while i >= 0:
            group_upkeep += cl[i].UPKEEP
            i -= 1

        return group_upkeep
        return

    def u_FindSpell(self, s_flags):
        ofs_spells = self.unit.OverlandCharacter.AvailableSpells['Known']
        i = len(ofs_spells) - 1
        while i >= 0:
            if ofs_spells[i].ParentSpell.Flags & s_flags == s_flags:
                return ofs_spells[i]
            i -= 1

        return None
        return

    def StartThinking(self):
        if self.unit.InitBehaviourOverride == 0:
            return
        ms = self.unit.Owner.CurrentLocation
        spells = self.unit.OverlandCharacter.AvailableSpells['Known']
        i = len(spells) - 1
        while i >= 0:
            ofs_spell = spells[i]
            if not ofs_spell.ParentSpell.Flags & SF_NON_COMBAT_CAST and ofs_spell.AICast == None:
                exec 'import ' + ofs_spell.ParentSpell.SpellMentalityName
                spell_script = eval(ofs_spell.ParentSpell.SpellMentalityName)
                ofs_spell.AICast = spell_script.AICastInitialize(self.unit, ofs_spell)
            i -= 1

        BBrain_Base.StartThinking(self)
        return

    def BStates_Init(self):
        self.ment.ClearBehaviour()
        self.bs_main = std_bstates.SCMain2(self)
        self.bs_attack = std_bstates.BattleAttack(self)
        self.bs_kick = std_bstates.BattleKick(self)
        self.bs_lockedanim = std_bstates.LockedAnim(self)
        self.bs_cast2 = std_bstates.SCCastSpell2(self)
        self.bs_magic_attack2 = std_bstates.SCMagicAttack2(self)
        self.bs_enchant2 = std_bstates.SCEnchant2(self)
        return

    def BStates_Term(self):
        self.ment.ClearBehaviour()
        if self.bs_main:
            self.bs_main.Dest()
            self.bs_main = None
        if self.bs_attack:
            self.bs_attack.Dest()
            self.bs_attack = None
        if self.bs_kick:
            self.bs_kick.Dest()
            self.bs_kick = None
        if self.bs_lockedanim:
            self.bs_lockedanim.Dest()
            self.bs_lockedanim = None
        if self.bs_cast2:
            self.bs_cast2.Dest()
            self.bs_cast2 = None
        if self.bs_magic_attack2:
            self.bs_magic_attack2.Dest()
            self.bs_magic_attack2 = None
        if self.bs_enchant2:
            self.bs_enchant2.Dest()
            self.bs_enchant2 = None
        return


class BCreatureController_Brain(BMage_Brain):

    def __init__(self, bunit):
        BMage_Brain.__init__(self, bunit)
        self.bs_recon = None
        self.bs_main = None
        self.bs_rlock = None
        self.bs_attack = None
        self.bs_kick = None
        self.bs_lockedanim = None
        self.bs_FBG = None
        self.bs_cast2 = None
        self.bs_magic_attack2 = None
        self.bs_summon2 = None
        self.bs_summon_imp = None
        self.bs_enchant2 = None
        self.grouping_flags = SB_GF_NONE
        self.loiter_type = SB_LOITER_NONE
        self.loiter_radius = 10
        self.loiter_frequency = 10
        self.attack_type = SB_AT_CAST
        self.kick_n_run_dist = 4.0
        self.remote_ctl_mode = SB_RCM_ALWAYS
        self.target_selection_rule = SB_TSR_AMBIGUITY
        self.ext_amb_func = self.h_TargetSelector
        self.target_selection_flags = SB_TSF_EXCEPT_MOVE | SB_TSF_EXCEPT_ATTACK
        self.OoCRT = 0.0
        self.danger_range = 2.5
        self.disengage_dist = 10.0
        self.disengage_tries_max = 1
        self.CCL_mana = 0.1
        self.CCL_upkeep = 0.01
        self.ADL_mana = 0.05
        self.ADL_upkeep = -0.1
        self.protect_dc = 0
        self.melee_contingency = 0.25
        self.recon_upkeep = 0.2
        self.escort_upkeep = 0.7
        self.messengers_upkeep = 0.1
        self.player_attack_activation_dist = 1.25
        self.HP_danger_limit = 0.75
        self.def_rlock_limit = 0.4
        self.att_rlock_limit = 0.5
        self.summon_timeout = 8
        self.need_more_imps = 1
        self.cl_current = None
        self.cl_recon = []
        self.cl_escort = []
        self.LastPlayerPos = []
        self.LastTimePlayerFound = 0
        i = len(self.bdomain.Attackers) - 1
        while i >= 0:
            if self.bdomain.Attackers[i] != self.unit.OverlandCharacter and self.bdomain.Attackers[i].CurrentLocation == self.unit.OverlandCharacter.CurrentLocation:
                self.recon_upkeep /= 2.0
                self.escort_upkeep /= 2.0
            i -= 1

        i = len(self.bdomain.Defenders) - 1
        while i >= 0:
            if self.bdomain.Defenders[i] != self.unit.OverlandCharacter and self.bdomain.Defenders[i].CurrentLocation == self.unit.OverlandCharacter.CurrentLocation:
                self.recon_upkeep /= 2.0
                self.escort_upkeep /= 2.0
            i -= 1

        return

    def Dest(self):
        BMage_Brain.Dest(self)
        self.cl_recon = []
        self.cl_escort = []
        self.cl_current = None
        return

    def u_GetUpkeep(self):
        upkeep = BMage_Brain.u_GetUpkeep(self)
        i = 0
        sz = len(self.unit.Critters)
        while i < sz:
            upkeep += self.unit.Critters[i].UPKEEP
            i += 1

        return upkeep
        return

    def h_RespawnFound(self, objs):
        self.ment.SendOrder(B_RESPAWN_FOUND, objs[0].GetMentalityPart().GetPythonCharacter())
        return

    def h_RespawnLocked(self, respawn):
        self.ment.SendOrder(B_RESPAWN_LOCKED, respawn)
        return

    def h_DCCapture(self):
        self.ment.SendOrder(B_DOMAIN_HEART_CAPTURE, 0)
        return

    def BStates_Init(self):
        self.ment.ClearBehaviour()
        if self.bdomain.Attackers.count(self.unit.OverlandCharacter):
            self.bs_main = std_bstates.CCMain_att(self)
            self.bs_rlock = std_bstates.SCRespawnLock(self)
            self.escort_upkeep += self.recon_upkeep
            self.recon_upkeep = 0.0
        else:
            self.bs_main = std_bstates.CCMain_def(self)
            self.bs_recon = std_bstates.CCRecon(self)
            self.bs_rlock = std_bstates.SCRespawnUnlock(self)
        self.bs_main.bstate.SetRuleOfTargetCharSelection(OBJECTS_FILTER, {OBJECTS_FILTER: [OBJECT_CLASS | INCLUDE, {OBJECT_CLASS: [MENTALITY_OBJECT, globalscript.MT_RESPAWN]}]}, self.h_RespawnFound)
        self.bs_attack = std_bstates.BattleAttack(self)
        self.bs_kick = std_bstates.BattleKick(self)
        self.bs_lockedanim = std_bstates.LockedAnim(self)
        self.bs_FBG = std_bstates.CCFormBattleGroup(self)
        self.bs_cast2 = std_bstates.SCCastSpell2(self)
        self.bs_magic_attack2 = std_bstates.SCMagicAttack2(self)
        self.bs_summon2 = std_bstates.SCSummon2(self)
        self.bs_summon_imp = std_bstates.CCSummonImp(self)
        self.bs_enchant2 = std_bstates.SCEnchant2(self)
        return

    def BStates_Term(self):
        self.ment.ClearBehaviour()
        if self.bs_main:
            self.bs_main.Dest()
            self.bs_main = None
        if self.bs_attack:
            self.bs_attack.Dest()
            self.bs_attack = None
        if self.bs_kick:
            self.bs_kick.Dest()
            self.bs_kick = None
        if self.bs_lockedanim:
            self.bs_lockedanim.Dest()
            self.bs_lockedanim = None
        if self.bs_recon:
            self.bs_recon.Dest()
            self.bs_recon = None
        if self.bs_FBG:
            self.bs_FBG.Dest()
            self.bs_FBG = None
        if self.bs_rlock:
            self.bs_rlock.Dest()
            self.bs_rlock = None
        if self.bs_cast2:
            self.bs_cast2.Dest()
            self.bs_cast2 = None
        if self.bs_magic_attack2:
            self.bs_magic_attack2.Dest()
            self.bs_magic_attack2 = None
        if self.bs_summon2:
            self.bs_summon2.Dest()
            self.bs_summon2 = None
        if self.bs_summon_imp:
            self.bs_summon_imp.Dest()
            self.bs_summon_imp = None
        if self.bs_enchant2:
            self.bs_enchant2.Dest()
            self.bs_enchant2 = None
        return

    def bc_AddCritter(self, critter):
        self.ment.ClearEventsQueue(MAIN_STATE_ORDERS, 1)
        self.ment.SendOrder(ADD_CRITTER, critter)
        if self.cl_current == None:
            return
        self.cl_current.append(critter)
        return

    def bc_RemoveCritter(self, critter):
        if self.cl_recon.count(critter):
            self.cl_recon.remove(critter)
            self.ment.SendOrder(B_UNIT_LOST, [self.cl_recon, critter])
            return
        if self.cl_escort.count(critter):
            self.cl_escort.remove(critter)
            self.ment.SendOrder(B_UNIT_LOST, [self.cl_escort, critter])
            return
        self.ment.SendOrder(B_UNIT_LOST, [None, critter])
        return

    def u_IsUnitBusy(self, unit):
        if unit.Brains == None:
            return 1
        if unit.Brains.ment.GetCurrentBehaviourState() == unit.Brains.bs_main.bstate and (unit.Brains.bs_main.moving == 0 or unit.Brains.bs_main.loitering == 1):
            return 0
        else:
            return 1
        return

    def u_MoveGroup_Free(self, pos, group):
        free_group = []
        i = len(group) - 1
        while i >= 0:
            if self.u_IsUnitBusy(group[i]) == 0:
                free_group.append(group[i])
            i -= 1

        self.u_MoveGroup(pos, free_group)
        return

    def u_MoveGroup(self, pos, group):
        """
        #--- old style
        nc=len(group)

        if nc==0 : return
        if nc > 6 : nc=6

        i=0
        while i<nc :
            self.u_DirectUnit(group[i],[pos[0]+ESCORT_POS[nc][i][0]*ESCORT_MUL, pos[1], pos[2]+ESCORT_POS[nc][i][1]*ESCORT_MUL])
            i+=1
        """
        sz = len(group)
        SelectedObjects = []
        i = 0
        while i < sz:
            unit = group[i]
            if battlecharacters.TwoPointDistance2_2x(unit.GetPosition(), self.unit.GetPosition()) < math.pow(self.unit.GetVisibilityRange(), 2):
                unit.MentalityObject.SendOrder(DIRECT_TO | REMOTE_ORDER, [None, pos])
            else:
                SelectedObjects.append(unit)
            i += 1

        if self.unit.ImpController == None:
            return
        if len(SelectedObjects):
            if not self.unit.ImpController.h_DeliverOrder(pos, SelectedObjects):
                self.need_more_imps = 1
        return

    def u_MoveToWithEscort(self, pos, adjust_speed=0):
        speed = 0
        if adjust_speed:
            speed = self.unit.GetStat('SPD')
            i = len(self.cl_escort) - 1
            while i >= 0:
                if self.cl_escort[i].GetStat('SPD') < speed:
                    speed = self.cl_escort[i].GetStat('SPD')
                i -= 1

            self.unit.SA_Set(speed - 1.5)
        self.ment.SendOrder(DIRECT_TO, [None, pos])
        self.u_MoveEscort(pos, speed)
        return

    def u_MoveEscort(self, pos, group_speed=0):
        if group_speed:
            nc = len(self.cl_escort)
            i = 0
            while i < nc:
                self.cl_escort[i].SA_Set(group_speed)
                i += 1

        self.u_MoveGroup(pos, self.cl_escort)
        return

    def u_StopObject(self, unit):
        # Сброс подавления агро от приказа игрока (план 10.3).
        if self.bs_main:
            self.bs_main.move_order = 0
        unit.MentalityObject.SetCurrentBehaviourState('suspend')
        unit.MentalityObject.StopObject()
        unit.MentalityObject.SetTarget(0)
        return

    def u_CheckEscortUpkeep(self):
        ret = float(self.u_GetGroupUpkeep(self.cl_escort)) / self.u_GetMaxUpkeep() / self.escort_upkeep
        return ret
        return

    def u_ConsolidateEscortForces(self):
        while len(self.cl_recon):
            self.cl_escort.append(self.cl_recon[0])
            self.cl_recon.remove(self.cl_recon[0])

        return


class BPlayer_Brain(BBrain_Base):

    def __init__(self, bunit):
        BBrain_Base.__init__(self, bunit)
        self.grouping_flags = SB_GF_NONE
        self.loiter_type = SB_LOITER_NONE
        self.loiter_radius = 10
        self.attack_type = SB_AT_MELEE
        self.kick_n_run_dist = 4.0
        self.remote_ctl_mode = SB_RCM_ALWAYS
        self.target_selection_rule = SB_TSR_AMBIGUITY
        self.ext_amb_func = self.h_TargetSelector
        self.target_selection_flags = SB_TSF_EXCEPT_ATTACK | SB_TSF_EXCEPT_MOVE
        self.AAR = -10.0
        return

    def SetAutoAttackRange(self, rad):
        self.AAR = rad
        return

    def h_RespawnFound(self, objs):
        return
        return

    def h_RespawnLocked(self, respawn):
        return
        return

    def h_DCCapture(self):
        return
        return

    def h_TargetSelector(self, objs):
        self.ment.SendOrder(B_ENEMY_FOUND, objs)
        return 0
        return

    def bc_AddCritter(self, critter):
        return
        return

    def bc_RemoveCritter(self, critter):
        return
        return

    def UpdateEnemyPlayerPos(self, pos):
        return
        return

    def BStates_Init(self):
        self.ment.ClearBehaviour()
        self.bs_main = std_bstates.PlayerSuspend(self)
        self.bs_attack = std_bstates.BattleAttack(self)
        self.bs_kick = std_bstates.BattleKick(self)
        self.bs_lockedanim = std_bstates.LockedAnim(self)
        return

    def StartThinking(self):
        if self.unit.InitBehaviourOverride == 0:
            return
        self.BStates_Init()
        self.SetStatModsTraps()
        self.ment.SetCurrentBehaviourState(self.bs_main.bstate)
        import g_defs
        self.ment.EnableNotifications(g_defs.dev_enable_notifications)
        return

    def PBase_Cast_Started(self):
        self.bs_main.u_RewindStateStack()
        return

    def Dest(self):
        BBrain_Base.Dest(self)
        return


class BBrain_DarkMirror(BBrain_Base):

    def __init__(self, bunit):
        BBrain_Base.__init__(self, bunit)
        self._h_TwinDied = self.h_TwinDied
        self.p_target = None
        self.p_spell = None
        return

    def h_TwinDied(self, unit):
        self.unit.Kill()
        return

    def SetParentSpell(self, p_spell):
        self.p_spell = p_spell
        self.p_target = self.p_spell.Target
        self.p_target.MentalityObject.AddMessageHandler(globalscript.GT_MENTALITY, globalscript.GM_DIED, self._h_TwinDied, 0)
        self.update_time = 3.0
        self.grouping_flags = SB_GF_NONE
        self.loiter_type = SB_LOITER_NONE
        self.loiter_radius = 10
        self.attack_type = SB_AT_MELEE
        self.kick_n_run_dist = 4.0
        self.remote_ctl_mode = SB_RCM_NEVER
        self.target_selection_rule = SB_TSR_MAXATT
        self.ext_amb_func = None
        self.target_selection_flags = SB_TSF_EXCEPT_ATTACK | SB_TSF_EXCEPT_MOVE
        return

    def Dest(self):
        if self._h_TwinDied and self.p_target:
            self.p_target.MentalityObject.RemoveMessageHandler(globalscript.GT_MENTALITY, globalscript.GM_DIED, self._h_TwinDied, 0)
        self._h_TwinDied = None
        self.p_target = None
        self.p_spell = None
        BBrain_Base.Dest(self)
        return


class BBrain_Gatekeeper(BMage_Brain):

    def __init__(self, bunit):
        BMage_Brain.__init__(self, bunit)
        self.teleport_timeout = 0
        self.req_sl = -1
        i = 0
        while i < 10:
            if spells.RegisteredSpells['Teleport'].GetCastResult(i, None)[1] >= self.unit.LEVEL:
                self.req_sl = i
                break
            i += 1

        return

    def h_TargetSelector(self, objs):
        self.teleport_timeout -= 1
        mypos = self.ment.GetPosition()
        objs = gamemodule.GameManager_GetScene().PickObjectsInCircle(mypos[0], mypos[2], g_defs.g_visibility_range)
        enemy_obj = None
        ally_obj = None
        i = len(objs) - 1
        while i >= 0:
            if objs[i].GetMentalityPart() and objs[i].GetMentalityPart().GetPythonCharacter() and objs[i].GetMentalityPart().GetObjectType() == globalscript.MT_CHAR:
                unit = objs[i].GetMentalityPart().GetPythonCharacter()
                if unit.UnitInfo == self.unit.UnitInfo and unit != self.unit:
                    ally_obj = objs[i]
                if unit.Owner != self.unit.Owner:
                    enemy_obj = objs[i]
                if (ally_obj or self.teleport_timeout > 0) and enemy_obj:
                    return enemy_obj
            i -= 1

        self.teleport_timeout = 3
        spell = self.unit.OverlandCharacter.AvailableSpells['Known'][0].AICast
        spell.Setup(self.unit, self.req_sl)
        self.bs_cast2.Run(spell)
        return 0
        return


class BBrain_RainbowBird(BMage_Brain):

    def __init__(self, bunit):
        BMage_Brain.__init__(self, bunit)
        self.update_time = 3.0
        self.grouping_flags = SB_GF_NONE
        self.loiter_type = SB_LOITER_RADIUS
        self.loiter_radius = 20
        self.attack_type = SB_AT_CAST
        self.kick_n_run_dist = 4.0
        self.remote_ctl_mode = SB_RCM_NEVER
        self.target_selection_rule = SB_TSR_AMBIGUITY
        self.ext_amb_func = self.h_TargetSelector
        self.target_selection_flags = SB_TSF_EXCEPT_ATTACK
        self.OoCRT = 2.0
        self.enchant_yourself = 0
        self.enchant_others_good = 0
        self.enchant_others_bad = 0
        self.danger_range = 5
        CR = self.unit.GetCastRange()
        if self.danger_range >= CR:
            self.danger_range = 2
        self.disengage_dist = 10.0
        self.disengage_tries_max = 100
        self.mana_can_cast_limit = 0.4
        self.mana_auto_dispell_limit = 0.3
        self.acquire_target_in_cast = 1
        return


class BBrain_Homunculus(BMage_Brain):

    def __init__(self, bunit):
        BMage_Brain.__init__(self, bunit)
        self.update_time = 3.0
        self.grouping_flags = SB_GF_NONE
        self.loiter_type = SB_LOITER_NONE
        self.loiter_radius = 10
        self.attack_type = SB_AT_CAST
        self.kick_n_run_dist = 4.0
        self.remote_ctl_mode = SB_RCM_ALWAYS
        self.target_selection_rule = SB_TSR_MAXATT
        self.ext_amb_func = self.h_TargetSelector
        self.target_selection_flags = SB_TSF_EXCEPT_ATTACK
        self.OoCRT = 2.0
        self.enchant_yourself = 1
        self.enchant_others_good = 0
        self.enchant_others_bad = 0
        self.danger_range = 5
        CR = self.unit.GetCastRange()
        if self.danger_range >= CR:
            self.danger_range = 2
        self.disengage_dist = 10.0
        self.disengage_tries_max = 0
        self.mana_can_cast_limit = 0.4
        self.mana_auto_dispell_limit = 0.3
        self.acquire_target_in_cast = 1
        return


class BBrain_DarkTwin(BMage_Brain):

    def __init__(self, bunit):
        BMage_Brain.__init__(self, bunit)
        self.update_time = 1.0
        self.grouping_flags = SB_GF_NONE
        self.loiter_type = SB_LOITER_NONE
        self.loiter_radius = 10
        self.attack_type = SB_AT_CAST
        self.kick_n_run_dist = 4.0
        self.remote_ctl_mode = SB_RCM_ALWAYS
        self.target_selection_rule = SB_TSR_MAXATT | SB_TSR_AMBIGUITY
        self.ext_amb_func = self.h_TargetSelector
        self.target_selection_flags = SB_TSF_EXCEPT_ATTACK
        self.OoCRT = 2.0
        self.enchant_yourself = 1
        self.enchant_others_good = 1
        self.enchant_others_bad = 1
        self.danger_range = 5
        CR = self.unit.GetCastRange()
        if self.danger_range >= CR:
            self.danger_range = 2
        self.disengage_dist = 10.0
        self.disengage_tries_max = 2
        self.mana_can_cast_limit = 0.4
        self.mana_auto_dispell_limit = 0.3
        self.acquire_target_in_cast = 1
        return

    def i_ParseKnownSpells(self):
        BMage_Brain.i_ParseKnownSpells(self)
        return


class BBrain_DarkRaven(BBrain_Base):

    def __init__(self, bunit):
        BBrain_Base.__init__(self, bunit)
        self.grouping_flags = SB_GF_NONE
        self.loiter_type = SB_LOITER_RADIUS
        self.loiter_radius = 25
        self.loiter_frequency = 10
        self.attack_type = SB_AT_MELEE
        self.kick_n_run_dist = 4.0
        self.remote_ctl_mode = SB_RCM_ALWAYS
        self.target_selection_rule = SB_TSR_AMBIGUITY
        self.ext_amb_func = self.h_TargetSelector
        self.target_selection_flags = SB_TSF_EXCEPT_ATTACK
        return

    def h_TargetSelector(self, objs):
        if self.ment.GetCurrentBehaviourState() != self.bs_main.bstate:
            return 0
        i = len(objs) - 1
        while i >= 0:
            if objs[i].GetMentalityPart().GetPythonCharacter() and objs[i].GetMentalityPart().GetPythonCharacter().UnitInfo == units_base.UnitsBase['Imp']:
                return objs[i]
            i -= 1

        return 0
        return

    def Dest(self):
        BBrain_Base.Dest(self)
        return


