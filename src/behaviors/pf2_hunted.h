// pf2-nextbot: PF2 hunted/VIP maps.
//
// Per-map rules come from addons/pf2nextbot/data/hunted.cfg (generated from BSP evidence by
// tools/hunted_rules.py; editable): classes each team may play, the Civilian VIP's goal, an optional
// "stand still" lift, and the gate door plus the buttons that open it.
//   * If BLU has no Civilian, one BLU bot becomes the VIP (via its class choice).
//   * The VIP waits until the gate door is open, then heads for its goal (or stands on the lift).
//   * BLU bodyguards touch/press the gate buttons until the door opens, then escort the VIP.
#pragma once
#include "bot/tf_bot.h"
#include "Path/NextBotPathFollow.h"

struct PF2HuntedRules
{
	bool active = false;
	unsigned classMask[TF_TEAM_COUNT] = {};   // bit (class-1); 0 = no rule for that team
	bool hasGoal = false;
	Vector goalMins, goalMaxs;
	bool goalWait = false;
	char gateDoor[64] = "";
	CUtlVector<Vector> buttons;
};

const PF2HuntedRules &PF2Hunted_Rules();
void PF2Hunted_LoadForMap(const char *mapname);   // also loads ladders
bool PF2Hunted_ClassAllowed(int team, int tfclass, bool *hasRule);
bool PF2Hunted_WantsBotAsVIP(const CTFBot *bot);    // class choice: this bot should be the Civilian VIP
bool PF2Hunted_GateOpen();
CTFPlayer *PF2Hunted_FindVIP(int team);
Action<CTFBot> *PF2Hunted_ScenarioAction(CTFBot *me);  // NULL if hunted rules do not apply to this bot

class CTFBotPF2HuntedVIP : public Action<CTFBot>
{
public:
	virtual ActionResult<CTFBot> OnStart(CTFBot *me, Action<CTFBot> *priorAction);
	virtual ActionResult<CTFBot> Update(CTFBot *me, float interval);
	virtual EventDesiredResult<CTFBot> OnStuck(CTFBot *me);
	virtual QueryResultType ShouldRetreat(const INextBot *me) const { return ANSWER_NO; }   // keep going
	virtual QueryResultType ShouldAttack(const INextBot *me, const CKnownEntity *them) const { return ANSWER_NO; }
	virtual const char *GetName(void) const { return "PF2HuntedVIP"; }
private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
};

class CTFBotPF2HuntedBodyguard : public Action<CTFBot>
{
public:
	virtual ActionResult<CTFBot> OnStart(CTFBot *me, Action<CTFBot> *priorAction);
	virtual ActionResult<CTFBot> Update(CTFBot *me, float interval);
	virtual EventDesiredResult<CTFBot> OnStuck(CTFBot *me);
	virtual const char *GetName(void) const { return "PF2HuntedBodyguard"; }
private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	int m_button = -1;
	int m_unreachable = 0;
};
