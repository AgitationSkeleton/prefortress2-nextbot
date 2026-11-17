// pf2-nextbot: TFBot behavior for the defending team on PF2 flag Attack/Defend maps
// (item_teamflag with m_nGameType == TF_FLAGTYPE_ATTACK_DEFEND). TF2 has no CTF defence behavior;
// without this, defenders fall back to SeekAndDestroy. Combat itself stays with TFBot's MainAction.
#pragma once
#include "bot/tf_bot.h"
#include "Path/NextBotPathFollow.h"

class CTFBotPF2DefendFlag : public Action<CTFBot>
{
public:
	virtual ActionResult<CTFBot> OnStart(CTFBot *me, Action<CTFBot> *priorAction);
	virtual ActionResult<CTFBot> Update(CTFBot *me, float interval);
	virtual ActionResult<CTFBot> OnResume(CTFBot *me, Action<CTFBot> *interruptingAction);
	virtual EventDesiredResult<CTFBot> OnStuck(CTFBot *me);
	virtual QueryResultType ShouldHurry(const INextBot *me) const;
	virtual const char *GetName(void) const { return "PF2DefendFlag"; }

	static CCaptureFlag *FlagToDefend(CTFBot *me);   // an A/D flag the enemy team carries/moves

private:
	Vector ChooseGuardSpot(CTFBot *me, CCaptureFlag *flag);
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_guardTimer;
	Vector m_guardSpot;
	bool m_haveGuardSpot;
};
