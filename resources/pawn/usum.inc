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

native ItemGetNum(item);
native ItemSub(item, quantity);
native PokePartyGetCount(mode);
native BoxIsFull();
native PokemonAdd(gift, argument_2, argument_3, argument_4, argument_5, argument_6);
native FadeOut_(argument_1, frames, argument_3, argument_4);
native FieldClose_(argument_1, argument_2);
native FieldOpen_(argument_1, argument_2);
native FieldTradeExcelLoad();
native FieldTradeExcelIsLoaded();
native FieldTradeExcelGetParam(trade, parameter);
native FieldTradeExcelEnd();
native CallBoxFieldTrade(species, sex);
native GetSelectMonsIsEgg(selection_1, selection_2);
native GetSelectMonsNo(selection_1, selection_2);
native GetSelectMonsSex(selection_1, selection_2);
native GetSelectMonsSeikaku(selection_1, selection_2);
native GetSelectMonsPlace(selection_1, selection_2);
native GetSelectMonsIsCapture(selection_1, selection_2);
native GetSelectMonsWazaNum(selection_1, selection_2);
native GetSelectMonsWaza(selection_1, selection_2, slot);
native PokeTradeLoad();
native PokeTradeIsLoaded(selection_1, selection_2, trade);
native PokeTradeExec();
native PokeTradeFin();

stock TradeItems(requested_item, requested_quantity, offered_item, offered_quantity)
{
    if (requested_item <= 0 || requested_item >= 1024 || offered_item <= 0 || offered_item >= 1024 ||
        requested_item == offered_item || requested_quantity < 1 || requested_quantity > 999 ||
        offered_quantity < 1 || offered_quantity > 999) return 0;
    if (ItemGetNum(requested_item) < requested_quantity) return 0;
    if (!ItemSub(requested_item, requested_quantity)) return 0;
    if (ItemAddCheck(offered_item, offered_quantity) && ItemAdd(offered_item, offered_quantity)) return 1;
    // No yield occurs between removal and refund.
    ItemAdd(requested_item, requested_quantity);
    return 0;
}

stock GivePokemon(gift)
{
    if (gift < 0 || (PokePartyGetCount(0) >= 6 && BoxIsFull())) return 0;
    FadeOut_(0, 8, 2, 0);
    while (!FadeWait_()) WaitFrames(1);
    PokemonAdd(gift, 0, -1, 0, 2, 1);
    return 1;
}

stock CloseTradeField()
{
    FadeOut_(0, 20, 2, 0);
    while (!FadeWait_()) WaitFrames(1);
    FieldClose_(0, 0);
}

stock OpenTradeField()
{
    FieldOpen_(0, 0);
    FadeIn_(20, 2, 0);
    while (!FadeWait_()) WaitFrames(1);
}

stock TradePokemon(trade)
{
    if (trade < 0 || PokePartyGetCount(2) < 2) return 0;
    FieldTradeExcelLoad();
    while (!FieldTradeExcelIsLoaded()) WaitFrames(1);
    new species = FieldTradeExcelGetParam(trade, 29);
    new sex = FieldTradeExcelGetParam(trade, 30);
    new nature = FieldTradeExcelGetParam(trade, 31);
    new place = FieldTradeExcelGetParam(trade, 32);
    new move = FieldTradeExcelGetParam(trade, 33);
    FieldTradeExcelEnd();
    CloseTradeField();
    CallBoxFieldTrade(species, sex);
    new selection_result = WorkGet(32780);
    new selection_1 = WorkGet(32781), selection_2 = WorkGet(32782);
    OpenTradeField();
    if (selection_result == 254) return 0;
    if (GetSelectMonsIsEgg(selection_1, selection_2)) return 0;
    if (species > 0 && GetSelectMonsNo(selection_1, selection_2) != species) return 0;
    if (sex > 0 && sex < 3 && GetSelectMonsSex(selection_1, selection_2) != sex) return 0;
    if (nature > 0 && nature < 255 && GetSelectMonsSeikaku(selection_1, selection_2) != nature) return 0;
    if (place >= 0 && GetSelectMonsPlace(selection_1, selection_2) != place &&
        GetSelectMonsIsCapture(selection_1, selection_2)) return 0;
    if (move >= 0) {
        new found = 0;
        for (new slot = 0; slot < GetSelectMonsWazaNum(selection_1, selection_2); slot++) {
            if (move == 0 || GetSelectMonsWaza(selection_1, selection_2, slot) == move) found = 1;
        }
        if (!found) return 0;
    }
    PokeTradeLoad();
    while (!PokeTradeIsLoaded(selection_1, selection_2, trade)) WaitFrames(1);
    CloseTradeField();
    PokeTradeExec();
    OpenTradeField();
    PokeTradeFin();
    return 1;
}

