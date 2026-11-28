import gamemodule, globalscript, whrandom, math
from balltecharacters_constants import *
from spell_constants import *
import std_brains, battlecharacters, overlandcharacters, g_defs, spell_functionality_base, operator, formulas
CCS_NONE = 0
CCS_RECON = 100
CCS_FORM_GROUP = 200
CCS_ATTACK = 300
CCS_LOCK_RESPAWN = 302
CCS_ATTACK_PLAYER = 400
CCS_PROTECTING_YOURSELF = 500
CCS_DISENGAGE = 600
CCS2_MAIN = 0
CCS2_CAST = 1
CCS2_MELEE = 2
CCS2_DISENGAGE = 3
CCS2_ESCORT_SUMMON = 4
CCS2_LOCK_RESPAWN = 2

def LOG(who, what):
    if std_brains.AI_LOGGING:
        gamemodule.Log(((((('% ' + who.unit.Name) + ':') + who.name) + ':') + what))


class StateBase:

    def __init__(self, parent):
        self.parent = parent
        self.unit = self.parent.unit
        self.ment = self.parent.ment
        self.name = 'stat_base'
        self.moving = 0
        self.ExtRun = 0



    def Dest(self):
        self.parent = None
        self.unit = None
        self.ment = None



    def h_PainTest(self, ordertype, params):
        self.ment.SendOrder(B_DAMAGE, params)
        if (params[0] > formulas.GetBreakCastPoints(self.unit)):
            self.ment.SendOrder(BREAK_CAST, 0)
            return 1
        else:
            return 0



    def h_MissTest(self, param0 = 0, param1 = 1):
        if self.moving:
            return 0
        elif (whrandom.random() < 0.25):
            return 1
        else:
            return 0



    def u_RewindStateStack(self):
        while (self.ment.GetCurrentBehaviourState() != self.bstate):
            self.ment.EndCurrentBehaviourState()

        self.ment.ClearEventsQueue(MAIN_STATE_ORDERS, 0)



    def u_ExitCurState(self, param = 0):
        self.u_RewindStateStack()
        self.ment.EndCurrentBehaviourState()



    def h_StartFunc(self):
        LOG(self, 'entering')
        return 



    def h_EndFunc(self):
        LOG(self, 'exiting')
        if (self.ExtRun == 0):
            self.ment.SendOrder(STATE_ENDED, 0)



    def Run(self):
        self.unit.MentalityObject.SetCurrentBehaviourState(self.bstate)
        self.ExtRun = 1
        ret = self.Think()
        self.ExtRun = 0
        return ret



class BattleMain(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'battle_main'
        self.bstate = self.ment.CreateNewBehaviourState('main')
        self.bstate.SetOrderTransparencyMask(0)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [STOP_UNIT, 0], ((FORGET_TARGET | STOP_OBJECT) | NOTIFY_ACTION), {NOTIFY_ACTION: self.n_StopUnit})
        if (self.parent.remote_ctl_mode < SB_RCM_NEVER):
            self.bstate.AddEventReaction(ORDER, [(REMOTE_ORDER | DIRECT_TO), self.parent.TestOrderObeyance], ((FUNCTION_ACTION | DIRECT_OBJECT_TO_POINT) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_DirectTo_Remote, NOTIFY_ACTION: self.n_DirectTo_Remote})
            self.bstate.AddEventReaction(ORDER, [(REMOTE_ORDER | DIRECT_TO_RUN), self.parent.TestOrderObeyance], ((FUNCTION_ACTION | DIRECT_OBJECT_TO_POINT_RUN) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_DirectTo_Remote, NOTIFY_ACTION: self.n_DirectTo_Remote})
        if (self.parent.grouping_flags & SB_GF_FLOCK):
            self.bstate.SetRuleOfTargetCharSelection(OBJECTS_FILTER, {OBJECTS_FILTER: [(OBJECT_OWNER | INCLUDE), {OBJECT_OWNER: MY_OWNER}]}, self.h_TestForAlly)
        self.bstate.AddEventReaction(NEW_TARGET_FOUND, [MO_TARGET], ((NOTIFY_ACTION | DIRECT_OBJECT_TO_CHAR_RUN) | CHANGE_STATE), {CHANGE_STATE: 'attack', NOTIFY_ACTION: self.n_TargetFound})
        self.bstate.AddEventReaction(ORDER, [DIRECT_TO, 0], ((FUNCTION_ACTION | NOTIFY_ACTION) | DIRECT_OBJECT_TO_POINT), {FUNCTION_ACTION: self.h_DirectTo, NOTIFY_ACTION: self.n_DirectTo})
        self.bstate.AddEventReaction(ORDER, [DIRECT_TO_POINT, 0], ((FUNCTION_ACTION | NOTIFY_ACTION) | DIRECT_OBJECT_TO_POINT_RUN), {FUNCTION_ACTION: self.h_DirectTo_Point, NOTIFY_ACTION: self.n_DirectTo_Point})
        self.bstate.AddEventReaction(TARGET_REACHED, [POINT_TARGET, [0, 0]], (((FUNCTION_ACTION | NOTIFY_ACTION) | STOP_OBJECT) | FORGET_TARGET), {FUNCTION_ACTION: self.h_TargetReached, NOTIFY_ACTION: self.n_TargetReached})
        self.bstate.AddEventReaction(TARGET_REACHED, [GO_TARGET, [0, 0]], (((FUNCTION_ACTION | NOTIFY_ACTION) | STOP_OBJECT) | FORGET_TARGET), {FUNCTION_ACTION: self.h_TargetReached, NOTIFY_ACTION: self.n_TargetReached})
        self.bstate.AddEventReaction(ORDER, [TARGET_UNREACHABLE, 0], (((FUNCTION_ACTION | NOTIFY_ACTION) | STOP_OBJECT) | FORGET_TARGET), {FUNCTION_ACTION: self.h_TargetUnreachable, NOTIFY_ACTION: self.n_TargetUnreachable})
        self.bstate.AddEventReaction(ORDER, [PAIN_ORDER, self.h_PainTest], PAIN_OBJECT, 0)
        self.bstate.AddEventReaction(ORDER, [MISS_ORDER, self.h_MissTest], DEFEND_OBJECT, 0)
        self.bstate.AddEventReaction(ORDER, [END_STAND_ANIM], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EndStandAnim})
        self.bstate.AddEventReaction(ORDER, [WAIT_OBJECT], (FUNCTION_ACTION | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_WaitObject, NOTIFY_ACTION: self.n_WaitObject})
        self.bstate.AddEventReaction(ORDER, [RESUME_OBJECT, self.h_ResumeTestAnother], FUNCTION_ACTION | NOTIFY_ACTION, {FUNCTION_ACTION: (self.h_ResumeObject), NOTIFY_ACTION: (self.n_ResumeObject)})
        self.bstate.AddEventReaction(ORDER, [RESUME_OBJECT, self.h_ResumeTestPoint], FUNCTION_ACTION | NOTIFY_ACTION | DIRECT_OBJECT_TO_POINT, {FUNCTION_ACTION: (self.h_ResumeObject), NOTIFY_ACTION: (self.n_ResumeObject)})
        self.bstate.AddEventReaction(ORDER, [RESUME_OBJECT, self.h_ResumeTestMO], FUNCTION_ACTION | NOTIFY_ACTION | DIRECT_OBJECT_TO_CHAR_RUN | CHANGE_STATE, {FUNCTION_ACTION: (self.h_ResumeObject), NOTIFY_ACTION: (self.n_ResumeObject), CHANGE_STATE: 'attack'})
        # Правило отбора задаёт SetRoTCS (CHAR_FUNCTION — радиус агро); правило-ноль
        # здесь не ставится: массив правил append-only, лишнее правило отбирает всё.
        self.bstate.SetRuleOfTargetPointSelection(DONT_SELECT_TARGET, 0)
        self.moving = 0
        # Приказ перемещения игрока в работе (план 10.3).
        self.move_order = 0
        self.loitering = 0
        self.loiter_cnt = 0
        self.wait_target = None
        self.waiting = 0



    def h_StartFunc(self):
        self.parent.SetRoTCS(self.bstate)
        self.stand_anim_cnt = 0



    def h_EndFunc(self):
        self.unit.SA_Reset()
        self.move_order = 0
        self.wait_target = None
        self.waiting = 0
        return



    def h_TestForAlly(self, gobjs):
        if self.parent.mo_Group:
            return 
        i = (len(gobjs) - 1)
        while (i >= 0):
            unit = gobjs[i].GetMentalityPart().GetPythonCharacter()
            if (unit and (unit.ClassName == self.unit.ClassName)):
                self.parent.AttachToGroup_unit(unit)
                break
            i -= 1




    def h_DirectTo_Remote(self, param):
        self.moving = 1
        if not self.parent.grouping_flags & SB_GF_STRONG_CONNEXITY & SB_GF_ONE_GROUP_PER_CLASS:
            self.parent.DetachFromGroup_self()
        # Агро глушится только у юнитов стороны игрока (план 10.3): ИИ-командиры
        # раздают приказы тем же REMOTE_ORDER, и у них подавление ломало бой.
        if not std_brains.IsEnemyOwner(self.unit.Owner):
            self.move_order = 1
            std_brains.AggroLog(self.unit.Name, 'order start')
        return

    def h_DirectTo(self, param):
        self.moving = 1



    def EndMove(self):
        self.moving = 0
        if self.move_order:
            std_brains.AggroLog(self.unit.Name, 'order end')
        self.move_order = 0
        self.loitering = 0
        self.parent.SetRoTCS(self.bstate)
        self.unit.SA_Reset()



    def h_TargetReached(self):
        self.EndMove()


        return

    def u_AttackTarget_Melee(self, unit):
        if self.move_order:
            std_brains.AggroLog(self.unit.Name, 'melee blocked by move_order')
            return
        mtarget = [MO_TARGET, 0, unit.MentalityObject, 0]
        self.ment.SetTarget(mtarget)
        self.ment.SendOrder(DIRECT_TO, [unit.MentalityObject.GetGraphObject(), 0])
        self.ment.SetCurrentBehaviourState(self.parent.bs_attack.bstate)



    def h_EndStandAnim(self, param):
        self.loiter_cnt += 1
        if ((self.loiter_cnt >= self.parent.loiter_frequency) and self.parent.LoiterFunc):
            self.parent.LoiterFunc()
            self.loitering = 1
            self.loiter_cnt = 0
            return 1
        return 0



    def h_WaitObject(self, params = None):
        self.wait_target = params
        self.waiting = 1



    def h_ResumeTestPoint(self, order, params):
        if ((not params) or (not self.wait_target)):
            return 0
        if (self.wait_target[1] != params[0]):
            return 0
        if (self.wait_target[0] != MO_TARGET):
            return 1
        return 0



    def h_ResumeTestMO(self, order, params):
        if ((not params) or (not self.wait_target)):
            return 0
        if (self.wait_target[1] != params[0]):
            return 0
        if (self.wait_target[0] == MO_TARGET):
            return 1
        return 0



    def h_ResumeTestAnother(self, order, params):
        if ((not params) or (not self.wait_target)):
            return 0
        if (self.wait_target[1] == params[0]):
            return 0
        return 1


        return

    def h_ResumeObject(self, params=None):
        self.move_order = 0
        self.wait_target = None
        self.waiting = 0
        return



    def Dest(self):
        StateBase.Dest(self)
        self.bstate = None
        self.wait_target = None
        self.waiting = 0



    def n_WaitObject(self, params = None):
        LOG(self, ('n_WaitObject ' + repr(params)))



    def n_ResumeObject(self, params):
        LOG(self, ('n_ResumeObject ' + repr(params)))


        return

    def n_TargetFound(self, param=None):
        std_brains.AggroLog(self.unit.Name, 'new_target')
        LOG(self, 'h_TargetFound')
        return

    def n_DirectTo_Remote(self, param = None):
        LOG(self, 'n_DirectTo_Remote')



    def n_DirectTo(self, param = None):
        LOG(self, 'n_DirectTo')



    def h_DirectTo_Point(self, param = None):
        self.moving = 1



    def n_DirectTo_Point(self, param = None):
        LOG(self, 'h_DirectTo_Point')



    def n_TargetReached(self, param = None):
        LOG(self, 'n_TargetReached')



    def h_TargetUnreachable(self, param = None):
        self.EndMove()



    def n_TargetUnreachable(self, param = None):
        LOG(self, 'h_TargetUnreachable')



    def n_StopUnit(self, param = None):
        LOG(self, 'n_StopUnit')



    def n_WaitUnit(self, param):
        LOG(self, 'n_WaitUnit')



    def n_ResumeUnit(self, param):
        LOG(self, 'n_ResumeUnit')



