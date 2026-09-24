// Include generated messages.inc first; it binds ACTOR_SELF to the selected interaction event.

#if defined _usum_included
    #endinput
#endif
#define _usum_included

#pragma dynamic 1024

#if !defined ACTOR_SELF
    #error Include messages.inc before usum.inc
#endif
#define SOUND_MESSAGE 327694
#define FLOAT_ONE_BITS 1065353216
#define WORK_MENU_ANSWER 32784

native SEPlay(sound, volume_bits, pan_bits, pitch_bits);
native _Suspend(frames);
native WorkGet(work);
native WorkSet(work, value);
native FlagGet(flag);
native FlagSet(flag);
native FlagReset(flag);
native ChrMotionPlayFrame_(actor, motion_slot, argument_3, argument_4);
native ChrMotionIsPlaying_(actor);
native ChrMove_(actor, argument_2, argument_3);
native TalkMdlStartInit_(actor, argument_2);
native TalkMdlEndInit_(actor);
native TalkMdlGetRepairAngle_();
native ChrRotTarget_(actor, target, argument_3, argument_4, argument_5, argument_6);
native ChrRot_(actor, argument_2, angle_bits, argument_4, argument_5, argument_6);
native ChrActionCommandIsPlaying_(actor);
native TalkMsg_Seq(window, actor, message, argument_4, argument_5,
                  argument_6, argument_7, argument_8, argument_9, argument_10, argument_11);
native TalkMsg_SeqWait();
native ChrLookAtFieldActor_(actor, target, argument_3, argument_4, argument_5, argument_6);
native TalkMsg_VisubleCursor(visible);
native ABKeyWait_();
native LastKeyWait_();
native MsgWinCloseNo(window);
native YesNoWin_Seq(argument_1, yes_message, no_message,
                    argument_4, argument_5);

stock ShowMessage(message)
{
    OpenMessage(message);
    AdvanceAndClose();
}

stock PlayMessageSound()
{
    SEPlay(SOUND_MESSAGE, FLOAT_ONE_BITS, 0, FLOAT_ONE_BITS);
}

stock WaitFrames(frames)
{
    _Suspend(frames);
    sleep 0;
}

stock WaitForTurn()
{
    while (ChrActionCommandIsPlaying_(ACTOR_SELF)) {
        WaitFrames(1);
    }
}

stock BeginConversation()
{
#if defined POSITION_TRIGGER
    // The region starts the script; there is no actor to face.
#elseif defined SCENERY_INTERACTION
    ChrLookAtFieldActor_(-1, ACTOR_SELF, 6, 0, 0, 0);
    PlayMessageSound();
#else
    TalkMdlStartInit_(ACTOR_SELF, 0);
    PlayMessageSound();
    ChrRotTarget_(ACTOR_SELF, -1, 8, 1, 1, 0);
    WaitForTurn();
#endif
}

stock OpenMessage(message)
{
#if defined SCENERY_INTERACTION
    TalkMsg_Seq(0, 0, message, 9, 1, 0, -168, 0, -1, 0, 0);
#else
    TalkMsg_Seq(0, ACTOR_SELF, message, 1, 1, 0, -168, 0, -1, 0, 0);
#endif
    while (!TalkMsg_SeqWait()) {
        WaitFrames(1);
    }
}

stock AdvanceAndClose()
{
#if defined SCENERY_INTERACTION
    LastKeyWait_();
#else
    TalkMsg_VisubleCursor(1);
    ABKeyWait_();
    TalkMsg_VisubleCursor(0);
    PlayMessageSound();
#endif
    MsgWinCloseNo(0);
}

stock AskYesNo(yes_message, no_message)
{
    while (!YesNoWin_Seq(1, yes_message, no_message, 0, 0)) {
        WaitFrames(1);
    }
    new answer = WorkGet(WORK_MENU_ANSWER);
    MsgWinCloseNo(0);
    return answer == 0;
}

stock EndConversation()
{
#if !defined SCENERY_INTERACTION
    TalkMdlEndInit_(ACTOR_SELF);
    ChrRot_(ACTOR_SELF, 8, TalkMdlGetRepairAngle_(), 1, 1, 0);
    WaitForTurn();
#endif
}