native PokePartyGetParam(slot, parameter, argument);
native BoxMonsNoExists(species, include_eggs);
native PlayerGetMoney();
native PlayerAddMoney(amount);
native PlayerSubMoney(amount);

#define PARTY_PARAM_SPECIES 0
#define PARTY_PARAM_IS_EGG 8
#define WALLET_LIMIT 9999999

stock HasPokemon(species, include_boxes = 0)
{
    if (species <= 0 || species > 65535) return 0;
    new count = PokePartyGetCount(0);
    for (new slot = 0; slot < count; ++slot) {
        if (PokePartyGetParam(slot, PARTY_PARAM_IS_EGG, 0)) continue;
        if (PokePartyGetParam(slot, PARTY_PARAM_SPECIES, 0) == species) return 1;
    }
    if (include_boxes) return BoxMonsNoExists(species, 0);
    return 0;
}

stock HasItem(item, quantity = 1)
{
    if (item <= 0 || item >= 1024 || quantity <= 0 || quantity > 999) return 0;
    return ItemGetNum(item) >= quantity;
}

stock TakeItem(item, quantity)
{
    if (!HasItem(item, quantity)) return 0;
    return ItemSub(item, quantity);
}

stock GiveMoney(amount)
{
    if (amount <= 0 || amount > WALLET_LIMIT) return 0;
    if (PlayerGetMoney() > WALLET_LIMIT - amount) return 0;
    PlayerAddMoney(amount);
    return 1;
}

stock TakeMoney(amount)
{
    if (amount <= 0 || amount > WALLET_LIMIT) return 0;
    if (PlayerGetMoney() < amount) return 0;
    PlayerSubMoney(amount);
    return 1;
}


native PokePartyRecoverAll();
native ChrDisp(actor, visible);
native MapChangeCore_(zone, x_bits, y_bits, z_bits, force_end, fade,
                      keep_music, show_location, fixed_position, update_event_time);

stock HealParty()
{
    PokePartyRecoverAll();
}

stock ShowActor(actor = ACTOR_SELF)
{
    ChrDisp(actor, 1);
}

stock HideActor(actor = ACTOR_SELF)
{
    ChrDisp(actor, 0);
}

stock WarpPlayer(zone, x_bits, y_bits, z_bits)
{
    EndConversation();
    ResetEventBasePos();
    LoadFieldDLL_();
    // The destination takes over; no calls may target the old field afterward.
    MapChangeCore_(zone, x_bits, y_bits, z_bits, 1, 1, 0, 1, 1, 1);
}

stock AddWorkValue(work, amount)
{
    if (work < 16384 || work >= 49152 || amount < 0 || amount > 65535) return 0;
    new value = WorkGet(work);
    if (value < 0) value = 0;
    if (value > 65535 - amount) value = 65535;
    else value += amount;
    WorkSet(work, value);
    return value;
}

stock SubtractWorkValue(work, amount)
{
    if (work < 16384 || work >= 49152 || amount < 0 || amount > 65535) return 0;
    new value = WorkGet(work);
    if (value > 65535) value = 65535;
    if (value < amount) value = 0;
    else value -= amount;
    WorkSet(work, value);
    return value;
}

stock BuyItem(item, quantity, total_price)
{
    if (item <= 0 || item >= 1024 || quantity < 1 || quantity > 999 ||
        total_price < 1 || total_price > WALLET_LIMIT) return 0;
    if (PlayerGetMoney() < total_price || !ItemAddCheck(item, quantity)) return 0;
    if (!ItemAdd(item, quantity)) return 0;
    // Delivery and payment do not yield to another script.
    PlayerSubMoney(total_price);
    return 1;
}