class BattleAttack(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'battle_attack'
        self.bstate = self.ment.CreateNewBehaviourState('attack')
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        if (self.parent.remote_ctl_mode < SB_RCM_EXCEPT_ATTACK):
            self.bstate.AddEventReaction(ORDER, [(REMOTE_ORDER | DIRECT_TO), 0], ((LEAVE_COMMAND | NOTIFY_ACTION) | END_STATE), {NOTIFY_ACTION: self.n_DirectTo})
            self.bstate.AddEventReaction(ORDER, [(REMOTE_ORDER | DIRECT_TO_RUN), 0], ((LEAVE_COMMAND | NOTIFY_ACTION) | END_STATE), {NOTIFY_ACTION: self.n_DirectTo})
        if (self.parent.attack_type == SB_AT_KICK_N_RUN):
            self.bstate.AddEventReaction(ORDER, [DIRECT_TO_POINT, 0], FUNCTION_ACTION | NOTIFY_ACTION | DIRECT_OBJECT_TO_POINT_RUN, {FUNCTION_ACTION: (self.h_DirectTo_Point), NOTIFY_ACTION: (self.n_DirectTo_Point)})
            self.bstate.AddEventReaction(TARGET_REACHED, [POINT_TARGET, [0, 0]], FUNCTION_ACTION | NOTIFY_ACTION | STOP_OBJECT, {FUNCTION_ACTION: (self.h_TargetReached_Point), NOTIFY_ACTION: (self.n_TargetReached_Point)})
        self.bstate.AddEventReaction(ORDER, [DIRECT_TO, 0], NOTIFY_ACTION | DIRECT_OBJECT_TO_CHAR_RUN, {NOTIFY_ACTION: (self.n_DirectTo_Run)})
        self.bstate.AddEventReaction(TARGET_HIDE, [MO_TARGET], STOP_OBJECT | NOTIFY_ACTION, {NOTIFY_ACTION: (self.n_TargetHide)})
        # Цель держится до смерти (план 10.4).
        self.bstate.AddEventReaction(ORDER, [TARGET_UNREACHABLE, 0], STOP_OBJECT | NOTIFY_ACTION, {NOTIFY_ACTION: (self.n_TargetUnreachable)})
        self.bstate.AddEventReaction(NEAR_ATTACK_DISTANCE, [MO_TARGET, [0, 0]], STOP_OBJECT | NOTIFY_ACTION | CHANGE_STATE, {CHANGE_STATE: 'kick', NOTIFY_ACTION: (self.n_NearAttackDistance)})
        self.bstate.AddEventReaction(TARGET_MOVED, [MO_TARGET, [0, 15]], DIRECT_OBJECT_TO_CHAR | NOTIFY_ACTION, {NOTIFY_ACTION: (self.n_TargetMoved)})
        self.bstate.AddEventReaction(ORDER, [MISS_ORDER, self.h_MissTest], DEFEND_OBJECT | NOTIFY_ACTION | CHANGE_STATE, {NOTIFY_ACTION: (self.n_Miss), CHANGE_STATE: 'locked_anim'})
        self.bstate.AddEventReaction(ORDER, [PAIN_ORDER, self.h_PainTest], ((PAIN_OBJECT | NOTIFY_ACTION) | CHANGE_STATE), {NOTIFY_ACTION: self.n_Pain, CHANGE_STATE: 'locked_anim'})
        self.bstate.AddEventReaction(ORDER, [TARGET_DIED, 0], (((FORGET_TARGET | STOP_OBJECT) | END_STATE) | NOTIFY_ACTION), {NOTIFY_ACTION: self.n_TargetDied})
        self.bstate.AddEventReaction(ORDER, [WAIT_OBJECT], ((LEAVE_COMMAND | STOP_OBJECT) | END_STATE), {})
        self.bstate.SetRuleOfTargetCharSelection(DONT_SELECT_TARGET, 0)
        self.bstate.SetRuleOfTargetPointSelection(DONT_SELECT_TARGET, 0)
        if (not (self.parent.target_selection_flags & SB_TSF_EXCEPT_ATTACK)):
            self.bstate.AddEventReaction(NEW_TARGET_FOUND, [MO_TARGET], ((LEAVE_COMMAND | NOTIFY_ACTION) | END_STATE), {NOTIFY_ACTION: self.n_TargetFound})
        self.mo_target = None
        self.mirror_att_mod = 0



    def h_Damage(self, params):
        LOG(self, 'h_Damage')
        return 



    def h_StartFunc(self):
        target = self.ment.GetTarget()
        std_brains.AggroLog(self.unit.Name, 'attack start target=' + str(target != None))
        if not self.parent.target_selection_flags & SB_TSF_EXCEPT_ATTACK:
            self.parent.SetRoTCS(self.bstate)
        if target == None:
            self.ment.EndCurrentBehaviourState()
            return 
        self.mo_target = target[2]
        if (self.unit.FLAGS & BCF_MIRROR_LIKE_ATT_CALC):
            self.mirror_att_mod = self.mo_target.GetPythonCharacter().GetStat('ATT')
            self.unit.ChangeStatMod('ATT', self.mirror_att_mod)
        return 



    def h_EndFunc(self):
        if (self.unit.FLAGS & BCF_MIRROR_LIKE_ATT_CALC):
            self.unit.ChangeStatMod('ATT', -self.mirror_att_mod)
        self.mo_target = None
        StateBase.h_EndFunc(self)



    def h_DirectTo_Point(self, param):
        self.bstate.SetRuleOfTargetCharSelection(DONT_SELECT_TARGET, 0)



    def h_TargetReached_Point(self, param = 0):
        go = self.mo_target.GetGraphObject()
        self.ment.SendOrder(DIRECT_TO, [go, 0])
        if (not (self.parent.target_selection_flags & SB_TSF_EXCEPT_ATTACK)):
            self.parent.SetRoTCS(self.bstate)



    def n_DirectTo_Point(self, param = None):
        LOG(self, 'n_DirectTo_Point')



    def n_TargetHide(self, param = None):
        LOG(self, 'n_TargetHide')



    def n_TargetUnreachable(self, param = None):
        LOG(self, 'n_TargetUnreachable')



    def n_TargetDied(self, param = None):
        LOG(self, 'n_TargetDied')



    def n_TargetFound(self, param = None):
        LOG(self, 'n_TargetFound')



    def n_NearAttackDistance(self, param = None):
        LOG(self, 'n_NearAttackDistance')



    def n_DirectTo_Run(self, param = None):
        LOG(self, 'n_DirectTo_Run')



    def n_DirectTo(self, param = None):
        LOG(self, 'n_DirectTo (remote)')



    def n_TargetReached_Point(self, param = None):
        LOG(self, 'h_TargetReached_Point')



    def n_Miss(self, param = 0):
        LOG(self, 'n_Miss')



    def n_Pain(self, param = 0):
        LOG(self, 'n_Pain')



    def n_TargetMoved(self, param = 0):
        LOG(self, 'n_TargetMoved')



    def Dest(self):
        StateBase.Dest(self)
        self.bstate = None
        self.mo_target = None
        self.wait_target = None
        self.waiting = 0



class BattleKick(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'battle_kick'
        self.bstate = self.ment.CreateNewBehaviourState('kick')
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        if (self.parent.remote_ctl_mode < SB_RCM_EXCEPT_ATTACK):
            self.bstate.AddEventReaction(ORDER, [(REMOTE_ORDER | DIRECT_TO), 0], ((LEAVE_COMMAND | FUNCTION_ACTION) | NOTIFY_ACTION), {NOTIFY_ACTION: self.n_DirectTo, FUNCTION_ACTION: self.h_DirectTo})
            self.bstate.AddEventReaction(ORDER, [(REMOTE_ORDER | DIRECT_TO_RUN), 0], ((LEAVE_COMMAND | FUNCTION_ACTION) | NOTIFY_ACTION), {NOTIFY_ACTION: self.n_DirectTo, FUNCTION_ACTION: self.h_DirectTo})
        self.bstate.AddEventReaction(ORDER, [KICK_ORDER, self.CheckNormal], KICK_TARGET_NORMAL, 0)
        self.bstate.AddEventReaction(ORDER, [KICK_ORDER, self.CheckHi], KICK_TARGET_HI, 0)
        self.bstate.AddEventReaction(ORDER, [KICK_ORDER, self.CheckLow], KICK_TARGET_LOW, 0)
        self.bstate.AddEventReaction(ORDER, [END_ANIM_ORDER, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EndKick})
        self.bstate.AddEventReaction(ORDER, [MISS_ORDER, 0], (LEAVE_COMMAND | NOTIFY_ACTION), {NOTIFY_ACTION: self.n_Miss})
        self.bstate.AddEventReaction(ORDER, [PAIN_ORDER, 0], (LEAVE_COMMAND | NOTIFY_ACTION), {NOTIFY_ACTION: self.n_Pain})
        self.bstate.AddEventReaction(ORDER, [TARGET_DIED, 0], ((LEAVE_COMMAND | FUNCTION_ACTION) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_TargetDied, NOTIFY_ACTION: self.n_TargetDied})
        self.bstate.SetRuleOfTargetCharSelection(DONT_SELECT_TARGET, 0)
        self.bstate.SetRuleOfTargetPointSelection(DONT_SELECT_TARGET, 0)
        if (not (self.parent.target_selection_flags & SB_TSF_EXCEPT_ATTACK)):
            self.bstate.AddEventReaction(NEW_TARGET_FOUND, [MO_TARGET], ((LEAVE_COMMAND | FUNCTION_ACTION) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_TargetFound, NOTIFY_ACTION: self.n_TargetFound})
        self.can_kick = 1
        self.can_kick_timer = None
        self.mo_target = None



    def CanKickTimer(self, tid):
        self.can_kick = 1
        self.can_kick_timer = None
        gamemodule.KillTimer(tid)



    def CheckNormal(self, params1, params2):
        if (self.mo_target.GetFlyingHeight() == self.ment.GetFlyingHeight()):
            return 1
        return 0



    def CheckHi(self, params1, params2):
        if (self.mo_target.GetFlyingHeight() > self.ment.GetFlyingHeight()):
            return 1
        return 0



    def CheckLow(self, params1, params2):
        if (self.mo_target.GetFlyingHeight() < self.ment.GetFlyingHeight()):
            return 1
        return 0



    def h_StartFunc(self):
        self.exit_to_suspend = 0
        if (not (self.parent.target_selection_flags & SB_TSF_EXCEPT_ATTACK)):
            self.parent.SetRoTCS(self.bstate)
        target = self.ment.GetTarget()
        if ((target[2].GetGraphObject() == None) or (target[2].GetPythonCharacter() == None)):
            self.ment.SendOrder(KICK_ORDER, 0)
            self.exit_to_suspend = 1
            return 
        self.mo_target = target[2]
        self.ment.LookAtPoint(target[3][0], 0, target[3][2])
        if (self.can_kick == 0):
            self.h_EndKick(None)
            return 
        self.can_kick = 0
        self.can_kick_timer = gamemodule.SetTimer(g_defs.g_attack_duration, self.CanKickTimer)
        self.ment.SendOrder(KICK_ORDER, 0)
        self.unit.KickObject(self.mo_target.GetPythonCharacter())



    def h_EndFunc(self):
        self.mo_target = None
        StateBase.h_EndFunc(self)



    def h_TargetDied(self, param = 0):
        self.exit_to_suspend = 1



    def n_TargetDied(self, param = 0):
        LOG(self, 'h_TargetDied')



    def h_DirectTo(self, target):
        self.exit_to_suspend = 1



    def n_DirectTo(self, params = None):
        LOG(self, 'n_DirectTo (remote)')



    def h_TargetFound(self):
        self.exit_to_suspend = 1



    def n_TargetFound(self):
        LOG(self, 'n_TargetFound')



    def h_EndKick(self, target):
        go = None
        if self.mo_target:
            go = self.mo_target.GetGraphObject()
            if (go == 0):
                self.exit_to_suspend = 1
        if self.exit_to_suspend:
            self.ment.SetTarget(0)
            self.ment.EndCurrentBehaviourState()
            return 
        if (self.parent.attack_type == SB_AT_KICK_N_RUN):
            vec = self.unit.CalcCounterVector(self.mo_target.GetPythonCharacter(), self.parent.kick_n_run_dist)
            mypos = self.ment.GetPosition()
            mypos[0] += vec[0]
            mypos[2] += vec[1]
            self.ment.EndCurrentBehaviourState()
            self.ment.SetTarget(0)
            self.ment.SendOrder(DIRECT_TO_POINT, [None, mypos])
            return 
        self.ment.SendOrder(DIRECT_TO, [go, 0])
        self.ment.EndCurrentBehaviourState()



    def n_Miss(self, param = 0):
        LOG(self, 'n_Miss')
        return 



    def n_Pain(self, param = 0):
        LOG(self, 'n_Pain')
        return 



    def Dest(self):
        self.mo_target = None
        self.bstate = None
        if self.can_kick_timer:
            gamemodule.KillTimer(self.can_kick_timer)
        self.can_kick_timer = None
        self.can_kick = 1
        StateBase.Dest(self)



class LockedAnim(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'locked_anim'
        self.bstate = self.ment.CreateNewBehaviourState('locked_anim')
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [END_ANIM_ORDER, 0], END_STATE, 0)
        self.bstate.AddEventReaction(ALL_COMMANDS, 0, LEAVE_COMMAND, 0)
        self.bstate.SetRuleOfTargetCharSelection(DONT_SELECT_TARGET, 0)
        self.bstate.SetRuleOfTargetPointSelection(DONT_SELECT_TARGET, 0)



    def h_StartFunc(self):
        return 



    def h_EndFunc(self):
        StateBase.h_EndFunc(self)



class SCMain2(BattleMain):

    def __init__(self, parent):
        BattleMain.__init__(self, parent)
        self.name = 'sc_main2'
        self.bstate.AddEventReaction(ORDER, [B_ENEMY_FOUND, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EnemyFound})
        self.disengage_cnt = 0
        self.enchant_cnt = 0



    def h_StartFunc(self):
        BattleMain.h_StartFunc(self)


        return

    def h_EnemyFound(self, objs):
        if self.move_order:
            std_brains.AggroLog(self.unit.Name, 'sc_enemy blocked by move_order')
            return
        target = objs[0]
        unit = target.GetMentalityPart().GetPythonCharacter()
        if not self.parent.IsWithinAggro(unit):
            std_brains.AggroLog(self.unit.Name, 'sc_enemy far')
            return
        if self.disengage_cnt >= self.parent.disengage_tries_max:
            self.u_AttackTarget_Melee(target.GetMentalityPart().GetPythonCharacter())
            self.disengage_cnt = 0
            return 
        if (self.unit.CalcDist(unit) < self.parent.danger_range):
            self.disengage_cnt += 1
            vec = self.unit.CalcCounterVector(unit, self.parent.disengage_dist)
            mypos = self.ment.GetPosition()
            mypos[0] += vec[0]
            mypos[2] += vec[1]
            self.ment.SendOrder(DIRECT_TO_POINT, [None, mypos])
            return 
        if (self.ment.GetCurrentBehaviourState() != self.bstate):
            return 
        if (self.parent.bs_magic_attack2.Test() > 0.0):
            self.u_RewindStateStack()
            self.parent.bs_magic_attack2.Run()
            return 
        return 



    def h_EndStandAnim(self, param):
        self.enchant_cnt += 1
        if (operator.mod(self.enchant_cnt, 10) == 0):
            if self.parent.bs_enchant2.Test([self.unit], 0.5):
                self.parent.bs_enchant2.Run()
                return 
            return 
        self.parent.u_ObtainManaLimit()
        BattleMain.h_EndStandAnim(self, param)
        return 



class SCCastSpell2(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'sc_castspell2'
        self.bstate = self.ment.CreateNewBehaviourState(self.name)
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.SetRuleOfTargetCharSelection(DONT_SELECT_TARGET, 0)
        self.bstate.SetRuleOfTargetPointSelection(DONT_SELECT_TARGET, 0)
        CR = self.unit.GetCastRange()
        OoCRT = self.parent.OoCRT
        if (self.parent.remote_ctl_mode < SB_RCM_EXCEPT_ATTACK):
            self.bstate.AddEventReaction(ORDER, [(REMOTE_ORDER | DIRECT_TO), 0], (((LEAVE_COMMAND | FUNCTION_ACTION) | NOTIFY_ACTION) | END_STATE), {FUNCTION_ACTION: self.h_DirectToRemote, NOTIFY_ACTION: self.n_DirectToRemote})
            self.bstate.AddEventReaction(ORDER, [(REMOTE_ORDER | DIRECT_TO_RUN), 0], (((LEAVE_COMMAND | FUNCTION_ACTION) | NOTIFY_ACTION) | END_STATE), {FUNCTION_ACTION: self.h_DirectToRemote, NOTIFY_ACTION: self.n_DirectToRemote})
        self.bstate.AddEventReaction(ORDER, [CAST_START, 0], (START_CAST | NOTIFY_ACTION), {NOTIFY_ACTION: self.n_CastStart})
        self.bstate.AddEventReaction(ORDER, [CAST_END, 0], (((FORGET_TARGET | FUNCTION_ACTION) | END_STATE) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_EndSpell, NOTIFY_ACTION: self.n_CastEnd})
        self.bstate.AddEventReaction(ORDER, [BREAK_CAST, 0], (((FORGET_TARGET | FUNCTION_ACTION) | END_STATE) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_BreakCast, NOTIFY_ACTION: self.n_BreakCast})
        self.bstate.AddEventReaction(ORDER, [TARGET_DIED, 0], ((((LEAVE_COMMAND | FORGET_TARGET) | FUNCTION_ACTION) | END_STATE) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_BreakCast, NOTIFY_ACTION: self.n_TargetDied})
        self.bstate.AddEventReaction(TARGET_MOVED, [MO_TARGET, [0, (CR - OoCRT)]], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_CorrectTarget})
        self.bstate.AddEventReaction(TARGET_MOVED, [MO_TARGET, [(CR - OoCRT), CR]], ((FORGET_TARGET | FUNCTION_ACTION) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_EndSpell, NOTIFY_ACTION: self.n_OutOfRange})
        self.bstate.AddEventReaction(TARGET_HIDE, [MO_TARGET], (((FORGET_TARGET | FUNCTION_ACTION) | NOTIFY_ACTION) | END_STATE), {FUNCTION_ACTION: self.h_BreakCast, NOTIFY_ACTION: self.n_BreakCast})
        self.bstate.AddEventReaction(ORDER, [PAIN_ORDER, self.h_PainTest], ((FORGET_TARGET | FUNCTION_ACTION) | END_STATE), {FUNCTION_ACTION: self.h_BreakCast})
        self.bstate.AddEventReaction(ORDER, [B_ENEMY_FOUND, 0], ((LEAVE_COMMAND | FUNCTION_ACTION) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_EnemyFound, NOTIFY_ACTION: self.n_EnemyFound})
        self.bstate.AddEventReaction(ORDER, [ADD_CRITTER, 0], (LEAVE_COMMAND | NOTIFY_ACTION), {NOTIFY_ACTION: self.h_AddCritter})
        self.EndCastLatch = 0
        self.spell_breaked = 0
        self.ai_cast = None
        self._ea_EndCastEnd = None



    def h_AddCritter(self, param = 0):
        LOG(self, 'h_AddCritter()')



    def Dest(self):
        self.ai_cast = None
        self._ea_EndCastEnd = None
        self.bstate = None
        StateBase.Dest(self)



    def Run(self, ai_cast):
        self.ai_cast = ai_cast
        self.ment.SetCurrentBehaviourState(self.bstate)
        self.ment.GetGraphObject().SetChainLooped((LOOPFLAG | CAST), 0, 1)
        self.ment.StopObject()
        self.ment.GetGraphObject().SetAnimationChain(STAND)
        if self.ai_cast.u_target:
            target = [MO_TARGET, 0, self.ai_cast.u_target.MentalityObject, 0]
            self.ment.SetTarget(target)
        else:
            self.ment.SetTarget(0)
        self.ai_cast.StartCast()



    def h_StartFunc(self):
        self.EndCastLatch = 0
        self.parent.SetRoTCS(self.bstate)



    def h_EndFunc(self):
        self.h_EndSpell()
        if self.spell_breaked:
            self.ment.SendOrder(SPELL_FINISHED_BREAK, 0)
        else:
            self.ment.SendOrder(SPELL_FINISHED_OK, 0)
        self.ment.SendOrder(SPELL_FINISHED, 0)
        self.ment.ClearEventsQueue(CAST_ORDERS, 1)
        StateBase.h_EndFunc(self)



    def h_CorrectTarget(self, param = 0):
        if (self.unit.NowCasting == None):
            return 
        if self.ai_cast.Update:
            self.ai_cast.Update()



    def ea_EndCastEnd(self, i):
        self.ment.GetGraphObject().RemoveEndAnimationFunction((ENDFLAG | CAST), 0, self._ea_EndCastEnd)
        self.ment.GetGraphObject().SetChainLooped((LOOPFLAG | CAST), 0, 1)
        self._ea_EndCastEnd = None



    def h_EndSpell(self, param = 0):
        if self.EndCastLatch:
            return 
        self.ment.GetGraphObject().SetChainLooped((LOOPFLAG | CAST), 0, 0)
        self._ea_EndCastEnd = self.ea_EndCastEnd
        self.ment.GetGraphObject().AddEndAnimationFunction((ENDFLAG | CAST), 0, self._ea_EndCastEnd)
        self.EndCastLatch = 1
        if self.unit.NowCasting:
            if self.ai_cast.Update:
                self.ai_cast.Update()
            self.unit.NowCasting.FinishCast()



    def h_BreakCast(self, param = 0):
        if self.EndCastLatch:
            return 
        self.ment.GetGraphObject().SetChainLooped((LOOPFLAG | CAST), 0, 0)
        self._ea_EndCastEnd = self.ea_EndCastEnd
        self.ment.GetGraphObject().AddEndAnimationFunction((ENDFLAG | CAST), 0, self._ea_EndCastEnd)
        self.EndCastLatch = 1
        self.spell_breaked = 1
        if self.unit.NowCasting:
            self.unit.NowCasting.BreakCast()



    def n_BreakCast(self, param = 0):
        LOG(self, ((((('h_BreakCast: NowCasting=' + str(self.unit.NowCasting)) + ', EndCastLatch=') + str(self.EndCastLatch)) + ', _ea_EndCastEnd=') + str(self._ea_EndCastEnd)))



    def h_DirectToRemote(self, target):
        self.h_BreakCast()



    def n_DirectToRemote(self, target):
        LOG(self, 'h_DirectToRemote')



    def h_EnemyFound(self, objs):
        if self.unit.NowCasting:
            if self.ai_cast.Update:
                self.ai_cast.Update()



    def n_EnemyFound(self, objs):
        LOG(self, 'n_EnemyFound()')



    def n_OutOfRange(self, param = 0):
        LOG(self, 'n_OutOfRange')



    def n_Pain(self, param = 0):
        LOG(self, 'n_Pain')



    def n_CastStart(self, param = 0):
        LOG(self, ('n_CastStart(): message queue:\n' + str(self.ment.PrintMessageQueue())))



    def n_CastEnd(self, param = 0):
        LOG(self, 'n_CastEnd')



    def n_TargetDied(self, params = None):
        LOG(self, 'n_TargetDied')



class SCMagicAttack2(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'sc_magic_attack2'
        self.bstate = self.ment.CreateNewBehaviourState(self.name)
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [SPELL_FINISHED, 0], END_STATE, 0)
        self.ai_cast = None



    def Dest(self):
        self.ai_cast = None
        self.bstate = None
        StateBase.Dest(self)



    def h_EndFunc(self):
        self.ai_cast = None
        StateBase.h_EndFunc(self)
        self.ment.SendOrder(STATE_ENDED, 0)



    def Test(self, u_target = None):
        eff_max = -1.0
        ofs_spells = self.unit.OverlandCharacter.AvailableSpells['Known']
        i = (len(ofs_spells) - 1)
        while (i >= 0):
            if (ofs_spells[i].ParentSpell.Flags & (SF2_INSTANT_BAD | SF2_ENCHANT_BAD_ZONE)):
                eff = ofs_spells[i].AICast.Test()
                if (eff > eff_max):
                    eff_max = eff
                    self.ai_cast = ofs_spells[i].AICast
            i -= 1

        if (eff_max > 0.0):
            return eff_max
        i = (len(ofs_spells) - 1)
        while (i >= 0):
            if (ofs_spells[i].ParentSpell.Flags & SF2_ENCHANT_BAD):
                j = (len(self.parent.l_venemys) - 1)
                while (j >= 0):
                    if ofs_spells[i].AICast.E_Test(10, self.parent.l_venemys[j].GetMentalityPart().GetPythonCharacter()):
                        self.ai_cast = ofs_spells[i].AICast
                        return 1.0
                    j -= 1

            i -= 1

        i = (len(ofs_spells) - 1)
        while (i >= 0):
            if (ofs_spells[i].ParentSpell.Flags & SF2_SPECIAL):
                if (ofs_spells[i].AICast.Test() == 1):
                    self.ai_cast = ofs_spells[i].AICast
                    return 1.0
            i -= 1

        return eff_max



    def Run(self):
        if (self.ai_cast == None):
            return 
        self.ment.SetCurrentBehaviourState(self.bstate)
        self.parent.bs_cast2.Run(self.ai_cast)



class SCEnchant2(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'sc_enchant2'
        self.bstate = self.ment.CreateNewBehaviourState(self.name)
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [SPELL_FINISHED, 0], END_STATE, 0)
        self.ai_cast = None



    def Dest(self):
        self.ai_cast = None
        self.bstate = None
        StateBase.Dest(self)



    def h_EndFunc(self):
        self.ai_cast = None
        StateBase.h_EndFunc(self)



    def Test(self, group, allowed_upkeep_percent):
        allowed_upkeep = (allowed_upkeep_percent * self.parent.u_GetMaxUpkeep())
        upkeep_left = allowed_upkeep
        self.ai_cast = None
        i = (len(group) - 1)
        while (i >= 0):
            j = (len(group[i].ActiveSpells) - 1)
            while (j >= 0):
                if (group[i].ActiveSpells[j].CasterCharacter == self.unit):
                    res = group[i].ActiveSpells[j].Result
                    if res:
                        upkeep_left -= res[(len(res) - 1)]
                j -= 1

            i -= 1

        if (upkeep_left < 0):
            return 0
        ins_healer = self.parent.u_FindSpell((SF2_INSTANT_GOOD | SF2_HEAL))
        if ins_healer:
            j = (len(group) - 1)
            while (j >= 0):
                if (((float(group[j].GetStat('HP')) / float(group[j].GetStat('MAX_HP'))) < 0.25) and ins_healer.AICast.Test(group[j])):
                    self.ai_cast = ins_healer.AICast
                    return 1
                j -= 1

        ofs_spells = self.unit.OverlandCharacter.AvailableSpells['Known']
        i = (len(ofs_spells) - 1)
        while (i >= 0):
            if (ofs_spells[i].ParentSpell.Flags & SF2_ENCHANT_GOOD):
                j = (len(group) - 1)
                while (j >= 0):
                    if ofs_spells[i].AICast.E_Test(upkeep_left, group[j]):
                        self.ai_cast = ofs_spells[i].AICast
                        return 1
                    j -= 1

            i -= 1

        return 0



    def Run(self):
        if (self.ai_cast == None):
            return 
        self.ment.SetCurrentBehaviourState(self.bstate)
        self.parent.bs_cast2.Run(self.ai_cast)



class SCSummon2(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'sc_summon2'
        self.bstate = self.ment.CreateNewBehaviourState(self.name)
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [SPELL_FINISHED, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_SpellFinished})
        self.ai_cast = None



    def Dest(self):
        self.ai_cast = None
        self.bstate = None
        StateBase.Dest(self)



    def h_EndFunc(self):
        self.ai_cast = None
        StateBase.h_EndFunc(self)



    def Test(self, allowed_upkeep, c_flags, s_flags, pt = None):
        ret = None
        rstr = None
        self.ai_cast = None
        ofs_spells = self.unit.OverlandCharacter.AvailableSpells['Known']
        i = (len(ofs_spells) - 1)
        while (i >= 0):
            if (ofs_spells[i].ParentSpell.Flags & SF2_SUMMON):
                if ofs_spells[i].AICast.S_Test(allowed_upkeep, c_flags, s_flags):
                    self.ai_cast = ofs_spells[i].AICast
                    return 1
            i -= 1

        return 0



    def Run(self):
        if (self.ai_cast == None):
            return 
        self.ment.SetCurrentBehaviourState(self.bstate)
        self.parent.bs_cast2.Run(self.ai_cast.S_GetAICastInterface())



    def h_SpellFinished(self, param = 0):
        if self.ai_cast:
            self.ai_cast.S_SpellFinished()
        else:
            i = 0
        if (self.ai_cast and self.ai_cast.S_GetAICastInterface()):
            self.parent.bs_cast2.Run(self.ai_cast.S_GetAICastInterface())
        else:
            self.ment.EndCurrentBehaviourState()



class SCRespawnLock(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'sc_respawn_lock'
        self.bstate = self.ment.CreateNewBehaviourState('respawn_lock')
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [SPELL_FINISHED, 0], END_STATE, 0)
        self.respawn = None



    def Dest(self):
        self.respawn = None
        self.bstate = None
        StateBase.Dest(self)



    def Test(self, respawn):
        self.respawn = respawn
        r_locker = self.parent.u_FindSpell(SF2_RESP_LOCKER)
        if (r_locker == None):
            return 0
        locker = spell_functionality_base.FindSpell(r_locker.ParentSpell.KeyName, self.respawn)
        if locker:
            return 0
        else:
            i = 0
            ro_mst = gamemodule.GameManager_GetScene().GetBDomainObject().RespawnsOwner.BattleUnit.GetStat('MST')
            while (i <= r_locker.GetKnownLevel()):
                result = r_locker.ParentSpell.GetCastResult(i, None)
                if (result[1] >= ro_mst):
                    pll = (i - 1)
                    break
                i += 1

            if (i > r_locker.GetKnownLevel()):
                return 0
            if (pll == -1):
                pll = 0
        if (pll >= r_locker.GetKnownLevel()):
            return 0
        else:
            self.rul = (pll + 1)
        self.respawn = respawn
        return 1



    def Run(self):
        target_go = self.respawn.MentalityObject.GetGraphObject()
        r_locker = self.parent.u_FindSpell(SF2_RESP_LOCKER)
        if self.parent.u_CanCastDist(self.respawn):
            self.ment.SetCurrentBehaviourState(self.bstate)
            r_locker.AICast.Setup(self.respawn, self.rul)
            self.parent.bs_cast2.Run(r_locker.AICast)
        else:
            mypos = self.respawn.GetPosition()
            self.ment.SendOrder(DIRECT_TO_POINT, [None, [mypos[0], 0, mypos[2]]])
        return 



class SCRespawnUnlock(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'sc_respawn_unlock'
        self.bstate = self.ment.CreateNewBehaviourState(self.name)
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [SPELL_FINISHED, 0], END_STATE, 0)
        self.respawn = None
        self.rul = -1



    def Dest(self):
        self.respawn = None
        StateBase.Dest(self)



    def h_EndFunc(self):
        StateBase.h_EndFunc(self)



    def Test(self, respawn):
        self.respawn = respawn
        r_locker = self.parent.u_FindSpell(SF2_RESP_LOCKER)
        if (r_locker == None):
            return 0
        locker = spell_functionality_base.FindSpell(r_locker.ParentSpell.KeyName, self.respawn)
        if locker:
            pll = locker.SpellLevel
        else:
            return 0
        if (pll >= r_locker.GetKnownLevel()):
            return 0
        else:
            self.rul = (pll + 1)
        self.respawn = respawn
        return 1



    def Run(self):
        target_go = self.respawn.MentalityObject.GetGraphObject()
        r_locker = self.parent.u_FindSpell(SF2_RESP_LOCKER)
        if self.parent.u_CanCastDist(self.respawn):
            self.ment.SetCurrentBehaviourState(self.bstate)
            r_locker.AICast.Setup(self.respawn, self.rul)
            self.parent.bs_cast2.Run(r_locker.AICast)
        else:
            mypos = self.respawn.GetPosition()
            self.ment.SendOrder(DIRECT_TO_POINT, [None, [mypos[0], 0, mypos[2]]])
        return 



class CCMain_def(SCMain2):

    def __init__(self, parent):
        SCMain2.__init__(self, parent)
        self.name = 'cc_main_def'
        self.bstate.AddEventReaction(ORDER, [B_PLAYER_FOUND, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_PlayerFound})
        self.bstate.AddEventReaction(ORDER, [B_ENEMY_FOUND, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EnemyFound})
        self.bstate.AddEventReaction(ORDER, [B_UNIT_LOST, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_UnitLost})
        self.bstate.AddEventReaction(ORDER, [B_DAMAGE, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_Damage})
        self.bstate.AddEventReaction(ORDER, [STATE_ENDED, 0], (FUNCTION_ACTION | NOTIFY_ACTION), {FUNCTION_ACTION: self.Think, NOTIFY_ACTION: self.n_StateEnded})
        self.bstate.AddEventReaction(ORDER, [END_STAND_ANIM], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EndStandAnim})
        self.bstate.AddEventReaction(ORDER, [B_RESPAWN_FOUND, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_RespawnFound})
        self.bstate.AddEventReaction(ORDER, [B_RESPAWN_LOCKED, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_RespawnLocked})
        self.DCTrap = gamemodule.GameManager_GetScene().GetBDomainObject().DCTrap
        self.CurState = CCS2_MAIN



    def Dest(self):
        self.DCTrap = None
        SCMain2.Dest(self)



    def h_StartFunc(self):
        SCMain2.h_StartFunc(self)
        return 



    def h_PlayerFound(self, unit):
        if ((gamemodule.GetTime() - self.parent.LastTimePlayerFound) < 2.0):
            return 
        self.parent.LastTimePlayerFound = gamemodule.GetTime()
        p_pos = unit.GetPosition()
        self.parent.LastPlayerPos = p_pos
        if (self.unit.CalcDist_Point(p_pos) < (self.unit.GetVisibilityRange() * self.parent.player_attack_activation_dist)):
            self.parent.u_MoveGroup_Free(p_pos, self.parent.cl_escort)
            if ((self.CurState < CCS2_CAST) and (self.parent.bs_magic_attack2.Test() > 0.0)):
                self.u_RewindStateStack()
                self.CurState = CCS2_CAST
                self.parent.bs_magic_attack2.Run()
                return 
            if ((self.CurState < CCS2_CAST) and (whrandom.random() < self.parent.melee_contingency)):
                self.u_RewindStateStack()
                self.CurState = CCS2_MELEE
                self.u_AttackTarget_Melee(unit)
                return 


        return

    def h_EnemyFound(self, objs):
        if self.move_order:
            return
        unit = objs[0].GetMentalityPart().GetPythonCharacter()
        if not self.parent.IsWithinAggro(unit):
            return
        self.parent.u_MoveGroup_Free(unit.GetPosition(), self.parent.cl_escort)
        if self.CurState < CCS2_MELEE and self.unit.CalcDist_Point(unit.GetPosition()) < self.parent.danger_range:
            self.u_RewindStateStack()
            self.CurState = CCS2_MELEE
            self.u_AttackTarget_Melee(unit)
            return 
        if ((self.CurState < CCS2_CAST) and (self.parent.bs_magic_attack2.Test() > 0.0)):
            self.u_RewindStateStack()
            self.CurState = CCS2_CAST
            self.parent.bs_magic_attack2.Run()
            return 
        if ((self.CurState < CCS2_CAST) and (whrandom.random() < self.parent.melee_contingency)):
            self.u_RewindStateStack()
            self.CurState = CCS2_MELEE
            self.u_AttackTarget_Melee(unit)
            return 



    def h_UnitLost(self, params):
        return 



    def h_Damage(self, params):
        hp_left = (float(self.unit.GetStat('HP')) / self.unit.GetStat('MAX_HP'))
        if ((self.CurState < CCS2_DISENGAGE) and (hp_left < self.parent.HP_danger_limit)):
            self.u_RewindStateStack()
            self.CurState = CCS2_DISENGAGE
            self.ment.SendOrder(DIRECT_TO_POINT, [None, self.parent.bdomain.DomCenter[0]])
            self.parent.u_ConsolidateEscortForces()
            self.parent.u_MoveGroup_Free(self.parent.bdomain.DomCenter[0], self.parent.cl_escort)
            return 
        if ((self.CurState < CCS2_MELEE) and params[1]):
            self.u_RewindStateStack()
            self.CurState = CCS2_MELEE
            self.u_AttackTarget_Melee(params[1])
            return 



    def Think(self, params = 0):
        self.CurState = CCS_NONE
        if ((gamemodule.GetTime() - self.parent.LastTimePlayerFound) > 20.0):
            self.parent.LastPlayerPos = []
            self.parent.LastTimePlayerFound = gamemodule.GetTime()
        if ((self.parent.u_CheckEscortUpkeep() > 0.75) and self.CheckRespawns()):
            return 
        if ((self.parent.protect_dc == 0) and (battlecharacters.TwoPointDistance2(self.parent.bdomain.DomCenter[0], self.unit.GetPosition()) > (self.DCTrap.dc_range - 0.5))):
            self.ment.SendOrder(DIRECT_TO_POINT, [None, self.parent.bdomain.DomCenter[0]])
            self.parent.u_MoveGroup_Free(self.parent.bdomain.DomCenter[0], self.parent.cl_escort)
            return 
        if self.parent.bs_summon_imp.Test(self.parent.messengers_upkeep):
            self.CurState = CCS2_CAST
            self.parent.bs_summon_imp.Run()
            return 
        if self.parent.bs_recon.Run(self.parent.recon_upkeep):
            self.CurState = CCS2_CAST
            return 
        if self.parent.bs_enchant2.Test([self.unit], 0.10000000000000001):
            self.CurState = CCS2_CAST
            self.parent.bs_enchant2.Run()
            return 
        if self.parent.bs_FBG.Run(self.parent.escort_upkeep, self.parent.cl_escort):
            self.CurState = CCS2_CAST
            return 
        critters = []
        i = (len(self.parent.cl_escort) - 1)
        while (i >= 0):
            if (not self.parent.u_IsUnitBusy(self.parent.cl_escort[i])):
                critters.append(self.parent.cl_escort[i])
            i -= 1

        self.parent.u_MoveGroup(self.unit.GetPosition(), critters)
        if self.parent.protect_dc:
            self.parent.u_MoveToWithEscort(self.parent.bdomain.DomCenter[0], 1)
            return 
        return 



    def CheckRespawns(self):
        if (len(self.DCTrap.respawns_list) == 0):
            return 0
        i = (len(self.DCTrap.respawns_list) - 1)
        n_locked = 0
        while (i >= 0):
            if self.DCTrap.respawns_list[i].IsLocked():
                n_locked += 1
            i -= 1

        if ((float(n_locked) / len(self.DCTrap.respawns_list)) < self.parent.def_rlock_limit):
            return 0
        min_dist = 1000
        closest = -1
        i = (len(self.DCTrap.respawns_list) - 1)
        while (i >= 0):
            if (self.DCTrap.respawns_list[i].IsLocked() and self.parent.bs_rlock.Test(self.DCTrap.respawns_list[i])):
                dist = self.unit.CalcDist(self.DCTrap.respawns_list[i])
                if (dist < min_dist):
                    min_dist = dist
                    closest = i
            i -= 1

        if (closest == -1):
            return 0
        self.parent.u_MoveToWithEscort(self.DCTrap.respawns_list[closest].GetPosition())
        return 1



    def h_RespawnLocked(self, respawn):
        self.parent.LastPlayerPos = respawn.GetPosition()
        self.parent.LastTimePlayerFound = gamemodule.GetTime()



    def h_RespawnFound(self, respawn):
        if (self.CurState >= CCS2_LOCK_RESPAWN):
            return 
        if self.parent.bs_rlock.Test(respawn):
            self.u_RewindStateStack()
            self.CurState = CCS2_LOCK_RESPAWN
            self.parent.bs_rlock.Run()
            return 
        return 



    def n_StateEnded(self, params = 0):
        LOG(self, 'n_StateEnded')



    def h_EndStandAnim(self, param):
        self.enchant_cnt += 1
        if (self.enchant_cnt == 1):
            self.Think()
            return 
        if (operator.mod(self.enchant_cnt, 5) == 0):
            if self.parent.bs_enchant2.Test([self.unit], 0.5):
                self.parent.bs_enchant2.Run()
                return 
            self.u_RewindStateStack()
            self.Think()
            return 
        return 



class CCRecon(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'cc_recon'
        self.bstate = self.ment.CreateNewBehaviourState('recon')
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [ADD_CRITTER, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.u_ExitCurState})
        self.bstate.AddEventReaction(ORDER, [END_STAND_ANIM], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EndStandAnim})
        self.bstate.AddEventReaction(ORDER, [PAIN_ORDER, self.h_PainTest], 0, 0)
        self.CRE = 0
        self.upkeep = 0
        self.battle_recon = 0
        self.stand_cnt = 0



    def Run(self, upkeep_percent):
        self.upkeep = (upkeep_percent * self.parent.u_GetMaxUpkeep())
        return StateBase.Run(self)



    def h_StartFunc(self):
        return 



    def h_EndFunc(self):
        StateBase.h_EndFunc(self)



    def Think(self, params = 0):
        LOG(self, ((('Think, allowed_upkeep=' + str(self.upkeep)) + ', LastPlayerPos=') + str(self.parent.LastPlayerPos)))
        if (self.CRE >= math.ceil((float(len(self.parent.bdomain.EntrancePoints)) / 2.0))):
            self.CRE = 0
        if (self.parent.LastPlayerPos != []):
            self.battle_recon = 1
        else:
            self.battle_recon = 0
        if (self.battle_recon == 0):
            LOG(self, (('Think: ' + str(len(self.parent.cl_recon))) + ' units'))
            i = (len(self.parent.cl_recon) - 1)
            while (i >= 0):
                LOG(self, ('Think: CRE=' + str(self.CRE)))
                if (self.CRE >= math.ceil((float(len(self.parent.bdomain.EntrancePoints)) / 2.0))):
                    break
                if (self.parent.u_IsUnitBusy(self.parent.cl_recon[i]) == 0):
                    if (self.CRE >= 0):
                        LOG(self, ('Think: idx=' + str(self.CRE)))
                        self.parent.u_MoveGroup([self.parent.bdomain.EntrancePoints[self.CRE][0], 0, self.parent.bdomain.EntrancePoints[self.CRE][1]], [self.parent.cl_recon[i]])
                        if (self.CRE != 0):
                            if (self.CRE > math.ceil((float(len(self.parent.bdomain.EntrancePoints)) / 2.0))):
                                self.u_ExitCurState()
                                return 0
                            self.CRE = -self.CRE
                        else:
                            self.CRE = -1
                    else:
                        idx = (len(self.parent.bdomain.EntrancePoints) + self.CRE)
                        LOG(self, ('Think: idx=' + str(idx)))
                        self.parent.u_MoveGroup([self.parent.bdomain.EntrancePoints[idx][0], 0, self.parent.bdomain.EntrancePoints[idx][1]], [self.parent.cl_recon[i]])
                        if (self.CRE == -1):
                            self.CRE = 1
                        else:
                            self.CRE = (-self.CRE + 1)
                i -= 1

        else:
            i = (len(self.parent.cl_recon) - 1)
            mypos = self.unit.GetPosition()
            while (i >= 0):
                if (self.parent.u_IsUnitBusy(self.parent.cl_recon[i]) == 0):
                    if len(self.parent.l_venemys):
                        self.parent.u_MoveGroup([mypos[0], 0, mypos[2]], [self.parent.cl_recon[i]])
                    else:
                        self.parent.u_MoveGroup([self.parent.LastPlayerPos[0], 0, self.parent.LastPlayerPos[2]], [self.parent.cl_recon[i]])
                i -= 1

        group_upkeep = self.parent.u_GetGroupUpkeep(self.parent.cl_recon)
        upkeep_left = (self.upkeep - group_upkeep)
        if (upkeep_left < 0):
            self.u_ExitCurState()
            return 0
        if (self.battle_recon == 0):
            if (len(self.parent.cl_recon) > (1.2 * len(self.parent.bdomain.EntrancePoints))):
                self.u_ExitCurState()
                return 0
            i = 0
            while (i < upkeep_left):
                i += 1
                if (i > upkeep_left):
                    i = upkeep_left
                if self.parent.bs_summon2.Test(upkeep_left, CF_FLYING, CS2_STRICT_CFLAGS):
                    self.parent.cl_current = self.parent.cl_recon
                    self.parent.bs_summon2.Run()
                    return 1

            i = 0
            while (i < upkeep_left):
                i += 1
                if (i > upkeep_left):
                    i = upkeep_left
                if self.parent.bs_summon2.Test(upkeep_left, CF_ANY, 0):
                    self.parent.cl_current = self.parent.cl_recon
                    self.parent.bs_summon2.Run()
                    return 1

        elif self.parent.bs_summon2.Test(upkeep_left, CF_ANY, 0):
            self.parent.cl_current = self.parent.cl_recon
            self.parent.bs_summon2.Run()
            return 1
        self.u_ExitCurState()
        return 0



    def h_EndStandAnim(self, param):
        self.stand_cnt += 1
        if (self.stand_cnt > self.parent.summon_timeout):
            LOG(self, 'h_EndStandAnim: summon timed out, exiting ....')
            self.u_ExitCurState()
        return 



