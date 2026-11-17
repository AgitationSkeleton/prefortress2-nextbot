// pf2-nextbot: PF2 hand grenades (TFC-style prime-and-throw) for TFBots.
#pragma once
class CTFBot;
class CUserCmd;
void PF2Grenades_Think(CTFBot *bot, CUserCmd *cmd);   // called after the bot built its usercmd
void PF2Grenades_Forget(CTFBot *bot);
