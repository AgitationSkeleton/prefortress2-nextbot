// pf2-nextbot: PF2's melee team support (TFC heritage), layered over TFBot like the grenade module.
//  * Medic: the melee syringe (tf_weapon_syringe) heals a teammate 50 and cures burning, infection,
//    tranquilizer and leg damage; on an enemy it infects (8 dmg / 2 s, ignores armor).
//  * Engineer: the wrench repairs a teammate's armor (up to 50 per hit, 1 metal per 5 armor).
// Runs only when no enemy is in view; while it acts, TFBot's own weapon choice stands aside.
#pragma once
class CTFBot;
class CUserCmd;

void PF2Support_Think(CTFBot *bot, CUserCmd *cmd);
bool PF2Support_IsBusy(const CTFBot *bot);   // TFBot's EquipBestWeaponForThreat defers while true
void PF2Support_Forget(const CTFBot *bot);