class CCFormBattleGroup(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'cc_form_battle_group'
        self.bstate = self.ment.CreateNewBehaviourState('form_battle_group')
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [ADD_CRITTER, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.u_ExitCurState})
        self.bstate.AddEventReaction(ORDER, [END_STAND_ANIM], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EndStandAnim})
        self.bstate.AddEventReaction(ORDER, [PAIN_ORDER, self.h_PainTest], 0, 0)
        self.upkeep = 0
        self.cl = None
        self.stand_cnt = 0



    def Run(self, upkeep_percent, cl):
        self.upkeep = (upkeep_percent * (self.parent.u_GetMaxUpkeep() - self.parent.u_GetUpkeep()))
        self.cl = cl
        return StateBase.Run(self)



    def Think(self, params = 0):
        LOG(self, ('Think(): allowed upkeep=' + str(self.upkeep)))
        self.stand_cnt = 0
        upkeep_left = self.upkeep
        group_upkeep = self.parent.u_GetGroupUpkeep(self.cl)
        upkeep_left -= group_upkeep
        if (upkeep_left < 0):
            self.u_ExitCurState()
            return 0
        LOG(self, ('Think(): upkeep left=' + str(upkeep_left)))
        if self.parent.bs_summon2.Test(upkeep_left, CF_ANY, 0):
            self.parent.cl_current = self.cl
            self.parent.bs_summon2.Run()
            return 1
        if self.parent.bs_summon2.Test(upkeep_left, CF_SPELLCASTER, 0):
            self.parent.cl_current = self.cl
            self.parent.bs_summon2.Run()
            return 1
        self.u_ExitCurState()
        return 0



    def h_StartFunc(self):
        LOG(self, 'entering')



    def h_EndFunc(self):
        self.cl = None
        StateBase.h_EndFunc(self)



    def h_EndStandAnim(self, param):
        self.stand_cnt += 1
        if (self.stand_cnt > self.parent.summon_timeout):
            LOG(self, 'h_EndStandAnim: summon timed out, exiting ....')
            self.u_ExitCurState()
        return 



