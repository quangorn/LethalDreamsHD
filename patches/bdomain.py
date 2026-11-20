import gamemodule, globalscript, whrandom, battlecamera, ui, dom_center, cshield, createspawnspots, overlandcharacters, s_ambient, std_misc, g_defs
Dict = globalscript.GetDictionary()

# Фиксированный индекс точки входа отряда (EntrancePoints) на всех боевых картах.
# Решение пользователя 20.11.2026: спавн отряда и стартовая камера детерминированы.
FIXED_ENTRANCE_INDEX = 5

class BDomain:

    def __init__(self):
        global Dict
        self.AmbientSoundsTable = []
        self.EntrancePoints = []
        self.StartLookPoints = []
        self.KeepersEntrancePoints = {'Owner': [0, 0, 0]}
        self.DomCenter = []
        self.DomRadius = 0
        self.DomShields = []
        self.DShieldList = []
        self.DynLights = []
        self.DLightsList = []
        self.SunDirection = []
        self.ShadowColor = []
        self.Defenders = []
        self.Attackers = []
        self.OpponentGroups = {}
        self.SolidGolemMtl = {'p1_texture': 'solid_golem_default.tga', 'p1_colorgensetrgba': None, 'p1_tcmodscale': [2, 2], 'p1_tcmodscroll': None, 'p1_tcmodrotate': None, 'p1_depthmask': None, 'p1_cullface': None, 'p2_texture': 'solid_golem01.bmp', 'p2_blending': (globalscript.BBlend_Modulate2x), 'p2_colorgensetrgba': None, 'p2_depthmask': None, 'p2_cullface': None}
        self.LiquidGolemMtl = {'p1_texture': 'liquid_golem_default.tga', 'p1_colorgensetrgba': None, 'p1_tcmodscale': None, 'p1_tcmodscroll': [0, 0.2], 'p1_tcmodrotate': None, 'p1_depthmask': None, 'p1_cullface': None, 'p2_texture': None, 'p2_blending': None, 'p2_colorgensetrgba': None, 'p2_depthmask': None, 'p2_cullface': None}
        self.ProgressTextureName = 'lp_unknown.bmp'
        self.ProgressDomainName = Dict.LP_Unknown
        self.ProgressText = Dict.LP_LoadingProgressText
        self.BaseVolume = 0
        self.AmbientMusicName = ''
        self.AmbientMusic = None
        self.AmbientVolume = 0
        self.AmbientSounds = []
        self.AdditionalFXDest = None
        self.FadeTimeElapsed = 0
        globalscript.ExecConfig()
        self.MiniDomain = None
        self.TimerID = None
        self.DCTrap = None
        self.TrashedCritters = []
        self.TrashedEffects = []
        self.InterfaceMode = 1
        self.Camera = None
        self.MiniMapTexture = None
        self.RespawnsOwner = None
        return

    def SetInterfaceMode(self, mode):
        self.InterfaceMode = mode
        return

    def CalculateAutoBattle(self):
        att_group = 0
        keys = self.OpponentGroups.keys()
        i = 0
        sz = len(keys)
        while i < sz:
            if self.OpponentGroups[keys[i]].count(self.Attackers[0]):
                att_group = keys[i]
                break
            i += 1

        attackers = []
        i = 0
        sz = len(self.OpponentGroups[att_group])
        while i < sz:
            attackers.append(self.OpponentGroups[att_group][i])
            i += 1

        deffenders = []
        keys = self.OpponentGroups.keys()
        del keys[keys.index(att_group)]
        i = 0
        sz = len(keys)
        while i < sz:
            opponents = self.OpponentGroups[keys[i]]
            j = 0
            sz1 = len(opponents)
            while j < sz1:
                deffenders.append(opponents[j])
                j += 1

            i += 1

        import formulas
        res = formulas.CalculateAutoBattle(self, attackers, deffenders)
        if res == 1:
            self.WinGroups([att_group])
        else:
            self.WinGroups(keys)
        return

    def WinGroups(self, groups):
        winners = []
        i = 0
        sz = len(groups)
        while i < sz:
            winners += self.OpponentGroups[groups[i]]
            i += 1

        loosers = []
        i = 0
        sz = len(self.Defenders)
        while i < sz:
            if winners.count(self.Defenders[i]) == 0:
                loosers.append(self.Defenders[i])
            i += 1

        i = 0
        sz = len(self.Attackers)
        while i < sz:
            if winners.count(self.Attackers[i]) == 0:
                loosers.append(self.Attackers[i])
            i += 1

        minidom = self.MiniDomain
        self.MiniDomain.CloseBattle()
        i = 0
        sz = len(loosers)
        while i < sz:
            loosers[i].LooseBattle(minidom)
            i += 1

        import overland_map
        keeped = 1
        if winners.count(minidom.Owner):
            keeped = 0
        i = 0
        sz = len(minidom.Keepers)
        while i < sz:
            if winners.count(minidom.Keepers[i]):
                keeped = 0
                break
            i += 1

        if keeped:
            overlandcharacters.WinBattle(minidom, winners, loosers, overland_map.DOMAIN_KEEPED, overlandcharacters.KILL_DEFENDER)
        else:
            overlandcharacters.WinBattle(minidom, winners, loosers, overland_map.DOMAIN_DEFENDED, overlandcharacters.KILL_ATTACKER)
        return [loosers, winners]
        return

    def CharacterDied(self, char):
        if self.Defenders.count(char) or self.Attackers.count(char):
            self.KillOpponent(char)
        return

    def AddCritterToTrash(self, critter):
        if not self.TrashedCritters.count(critter):
            self.TrashedCritters.append(critter)
        return

    def RemoveCritterFromTrash(self, critter):
        if self.TrashedCritters.count(critter):
            self.TrashedCritters.remove(critter)
        return

    def AddEffectToTrash(self, fx):
        self.TrashedEffects.append(fx)
        return

    def RemoveEffectFromTrash(self, fx):
        if self.TrashedEffects.count(fx):
            self.TrashedEffects.remove(fx)
        return

    def ChangeUpkeep(self, Owner, upkeep):
        if self.TimerID == None:
            self.TimerID = gamemodule.SetTimer(1, self.h_Timer)
        own = Owner.Owner
        found = 0
        i = 0
        sz = len(self.Defenders)
        while i < sz:
            if self.Defenders[i] == own:
                found = 1
                break
            i += 1

        if found:
            self.MiniDomain.ChangeBattleUpkeep(upkeep, Owner)
        i = 0
        sz = len(self.Attackers)
        while i < sz:
            if self.Attackers[i] == own:
                self.Attackers[i].CurrentLocation.ChangeBattleUpkeep(upkeep, Owner)
                break
            i += 1

        return

    def h_Timer(self, tid):
        self.MiniDomain.TickBattleUpkeep()
        i = 0
        sz = len(self.Attackers)
        while i < sz:
            self.Attackers[i].CurrentLocation.TickBattleUpkeep()
            i += 1

        return

    def DecMana(self, Owner, mana):
        found = 0
        i = 0
        sz = len(self.Defenders)
        while i < sz:
            if self.Defenders[i] == Owner:
                found = 1
                break
            i += 1

        if found:
            return self.MiniDomain.DecBattleMana(mana, Owner)
        i = 0
        sz = len(self.Attackers)
        while i < sz:
            if self.Attackers[i] == Owner:
                return self.Attackers[i].CurrentLocation.DecBattleMana(mana, Owner)
            i += 1

        if Owner:
            return Owner.CurrentLocation.DecBattleMana(mana, Owner)
        return 0
        return

    def Dest(self):
        gamemodule.Log('DestBDomain')
        if self.DCTrap:
            self.DCTrap.Dest()
        if self.TimerID != None:
            gamemodule.KillTimer(self.TimerID)
        i = 0
        sz = len(self.Attackers)
        while i < sz:
            self.Attackers[0].CalculateAllSpellsPowerChanges()
            self.Attackers[0].DeleteBattleUnit()
            del self.Attackers[0]
            i += 1

        self.Attackers = []
        i = 0
        sz = len(self.Defenders)
        while i < sz:
            self.Defenders[0].CalculateAllSpellsPowerChanges()
            self.Defenders[0].DeleteBattleUnit()
            del self.Defenders[0]
            i += 1

        self.Defenders = []
        scene = gamemodule.GameManager_GetScene()
        while len(self.TrashedCritters):
            oldsize = len(self.TrashedCritters)
            scene.RemoveMentalObject(self.TrashedCritters[0].MentalityObject)
            if oldsize == len(self.TrashedCritters):
                gamemodule.Log('Error on object ' + repr(self.TrashedCritters[0]))
                self.TrashedCritters.remove(self.TrashedCritters[0])

        self.TrashedCritters = []
        i = len(self.TrashedEffects) - 1
        while i >= 0:
            self.TrashedEffects[i].Kill()
            i -= 1

        self.TrashedEffects = []
        createspawnspots.Dest()
        if self.Camera:
            self.Camera.Dest()
        self.DestDynLights()
        i = 0
        sz = len(self.DShieldList)
        while i < sz:
            self.DShieldList[0].Dest()
            del self.DShieldList[0]
            i += 1

        self.DomShields = []
        if not self.AdditionalFXDest == None:
            self.AdditionalFXDest()
        i = len(self.AmbientSounds)
        while i > 0:
            self.AmbientSounds[0].Dest()
            del self.AmbientSounds[0]
            i = i - 1

        self.MiniDomain = None
        if self.AmbientMusic:
            self.AmbientMusic.Stop()
            self.AmbientMusic.DeleteInnerData()
        self.AmbientMusic = None
        if self.RespawnsOwner:
            self.RespawnsOwner.Dest()
        return

    def PreDest(self):
        if self.TimerID != None:
            gamemodule.KillTimer(self.TimerID)
        self.TimerID = None
        i = 0
        sz = len(self.Attackers)
        while i < sz:
            self.Attackers[i].PreDest()
            i += 1

        i = 0
        sz = len(self.Defenders)
        while i < sz:
            self.Defenders[i].PreDest()
            i += 1

        return

    def SetMiniDomain(self, dom):
        self.MiniDomain = dom
        return

    def CreateAmbientSounds(self):
        i = len(self.AmbientSoundsTable) - 1
        while i >= 0:
            snd = s_ambient.BAmbientSound(self.AmbientSoundsTable[i][0], self.AmbientSoundsTable[i][1], self.AmbientSoundsTable[i][2], self.AmbientSoundsTable[i][3], self.AmbientSoundsTable[i][4])
            if len(self.AmbientSoundsTable[i][5]):
                snd.SetRandom(self.AmbientSoundsTable[i][5][0], self.AmbientSoundsTable[i][5][1], self.AmbientSoundsTable[i][5][2], self.AmbientSoundsTable[i][5][3])
            else:
                snd.Play(1)
            self.AmbientSounds.append(snd)
            i = i - 1

        return

    def InitializeProgress(self, steps):
        self.OldScene = gamemodule.GameManager_GetScene()
        self.ProgressScene = gamemodule.Scene_CreateNew()
        self.ProgressScene.CreateInnerData()
        gamemodule.GameManager_SetScene(self.ProgressScene)
        if self.AmbientMusicName != '' and g_defs.dev_no_music == 0:
            self.AmbientMusic = gamemodule.LoadMusic(self.AmbientMusicName)
            self.AmbientMusic.Play(1)
            self.AmbientMusic.SetVolume(self.AmbientVolume)
        self.OwnConsole = gamemodule.NewConsole()
        self.OldConsole = gamemodule.SetConsole(self.OwnConsole)
        self.LP_PicBB = ui.BActionElement([0, 0, 1024, 1024, [1, 1, 1, 1], self.ProgressTextureName, globalscript.BBlend_Replace])
        self.LP_PicBB.Unsubscribe()
        self.LP_PicBB.SetPrior('MainBB', 990)
        self.LP_FrameBB = ui.BActionElement([0, 0, 1024, 1024, [1, 1, 1, 1], 'lp_frame.tga', globalscript.BBlend_Blend])
        self.LP_FrameBB.Unsubscribe()
        self.LP_TextBB = ui.BActionElement([0, 725, 245, 18, ui.GUI_TNORMAL, '', 0, [self.ProgressText, 'ld_uncial12', 18, 1, 0, 0]], [], [])
        self.LP_TextBB.SetPrior('MainBB', 1010)
        self.LP_NameBB = ui.BActionElement([520, 85, 445, 32, ui.GUI_TNORMAL, '', 0, [self.ProgressDomainName, 'ld_uncial20', 32, 2, 0, 0]], [], [])
        self.LP_NameBB.SetPrior('MainBB', 1010)
        self.ProgressStep = 0
        self.ProgressSteps = steps
        i = 0
        self.bubbles = []
        while i < 50:
            tmp = ui.BActionElement([300 + i * 14, 724, 16, 16, [1, 1, 1, 1], 'st_tealbubblehilit.bmp', globalscript.BBlend_Add])
            tmp.Unsubscribe()
            tmp.SetVisible('MainBB', 0)
            self.bubbles.append(tmp)
            i += 1

        return
        return

    def Tick(self):
        if self.__dict__.has_key('ProgressStep'):
            nold = 50.0 / self.ProgressSteps * self.ProgressStep
            self.ProgressStep += 1
            n = 50.0 / self.ProgressSteps * self.ProgressStep
            if int(nold) != int(n):
                i = int(0)
                if n > 50.0:
                    n = 50.0
                while i < int(n):
                    self.bubbles[i].SetVisible('MainBB', 1)
                    i += 1

                return 1
            return 0
        return

    def EndProgress(self):
        if self.__dict__.has_key('ProgressStep'):
            if self.LP_PicBB != None:
                self.LP_PicBB.Dest()
                self.LP_PicBB = None
            if self.LP_FrameBB != None:
                self.LP_FrameBB.Dest()
                self.LP_FrameBB = None
            if self.LP_TextBB != None:
                self.LP_TextBB.Dest()
                self.LP_TextBB = None
            if self.LP_NameBB != None:
                self.LP_NameBB.Dest()
                self.LP_NameBB = None
            i = 0
            while i < 50:
                self.bubbles[i].Dest()
                i += 1

            self.bubbles = []
        gamemodule.SetConsole(self.OldConsole)
        gamemodule.RemoveConsole(self.OwnConsole)
        self.ProgressScene.DeleteInnerData()
        gamemodule.GameManager_SetScene(self.OldScene)
        self.OldScene = None
        self.ProgressScene = None
        return

    def InitializeData(self):
        import overland_map
        gamemodule.Log('bdomain.InitializeData' + repr(self.InterfaceMode))
        if self.InterfaceMode:
            if self.MiniDomain.DomainType == overland_map.OL_RANDOM_TYPE:
                self.MiniMapTexture = gamemodule.GetMiniMapTexture()
            else:
                self.MiniMapTexture = gamemodule.LoadTexture(self.MiniDomain.minimap)
            self.DCTrap = std_misc.DCTrap(self.DomCenter[0])
            self.CreateDomShields()
            self.SetDynLights()
            if len(self.SunDirection) > 0:
                scene = gamemodule.GameManager_GetScene()
                scene.SetSunDirection(self.SunDirection[0], self.SunDirection[1], self.SunDirection[2])
                if len(self.ShadowColor) > 0:
                    scene.SetShadowColor(self.ShadowColor[0], self.ShadowColor[1], self.ShadowColor[2])
            else:
                globalscript.EnableShadowCheck(0)
                gamemodule.r_shadows(0)
            # Точка входа отряда фиксирована: FIXED_ENTRANCE_INDEX, случайного выбора нет.
            # Камера стартует в той же точке, взгляд — по StartLookPoints того же входа.
            index = FIXED_ENTRANCE_INDEX
            if index >= len(self.EntrancePoints):
                index = whrandom.randint(0, len(self.EntrancePoints) - 1)
            createspawnspots.DoIt(self.EntrancePoints)
            self.CreateCharactersBattleUnits(index)
            if self.MiniDomain.Owner != overlandcharacters.Player:
                self.InitializeCamera(index)
            else:
                self.InitializeCamera(len(self.StartLookPoints) - 1)
            self.CreateAmbientSounds()
        else:
            self.CreateCharactersBattleUnits(0)
        return
        return

    def InitializeCamera(self, entranceindex):
        self.Camera = battlecamera.BattleCamera()
        if len(self.DomShields) > 0:
            x = [self.DomShields[0][0], self.DomShields[0][1], self.DomShields[0][2]]
            if entranceindex < len(self.EntrancePoints):
                self.Camera.B_SetDomainData(self.EntrancePoints[entranceindex], x, self.DomRadius)
            else:
                self.Camera.B_SetDomainData(self.KeepersEntrancePoints['Owner'], x, self.DomRadius)
        elif entranceindex < len(self.EntrancePoints):
            self.Camera.B_SetDomainData(self.EntrancePoints[entranceindex], self.DomCenter[0], self.DomRadius)
        else:
            self.Camera.B_SetDomainData(self.KeepersEntrancePoints['Owner'], self.DomCenter[0], self.DomRadius)
        if entranceindex < len(self.EntrancePoints):
            self.Camera.B_SetPos(self.EntrancePoints[entranceindex], self.StartLookPoints[entranceindex])
        else:
            self.Camera.B_SetPos([self.KeepersEntrancePoints['Owner'][0], self.KeepersEntrancePoints['Owner'][2]], self.StartLookPoints[entranceindex])
        return
        return

    def CreateCharactersBattleUnits(self, EntrancePointIndex):
        import battlemenu, whrandom
        i = 0
        sz = len(self.MiniDomain.Keepers)
        while i < sz:
            self.AddOpponent(self.MiniDomain.Keepers[i])
            i += 1

        if self.MiniDomain.Owner and self.MiniDomain.Owner.AcceptFight(self.MiniDomain):
            self.AddOpponent(self.MiniDomain.Owner)
        i = 0
        sz = len(self.Attackers)
        while i < sz:
            self.AddOpponent(self.Attackers[i])
            i += 1

        gamemodule.Log('CreateCharactersBattleUnits' + repr(self.Attackers))
        if self.MiniDomain.RespawnsInfo:
            owner = self.MiniDomain.GetRespawnOwner(None)
            import units_base
            self.RespawnsOwner = overlandcharacters.CCharacter(units_base.UnitsBase['Empty'], '')
            self.RespawnsOwner.Owner = owner
            self.RespawnsOwner.SetStat('MST', owner.GetStat('MST'))
            self.RespawnsOwner.SetCurrentLocation(self.MiniDomain)
            self.AddOpponent(self.RespawnsOwner)
        ep = 0
        if self.InterfaceMode:
            ep = self.EntrancePoints[EntrancePointIndex]
        i = 0
        sz = len(self.MiniDomain.Keepers)
        while i < sz:
            self.Defenders.append(self.MiniDomain.Keepers[i])
            if self.InterfaceMode:
                if not self.KeepersEntrancePoints.has_key(self.MiniDomain.Keepers[i].KeyName) and self.KeepersEntrancePoints.has_key('Keeper'):
                    pos = self.KeepersEntrancePoints['Keeper']
                else:
                    pos = self.KeepersEntrancePoints[self.MiniDomain.Keepers[i].KeyName]
                self.MiniDomain.Keepers[i].CreateBattleUnit(pos[0], pos[1], pos[2])
                self.MiniDomain.Keepers[i].BattleUnit.MentalityObject.LookAtPoint(ep[0], 0, ep[1])
            i += 1

        if self.MiniDomain.Owner and self.MiniDomain.Owner.AcceptFight(self.MiniDomain):
            pos = self.KeepersEntrancePoints['Owner']
            self.Defenders.append(self.MiniDomain.Owner)
            if self.InterfaceMode:
                self.MiniDomain.Owner.CreateBattleUnit(pos[0], pos[1], pos[2])
                self.MiniDomain.Owner.BattleUnit.MentalityObject.LookAtPoint(ep[0], 0, ep[1])
        if not self.InterfaceMode:
            return
        if self.RespawnsOwner:
            self.RespawnsOwner.BattleUnit = gamemodule.LoadSceneObject(self.RespawnsOwner.UnitInfo.ModelName).GetPythonCharacter()
            gamemodule.GameManager_GetScene().AddMentalObject(self.RespawnsOwner.BattleUnit.MentalityObject)
        c_pos = self.DomCenter[0]
        i = 0
        sz = len(self.Attackers)
        while i < sz:
            self.Attackers[i].CreateBattleUnit(ep[0], 0, ep[1])
            self.Attackers[i].BattleUnit.MentalityObject.LookAtPoint(c_pos[0], 0, c_pos[2])
            i += 1

        i = 0
        sz = len(self.Attackers)
        while i < sz:
            self.Attackers[i].BattleUnit.SetOwner(self.Attackers[i])
            if self.Attackers[i] != overlandcharacters.Player:
                self.Attackers[i].CurrentLocation.ai_MultiplyBattleMana()
            i += 1

        i = 0
        sz = len(self.Defenders)
        while i < sz:
            self.Defenders[i].BattleUnit.SetOwner(self.Defenders[i])
            if self.Defenders[i] != overlandcharacters.Player:
                self.Defenders[i].CurrentLocation.ai_MultiplyBattleMana()
            i += 1

        return

    def AddOpponent(self, opponent):
        import math

        def IsFriendlyGroup(opponent, group):
            j = 0
            sz1 = len(group)
            while j < sz1:
                if not opponent.IsFriend(group[j]):
                    return 0
                j += 1

            return 1
            return

        def IsFriendInGroup(opponent, group):
            j = 0
            sz1 = len(group)
            while j < sz1:
                if opponent.IsFriend(group[j]):
                    return 1
                j += 1

            return 0
            return

        keys = self.OpponentGroups.keys()
        i = 0
        sz = len(keys)
        while i < sz:
            opponents = self.OpponentGroups[keys[i]]
            if IsFriendlyGroup(opponent, opponents):
                opponents.append(opponent)
                opponent.SetGroupID(keys[i])
                return
            i += 1

        self.OpponentGroups[int(math.pow(2, i))] = [opponent]
        opponent.SetGroupID(int(math.pow(2, i)))
        return
        return

    def KillOpponent(self, opponent):
        keys = self.OpponentGroups.keys()
        i = 0
        sz = len(keys)
        while i < sz:
            opponents = self.OpponentGroups[keys[i]]
            if opponents.count(opponent):
                del opponents[opponents.index(opponent)]
            if len(opponents) == 0 or opponents[0] == self.RespawnsOwner:
                del self.OpponentGroups[keys[i]]
            i += 1

        if opponent == overlandcharacters.Player:
            self.WinGroups(self.OpponentGroups.keys())
        elif len(self.OpponentGroups.keys()) == 1:
            self.WinGroups(self.OpponentGroups.keys())
        return

    def GetFriendlySides(self, unit):
        import operator
        ret = 0
        keys = self.OpponentGroups.keys()
        i = 0
        sz = len(keys)
        while i < sz:
            if self.OpponentGroups[keys[i]].count(unit):
                ret = operator.or_(ret, keys[i])
            i += 1

        return ret
        return

    def GetOpponentSides(self, unit):
        import operator
        ret = 0
        keys = self.OpponentGroups.keys()
        i = 0
        sz = len(keys)
        while i < sz:
            if not self.OpponentGroups[keys[i]].count(unit):
                ret = operator.or_(ret, keys[i])
            i += 1

        return ret
        return

    def GetCharactersAtGroups(self, groups):
        import operator
        ret = []
        keys = self.OpponentGroups.keys()
        i = 0
        sz = len(keys)
        while i < sz:
            if operator.and_(keys[i], groups) != 0:
                j = 0
                sz1 = len(self.OpponentGroups[keys[i]])
                while j < sz1:
                    if self.OpponentGroups[keys[i]][j] != self.RespawnsOwner:
                        ret.append(self.OpponentGroups[keys[i]][j])
                    j += 1

            i += 1

        return ret
        return

    def GetAttackerGO(self, i=None):
        if i == None:
            i = 0
        return self.Attackers[i].BattleUnit.MentalityObject.GetGraphObject()
        return

    def GetDefenderGO(self, i=None):
        if i == None:
            i = 0
        return self.Defenders[i].BattleUnit.MentalityObject.GetGraphObject()
        return

    def CreateDomShields(self):
        i = 0
        sz = len(self.DomShields)
        while i < sz:
            p = self.DomShields[i]
            self.DShieldList.append(cshield.CDomainShield(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15], p[16], p[17], p[18], p[19], p[20]))
            i += 1

        return
        return

    def SetDynLights(self):
        i = 0
        sz = len(self.DynLights)
        scene = gamemodule.GameManager_GetScene()
        while i < sz:
            p = self.DynLights[i]
            lt = gamemodule.Light_CreateNew()
            lt.CreateInnerData()
            lt.SetType(p[0])
            lt.SetColor(p[1], p[2], p[3])
            lt.SetPosition(p[4], p[5], p[6])
            lt.SetDirection(p[7], p[8], p[9])
            lt.SetRange(p[10])
            lt.SetAtten(p[11], p[12], p[13])
            lt.SetTheta(p[14])
            lt.SetPhi(p[15])
            lt.SetFalloff(p[16])
            scene.AddGraphObjectBase(lt)
            self.DLightsList.append(lt)
            i += 1

        return
        return

    def DestDynLights(self):
        i = 0
        sz = len(self.DLightsList)
        scene = gamemodule.GameManager_GetScene()
        while i < sz:
            scene.RemoveGraphObjectBase(self.DLightsList[0])
            del self.DLightsList[0]
            i += 1

        return


