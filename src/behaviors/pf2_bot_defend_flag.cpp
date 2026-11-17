// pf2-nextbot: defending team on PF2 flag Attack/Defend maps. See pf2_bot_defend_flag.h.
#include "cbase.h"
#include "pf2_bot_defend_flag.h"
#include "tf_nav_mesh.h"
#include "nav_mesh.h"

ConVar tf_bot_pf2_flag_guard_radius("tf_bot_pf2_flag_guard_radius", "500", FCVAR_NONE, "How far from the objective PF2 A/D defenders take up guard positions");

// TF2 semantics (CTFBot::GetFlagToFetch): an Attack/Defend flag is moved by the team that owns it
// (or anyone, if neutral). The other team defends it.
CCaptureFlag *CTFBotPF2DefendFlag::FlagToDefend(CTFBot *me)
{
	for (int i = 0; i < ICaptureFlagAutoList::AutoList().Count(); ++i)
	{
		CCaptureFlag *flag = static_cast<CCaptureFlag *>(ICaptureFlagAutoList::AutoList()[i]);
		if (flag->IsDisabled() || flag->GetType() != TF_FLAGTYPE_ATTACK_DEFEND)
			continue;
		if (flag->GetTeamNumber() == GetEnemyTeam(me->GetTeamNumber()))
			return flag;
	}
	return NULL;
}

// Guard where the attackers must go: their capture zone if the map has one, otherwise the flag.
Vector CTFBotPF2DefendFlag::ChooseGuardSpot(CTFBot *me, CCaptureFlag *flag)
{
	Vector objective = flag->GetAbsOrigin();
	float best = FLT_MAX;
	for (int i = 0; i < ICaptureZoneAutoList::AutoList().Count(); ++i)
	{
		CCaptureZone *zone = static_cast<CCaptureZone *>(ICaptureZoneAutoList::AutoList()[i]);
		if (zone->IsDisabled())
			continue;
		int t = zone->GetTeamNumber();
		if (t != TEAM_UNASSIGNED && t != flag->GetTeamNumber())
			continue;   // not a zone the attackers capture at
		float d = (zone->WorldSpaceCenter() - flag->GetAbsOrigin()).LengthSqr();
		if (d < best)
		{
			best = d;
			objective = zone->WorldSpaceCenter();
		}
	}

	// a random reachable nav area near the objective
	CNavArea *centre = TheNavMesh->GetNearestNavArea(objective, false, 500.0f, false, true);
	if (!centre)
		return objective;
	CUtlVector<CNavArea *> nearby;
	CollectSurroundingAreas(&nearby, centre, tf_bot_pf2_flag_guard_radius.GetFloat(), StepHeight, StepHeight);
	for (int tries = 0; tries < 8 && nearby.Count(); ++tries)
	{
		CNavArea *a = nearby[RandomInt(0, nearby.Count() - 1)];
		if (me->GetLocomotionInterface()->IsAreaTraversable(a))
			return a->GetCenter();
	}
	return centre->GetCenter();
}

ActionResult<CTFBot> CTFBotPF2DefendFlag::OnStart(CTFBot *me, Action<CTFBot> *priorAction)
{
	m_path.SetMinLookAheadDistance(me->GetDesiredPathLookAheadRange());
	m_haveGuardSpot = false;
	m_repathTimer.Invalidate();
	m_guardTimer.Invalidate();
	return Continue();
}

ActionResult<CTFBot> CTFBotPF2DefendFlag::OnResume(CTFBot *me, Action<CTFBot> *interruptingAction)
{
	m_repathTimer.Invalidate();
	return Continue();
}

ActionResult<CTFBot> CTFBotPF2DefendFlag::Update(CTFBot *me, float interval)
{
	CCaptureFlag *flag = FlagToDefend(me);
	if (!flag)
		return Done("No attack/defend flag to defend");

	Vector goal;
	if (flag->IsStolen())
	{
		// run down the carrier; TFBot's MainAction fires at visible threats on its own
		CBaseEntity *carrier = flag->GetCarrier();
		goal = carrier ? carrier->GetAbsOrigin() : flag->GetAbsOrigin();
		m_haveGuardSpot = false;
	}
	else if (flag->IsDropped())
	{
		goal = flag->GetAbsOrigin();   // deny the dropped flag until it returns
		m_haveGuardSpot = false;
	}
	else
	{
		if (!m_haveGuardSpot || m_guardTimer.IsElapsed())
		{
			m_guardSpot = ChooseGuardSpot(me, flag);
			m_haveGuardSpot = true;
			m_guardTimer.Start(RandomFloat(15.0f, 30.0f));
			m_repathTimer.Invalidate();
		}
		goal = m_guardSpot;
		if (me->IsRangeLessThan(goal, 50.0f))
		{
			// on station: watch the approaches instead of walking in circles
			me->UpdateLookingAroundForIncomingPlayers(true);
			return Continue();
		}
	}

	if (m_repathTimer.IsElapsed())
	{
		m_repathTimer.Start(flag->IsStolen() ? 0.5f : 2.0f);
		CTFBotPathCost cost(me, FASTEST_ROUTE);
		m_path.Compute(me, goal, cost);
	}
	m_path.Update(me);
	return Continue();
}

EventDesiredResult<CTFBot> CTFBotPF2DefendFlag::OnStuck(CTFBot *me)
{
	m_haveGuardSpot = false;
	m_repathTimer.Invalidate();
	return TryContinue();
}

QueryResultType CTFBotPF2DefendFlag::ShouldHurry(const INextBot *me) const
{
	return ANSWER_UNDEFINED;
}