class CCSummonImp(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'cc_summon_imp'
        self.bstate = self.ment.CreateNewBehaviourState(self.name)
        self.bstate.SetOrderTransparencyMask(MAIN_STATE_ORDERS)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.AddEventReaction(ORDER, [ADD_CRITTER, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_AddCrittter})
        self.upkeep = 0



    def Test(self, allowed_upkeep_percent):
        if ((self.parent.need_more_imps == 0) and (len(self.unit.Imps) >= 2)):
            return 0
        self.upkeep = (allowed_upkeep_percent * self.parent.u_GetMaxUpkeep())
        upkeep_left = self.upkeep
        group_upkeep = self.parent.u_GetGroupUpkeep(self.unit.Imps)
        upkeep_left -= group_upkeep
        if (upkeep_left <= 0):
            return 0
        if self.parent.bs_summon2.Test(upkeep_left, CF_MESSENGER, CS2_STRICT_CFLAGS):
            return 1
        return 0



    def Run(self):
        self.ment.SetCurrentBehaviourState(self.bstate)
        self.parent.cl_current = None
        self.parent.bs_summon2.Run()



    def h_AddCrittter(self, param = 0):
        LOG(self, 'h_AddCrittter')
        self.parent.need_more_imps = 0
        self.u_ExitCurState()



    def h_StartFunc(self):
        return 



    def h_EndFunc(self):
        StateBase.h_EndFunc(self)



class CCMain_att(SCMain2):

    def __init__(self, parent):
        SCMain2.__init__(self, parent)
        self.name = 'cc_main_att'
        self.bstate.AddEventReaction(ORDER, [B_PLAYER_FOUND, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_PlayerFound})
        self.bstate.AddEventReaction(ORDER, [B_ENEMY_FOUND, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EnemyFound})
        self.bstate.AddEventReaction(ORDER, [B_UNIT_LOST, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_UnitLost})
        self.bstate.AddEventReaction(ORDER, [B_DAMAGE, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_Damage})
        self.bstate.AddEventReaction(ORDER, [B_RESPAWN_FOUND, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_RespawnFound})
        self.bstate.AddEventReaction(ORDER, [STATE_ENDED, 0], (FUNCTION_ACTION | NOTIFY_ACTION), {FUNCTION_ACTION: self.Think, NOTIFY_ACTION: self.n_StateEnded})
        self.bstate.AddEventReaction(ORDER, [END_STAND_ANIM], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EndStandAnim})
        self.CurState = CCS2_MAIN
        self.DCTrap = gamemodule.GameManager_GetScene().GetBDomainObject().DCTrap



    def Dest(self):
        self.DCTrap = None
        SCMain2.Dest(self)



    def h_StartFunc(self):
        SCMain2.h_StartFunc(self)
        return 



    def h_PlayerFound(self, unit):
        if ((gamemodule.GetTime() - self.parent.LastTimePlayerFound) < 2.0):
            return 
        self.parent.LastTimePlayerFound = gamemodule.GetTime()
        p_pos = unit.GetPosition()
        self.parent.LastPlayerPos = p_pos
        if (self.unit.CalcDist_Point(p_pos) < (self.unit.GetVisibilityRange() * self.parent.player_attack_activation_dist)):
            self.parent.u_MoveEscort(p_pos)
            if ((self.CurState < CCS2_CAST) and (self.parent.bs_magic_attack2.Test() > 0.0)):
                self.u_RewindStateStack()
                self.CurState = CCS2_CAST
                self.parent.bs_magic_attack2.Run()
                return 
            if ((self.CurState < CCS2_CAST) and (whrandom.random() < self.parent.melee_contingency)):
                self.u_RewindStateStack()
                self.CurState = CCS2_MELEE
                self.u_AttackTarget_Melee(unit)
                return 


        return

    def h_EnemyFound(self, objs):
        if self.move_order:
            return
        unit = objs[0].GetMentalityPart().GetPythonCharacter()
        if not self.parent.IsWithinAggro(unit):
            return
        self.parent.u_MoveGroup_Free(unit.GetPosition(), self.parent.cl_escort)
        if self.CurState < CCS2_MELEE and self.unit.CalcDist_Point(unit.GetPosition()) < self.parent.danger_range:
            self.u_RewindStateStack()
            self.CurState = CCS2_MELEE
            self.u_AttackTarget_Melee(unit)
            return 
        if ((self.CurState < CCS2_CAST) and (self.parent.bs_magic_attack2.Test() > 0.0)):
            self.u_RewindStateStack()
            self.CurState = CCS2_CAST
            self.parent.bs_magic_attack2.Run()
            return 
        if ((self.CurState < CCS2_CAST) and (whrandom.random() < self.parent.melee_contingency)):
            self.u_RewindStateStack()
            self.CurState = CCS2_MELEE
            self.u_AttackTarget_Melee(unit)
            return 



    def h_UnitLost(self, params):
        LOG(self, 'h_UnitLost')



    def h_Damage(self, params):
        LOG(self, 'h_Damage')



    def h_RespawnFound(self, respawn):
        if (self.CurState >= CCS2_LOCK_RESPAWN):
            return 
        if self.parent.bs_rlock.Test(respawn):
            self.u_RewindStateStack()
            self.CurState = CCS2_LOCK_RESPAWN
            self.parent.bs_rlock.Run()
            return 
        return 



    def CheckRespawns(self):
        if (len(self.DCTrap.respawns_list) == 0):
            return 0
        i = (len(self.DCTrap.respawns_list) - 1)
        n_locked = 0
        while (i >= 0):
            if (self.DCTrap.respawns_list[i].IsLocked() == 1):
                n_locked += 1
            i -= 1

        if ((float(n_locked) / len(self.DCTrap.respawns_list)) > self.parent.att_rlock_limit):
            return 0
        min_dist = 1800
        closest = -1
        i = (len(self.DCTrap.respawns_list) - 1)
        while (i >= 0):
            if ((self.DCTrap.respawns_list[i].IsLocked() == 0) and self.parent.bs_rlock.Test(self.DCTrap.respawns_list[i])):
                dist = self.unit.CalcDist(self.DCTrap.respawns_list[i])
                if (dist < min_dist):
                    min_dist = dist
                    closest = i
            i -= 1

        if (closest == -1):
            return 0
        self.parent.u_MoveToWithEscort(self.DCTrap.respawns_list[closest].GetPosition(), 1)
        return 1



    def Think(self, params = 0):
        self.CurState = CCS_NONE
        if ((gamemodule.GetTime() - self.parent.LastTimePlayerFound) > 20.0):
            self.parent.LastPlayerPos = []
            self.parent.LastTimePlayerFound = gamemodule.GetTime()
        if ((self.parent.u_CheckEscortUpkeep() > 0.75) and self.CheckRespawns()):
            return 
        if self.parent.bs_summon_imp.Test(self.parent.messengers_upkeep):
            self.CurState = CCS2_CAST
            self.parent.bs_summon_imp.Run()
            return 
        if self.parent.bs_enchant2.Test([self.unit], 0.10000000000000001):
            self.CurState = CCS2_CAST
            self.parent.bs_enchant2.Run()
            return 
        if self.parent.bs_FBG.Run(self.parent.escort_upkeep, self.parent.cl_escort):
            self.CurState = CCS2_CAST
            return 
        critters = []
        i = (len(self.parent.cl_escort) - 1)
        while (i >= 0):
            if (not self.parent.u_IsUnitBusy(self.parent.cl_escort[i])):
                critters.append(self.parent.cl_escort[i])
            i -= 1

        self.parent.u_MoveGroup(self.unit.GetPosition(), critters)
        self.parent.u_MoveToWithEscort(self.parent.bdomain.DomCenter[0], 1)
        return 



    def n_StateEnded(self, params = 0):
        LOG(self, 'n_StateEnded')
        return 



    def h_EndStandAnim(self, param):
        self.enchant_cnt += 1
        if (operator.mod(self.enchant_cnt, 3) == 0):
            if self.parent.bs_enchant2.Test([self.unit], 0.5):
                self.parent.bs_enchant2.Run()
                return 
            self.Think()
            return 
        return 