stock FacePlayer(actor = ACTOR_SELF)
{
    ChrRotTarget_(actor, -1, 8, 1, 1, 0);
}

stock RotateCharacter(actor, angle_bits)
{
    ChrRot_(actor, 8, angle_bits, 1, 1, 0);
}

stock WaitForAction(actor = ACTOR_SELF)
{
    while (ChrActionCommandIsPlaying_(actor)) {
        WaitFrames(1);
    }
}

stock WaitForMotion(actor = ACTOR_SELF)
{
    while (ChrMotionIsPlaying_(actor)) {
        WaitFrames(1);
    }
}


native ChrMovePos_(actor, mode, x_bits, y_bits, z_bits, angle_bits,
                   argument_7, turn_threshold_bits, argument_9, argument_10, argument_11);
native ItemAddCheck(item, quantity);
native ItemAdd(item, quantity);
native LoadFieldDLL_();
native CallWildBattleCore(encounter, flags, return_music);
native GetWildBattleResult();
native ResetEventBasePos();
native ChangeEventWildLose_();
native FadeIn_(frames, argument_2, argument_3);
native FadeWait_();

stock MoveCharacterTo(actor, x_bits, y_bits, z_bits, angle_bits, turn_threshold_bits)
{
    ResetEventBasePos();
    ChrMovePos_(actor, 0, x_bits, y_bits, z_bits, angle_bits, 0, turn_threshold_bits, 0, 1, 0);
}

stock GiveItem(item, quantity)
{
    if (!ItemAddCheck(item, quantity)) {
        return 0;
    }
    return ItemAdd(item, quantity);
}

stock StartEncounter(encounter)
{
    EndConversation();
    LoadFieldDLL_();
    CallWildBattleCore(encounter, 0, 0);
    new result = GetWildBattleResult();
    if (result != 0) {
        FadeIn_(20, 2, 0);
        while (!FadeWait_()) {
            WaitFrames(1);
        }
#if !defined SCENERY_INTERACTION
        TalkMdlStartInit_(ACTOR_SELF, 0);
#endif
    }
    return result;
}

stock RecoverFromDefeat()
{
    ResetEventBasePos();
    ChangeEventWildLose_();
}


native CallTrainerBattleCore(partner, trainer, second_trainer, flags,
                            return_music, argument_6, argument_7, argument_8);
native BattleGetResult_(result[]);
native ChangeEventTrainerLose_();

stock StartTrainerBattle(trainer)
{
    EndConversation();
    LoadFieldDLL_();
    CallTrainerBattleCore(0, trainer, 0, 0, 0, 0, 0, 0);
    new result[9];
    BattleGetResult_(result);
    if (result[0] != 0) {
        FadeIn_(20, 2, 0);
        while (!FadeWait_()) {
            WaitFrames(1);
        }
#if !defined SCENERY_INTERACTION
        TalkMdlStartInit_(ACTOR_SELF, 0);
#endif
    }
    return result[0];
}

stock RecoverFromTrainerDefeat()
{
    ResetEventBasePos();
    ChangeEventTrainerLose_();
}


native ListMenuInit_Seq(x, y, argument_3, argument_4, argument_5, argument_6);
native ListMenuAdd(message, result, argument_3);
native ListMenuStart_Seq();
native PG_WordSetRegister(kind, slot, value, count);
native WordSetNumber(slot, value, digits, display);

stock BeginChoiceMenu()
{
    while (!ListMenuInit_Seq(180, 3, 0, 0, 0, 0)) {
        WaitFrames(1);
    }
}

stock AddChoiceOption(message, result)
{
    ListMenuAdd(message, result, 0);
}

stock FinishChoiceMenu()
{
    while (!ListMenuStart_Seq()) {
        WaitFrames(1);
    }
    new answer = WorkGet(WORK_MENU_ANSWER);
    MsgWinCloseNo(0);
    return answer;
}

stock SetMessagePlayer(slot)
{
    PG_WordSetRegister(1, slot, 0, 0);
}

stock SetMessagePokemon(slot, species)
{
    PG_WordSetRegister(2, slot, species, 0);
}

stock SetMessageItem(slot, item)
{
    PG_WordSetRegister(10, slot, item, 1);
}

stock SetMessageNumber(slot, value)
{
    WordSetNumber(slot, value, 0, 0);
}
