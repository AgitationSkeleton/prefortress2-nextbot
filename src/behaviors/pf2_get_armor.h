// pf2-nextbot: PF2 armor. Every class but the Civilian has armor that absorbs part of the damage
// (pf_armor_absorption_*); item_armor pickups give +200 up to the class max and resupply cabinets refill it.
// Modelled on CTFBotGetHealth: the tactical monitor suspends into this when armor is low.
#pragma once
#include "bot/tf_bot.h"
#include "Path/NextBotPathFollow.h"

class CTFBotPF2GetArmor : public Action<CTFBot>
{
public:
	static bool IsPossible(CTFBot *me);

	virtual ActionResult<CTFBot> OnStart(CTFBot *me, Action<CTFBot> *priorAction);
	virtual ActionResult<CTFBot> Update(CTFBot *me, float interval);
	virtual EventDesiredResult<CTFBot> OnStuck(CTFBot *me);
	virtual EventDesiredResult<CTFBot> OnMoveToFailure(CTFBot *me, const Path *path, MoveToFailureType reason);
	virtual QueryResultType ShouldHurry(const INextBot *me) const { return ANSWER_YES; }
	virtual const char *GetName(void) const { return "GetArmor"; }

private:
	PathFollower m_path;
	CHandle<CBaseEntity> m_item;
};