class PlayerSuspend(StateBase):

    def __init__(self, parent):
        StateBase.__init__(self, parent)
        self.name = 'player_main'
        self.bstate = self.ment.CreateNewBehaviourState('main')
        self.bstate.SetOrderTransparencyMask(0)
        self.bstate.SetStartFunction(self.h_StartFunc)
        self.bstate.SetEndFunction(self.h_EndFunc)
        self.bstate.SetRuleOfTargetCharSelection(DONT_SELECT_TARGET, 0)
        self.bstate.SetRuleOfTargetPointSelection(DONT_SELECT_TARGET, 0)
        self.bstate.AddEventReaction(ORDER, [(DIRECT_TO | REMOTE_ORDER), 0], (FUNCTION_ACTION | DIRECT_OBJECT_TO_POINT), {FUNCTION_ACTION: self.h_DirectTo})
        self.bstate.AddEventReaction(ORDER, [(DIRECT_TO_RUN | REMOTE_ORDER), 0], (FUNCTION_ACTION | DIRECT_OBJECT_TO_POINT_RUN), {FUNCTION_ACTION: self.h_DirectTo})
        self.bstate.AddEventReaction(ORDER, [DIRECT_TO, 0], (FUNCTION_ACTION | DIRECT_OBJECT_TO_POINT), {FUNCTION_ACTION: self.h_DirectTo})
        self.bstate.AddEventReaction(ORDER, [DIRECT_TO_POINT, 0], (FUNCTION_ACTION | DIRECT_OBJECT_TO_POINT_RUN), {FUNCTION_ACTION: self.h_DirectTo_Point})
        self.bstate.AddEventReaction(TARGET_REACHED, [POINT_TARGET, [0, 0]], ((FUNCTION_ACTION | STOP_OBJECT) | FORGET_TARGET), {FUNCTION_ACTION: self.h_TargetReached})
        self.bstate.AddEventReaction(TARGET_REACHED, [GO_TARGET, [0, 0]], ((FUNCTION_ACTION | STOP_OBJECT) | FORGET_TARGET), {FUNCTION_ACTION: self.h_TargetReached})
        self.bstate.AddEventReaction(ORDER, [TARGET_UNREACHABLE, 0], ((FUNCTION_ACTION | STOP_OBJECT) | FORGET_TARGET), {FUNCTION_ACTION: self.h_TargetUnreachable})
        self.bstate.AddEventReaction(ORDER, [PAIN_ORDER, self.h_PainTest], PAIN_OBJECT, 0)
        self.bstate.AddEventReaction(ORDER, [MISS_ORDER, self.h_MissTest], DEFEND_OBJECT, 0)
        self.bstate.AddEventReaction(ORDER, [CAST_START, 0], ((START_CAST | FUNCTION_ACTION) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_StartSpell, NOTIFY_ACTION: self.n_CastStart})
        self.bstate.AddEventReaction(ORDER, [CAST_END, 0], ((FORGET_TARGET | FUNCTION_ACTION) | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_EndSpell, NOTIFY_ACTION: self.n_CastEnd})
        self.bstate.AddEventReaction(ORDER, [BREAK_CAST, 0], (FORGET_TARGET | FUNCTION_ACTION), {FUNCTION_ACTION: self.h_BreakCast})
        self.bstate.AddEventReaction(ORDER, [B_ENEMY_FOUND, 0], FUNCTION_ACTION, {FUNCTION_ACTION: self.h_EnemyFound})
        self.bstate.AddEventReaction(ORDER, [WAIT_OBJECT], (FUNCTION_ACTION | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_WaitObject, NOTIFY_ACTION: self.n_WaitObject})
        self.bstate.AddEventReaction(ORDER, [RESUME_OBJECT, self.h_ResumeTestAnother], (FUNCTION_ACTION | NOTIFY_ACTION), {FUNCTION_ACTION: self.h_ResumeObject, NOTIFY_ACTION: self.n_ResumeObject})
        self.bstate.AddEventReaction(ORDER, [RESUME_OBJECT, self.h_ResumeTestPoint], ((FUNCTION_ACTION | NOTIFY_ACTION) | DIRECT_OBJECT_TO_POINT), {FUNCTION_ACTION: self.h_ResumeObject, NOTIFY_ACTION: self.n_ResumeObject})
        self.bstate.AddEventReaction(ORDER, [RESUME_OBJECT, self.h_ResumeTestMO], (((FUNCTION_ACTION | NOTIFY_ACTION) | DIRECT_OBJECT_TO_CHAR_RUN) | CHANGE_STATE), {FUNCTION_ACTION: self.h_ResumeObject, NOTIFY_ACTION: self.n_ResumeObject, CHANGE_STATE: 'attack'})
        self.now_casting = 0
        self.wait_target = None



    def h_EnemyFound(self, objs):
        LOG(self, 'h_EnemyFound')
        if self.now_casting:
            return 
        target = objs[0]
        unit = target.GetMentalityPart().GetPythonCharacter()
        LOG(self, ((('h_EnemyFound: ' + str(len(objs))) + ' objs, dist=') + str(self.unit.CalcDist(unit))))
        if (self.unit.CalcDist(unit) < self.parent.AAR):
            mtarget = [MO_TARGET, 0, unit.MentalityObject, 0]
            self.ment.SetTarget(mtarget)
            self.ment.SetCurrentBehaviourState(self.parent.bs_attack.bstate)
            self.ment.SendOrder(DIRECT_TO, [unit.MentalityObject.GetGraphObject(), 0])
            return 
        return 



    def h_StartFunc(self):
        LOG(self, 'entering')
        self.parent.SetRoTCS(self.bstate)
        return 



    def h_EndFunc(self):
        LOG(self, 'exiting')
        return 



    def h_DirectTo(self, param):
        LOG(self, 'h_DirectTo')
        self.moving = 1



    def h_DirectTo_Point(self, param):
        LOG(self, 'h_DirectTo_Point')
        self.moving = 1



    def EndMove(self):
        self.moving = 0



    def h_TargetReached(self):
        LOG(self, 'h_TargetReached')
        self.EndMove()



    def h_TargetUnreachable(self, param = None):
        LOG(self, 'h_TargetUnreachable')
        self.EndMove()



    def ea_EndCastEnd(self, i):
        LOG(self, 'ea_EndCastEnd')
        self.ment.GetGraphObject().RemoveEndAnimationFunction((ENDFLAG | CAST), 0, self.ea_EndCastEnd)
        self.ment.GetGraphObject().SetChainLooped((LOOPFLAG | CAST), 0, 1)



    def h_StartSpell(self, param = 0):
        self.now_casting = 1
        self.ment.GetGraphObject().SetChainLooped((LOOPFLAG | CAST), 0, 1)



    def h_EndSpell(self, param = 0):
        self.now_casting = 0
        if self.unit.NowCasting:
            self.unit.NowCasting.FinishCast()
        self.ment.GetGraphObject().SetChainLooped((LOOPFLAG | CAST), 0, 0)
        self.ment.GetGraphObject().AddEndAnimationFunction((ENDFLAG | CAST), 0, self.ea_EndCastEnd)



    def h_BreakCast(self, param = 0):
        LOG(self, 'h_BreakCast')
        self.now_casting = 0
        if self.unit.NowCasting:
            self.unit.NowCasting.BreakCast()



    def h_PainTest(self, ordertype, params):
        ret = StateBase.h_PainTest(self, ordertype, params)
        if (self.moving and (not ret)):
            return ret
        if (params[1] and ((not (params[1].FLAGS & BCF_DYING)) and (not (self.unit.NowCasting and self.unit.NowCasting.SpellFunctionalityObject)))):
            mtarget = [MO_TARGET, 0, params[1].MentalityObject, 0]
            LOG(self, ('h_PainTest (player): melee attack: mtarget=' + str(mtarget)))
            self.ment.SetTarget(mtarget)
            self.ment.SetCurrentBehaviourState(self.parent.bs_attack.bstate)
            self.ment.SendOrder(DIRECT_TO, [params[1].MentalityObject.GetGraphObject(), 0])
        return ret



    def n_Flee(self, param = 0):
        LOG(self, 'n_Flee')
        return 



    def n_OutOfRange(self, param = 0):
        LOG(self, 'n_OutOfRange')
        return 



    def n_Pain(self, param = 0):
        LOG(self, 'n_Pain')
        return 



    def n_CastStart(self, param = 0):
        LOG(self, 'n_CastStart')
        return 



    def n_CastEnd(self, param = 0):
        LOG(self, 'n_CastEnd')
        return 



    def Dest(self):
        self.wait_target = None
        StateBase.Dest(self)



    def h_WaitObject(self, params):
        self.wait_target = params
        self.waiting = 1



    def h_ResumeTestPoint(self, order, params):
        if ((not params) or (not self.wait_target)):
            return 0
        if (self.wait_target[1] != params[0]):
            return 0
        if (self.wait_target[0] != MO_TARGET):
            return 1
        return 0



    def h_ResumeTestMO(self, order, params):
        if ((not params) or (not self.wait_target)):
            return 0
        if (self.wait_target[1] != params[0]):
            return 0
        if (self.wait_target[0] == MO_TARGET):
            return 1
        return 0



    def h_ResumeTestAnother(self, order, params):
        if ((not params) or (not self.wait_target)):
            return 0
        if (self.wait_target[1] == params[0]):
            return 0
        return 1



    def h_ResumeObject(self, params):
        self.wait_target = None
        self.waiting = 0



    def n_WaitObject(self, params):
        LOG(self, ('n_WaitObject ' + repr(params)))



    def n_ResumeObject(self, params):
        LOG(self, ('n_ResumeObject ' + repr(params)))
