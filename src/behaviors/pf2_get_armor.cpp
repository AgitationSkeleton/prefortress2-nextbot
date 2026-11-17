// pf2-nextbot: fetch armor (see pf2_get_armor.h)
#include "cbase.h"
#include "pf2_get_armor.h"
#include "nav_mesh.h"
#include "tf_nav_mesh.h"

ConVar tf_bot_pf2_armor_ratio("tf_bot_pf2_armor_ratio", "0.5", FCVAR_NONE, "Bots look for armor below this fraction of their class's max armor");
ConVar tf_bot_pf2_armor_search_range("tf_bot_pf2_armor_search_range", "1500", FCVAR_NONE, "How far (travel distance) bots go for armor when it is low; twice as far when nearly gone");

static bool ArmorLow(CTFBot *me, float ratio)
{
	int maxArmor = me->GetMaxArmor();
	return maxArmor > 0 && me->GetArmorValue() < ratio * maxArmor;
}

class CArmorFilter : public INextBotFilter
{
public:
	CArmorFilter(CTFBot *me) : m_me(me) {}
	bool IsSelected(const CBaseEntity *constCandidate) const
	{
		CBaseEntity *candidate = const_cast<CBaseEntity *>(constCandidate);
		if (!candidate)
			return false;
		CTFNavArea *area = (CTFNavArea *)TheNavMesh->GetNearestNavArea(candidate->WorldSpaceCenter());
		if (!area)
			return false;
		CClosestTFPlayer close(candidate);
		ForEachPlayer(close);
		if (close.m_closePlayer && !m_me->InSameTeam(close.m_closePlayer))
			return false;   // an enemy is closer to it
		if (candidate->ClassMatches("func_regenerate"))
			return (m_me->GetTeamNumber() == TF_TEAM_RED && area->HasAttributeTF(TF_NAV_SPAWN_ROOM_RED)) ||
				   (m_me->GetTeamNumber() == TF_TEAM_BLUE && area->HasAttributeTF(TF_NAV_SPAWN_ROOM_BLUE));
		if (candidate->IsEffectActive(EF_NODRAW))
			return false;   // taken, waiting to respawn
		return candidate->GetTeamNumber() != GetEnemyTeam(m_me->GetTeamNumber());
	}
	CTFBot *m_me;
};

static CTFBot *s_possibleBot = NULL;
static CHandle<CBaseEntity> s_possibleItem;
static int s_possibleFrame = 0;

bool CTFBotPF2GetArmor::IsPossible(CTFBot *me)
{
	if (!ArmorLow(me, tf_bot_pf2_armor_ratio.GetFloat()))
		return false;
	static ConVarRef armorEnabled("pf_armor_enabled");
	if (armorEnabled.IsValid() && !armorEnabled.GetBool())
		return false;
	if (me->HasTheFlag())
		return false;
	float range = tf_bot_pf2_armor_search_range.GetFloat();
	if (ArmorLow(me, 0.2f))
		range *= 2.0f;
	CUtlVector<CHandle<CBaseEntity> > found;
	CArmorFilter filter(me);
	me->SelectReachableObjects(TFGameRules()->GetArmorEntityVector(), &found, filter, me->GetLastKnownArea(), range);
	if (found.Count() == 0)
		return false;
	CTFBotPathCost cost(me, FASTEST_ROUTE);
	PathFollower path;
	if (!path.Compute(me, found[0]->WorldSpaceCenter(), cost))
		return false;
	s_possibleBot = me;
	s_possibleItem = found[0];
	s_possibleFrame = gpGlobals->framecount;
	return true;
}

ActionResult<CTFBot> CTFBotPF2GetArmor::OnStart(CTFBot *me, Action<CTFBot> *priorAction)
{
	m_path.SetMinLookAheadDistance(me->GetDesiredPathLookAheadRange());
	if ((s_possibleFrame != gpGlobals->framecount || s_possibleBot != me) && (!IsPossible(me) || s_possibleItem == NULL))
		return Done("Can't get armor");
	m_item = s_possibleItem;
	CTFBotPathCost cost(me, SAFEST_ROUTE);
	if (!m_path.Compute(me, m_item->WorldSpaceCenter(), cost))
		return Done("No path to armor");
	return Continue();
}

ActionResult<CTFBot> CTFBotPF2GetArmor::Update(CTFBot *me, float interval)
{
	if (m_item == NULL || (m_item->IsEffectActive(EF_NODRAW) && !m_item->ClassMatches("func_regenerate")))
		return Done("The armor was taken");
	if (me->GetArmorValue() >= me->GetMaxArmor())
		return Done("Armor full");
	CClosestTFPlayer close(m_item);
	ForEachPlayer(close);
	if (close.m_closePlayer && !me->InSameTeam(close.m_closePlayer))
		return Done("An enemy is closer to it");
	if (!m_path.IsValid())
	{
		CTFBotPathCost cost(me, SAFEST_ROUTE);
		if (!m_path.Compute(me, m_item->WorldSpaceCenter(), cost))
			return Done("No path to armor");
	}
	m_path.Update(me);
	me->EquipBestWeaponForThreat(me->GetVisionInterface()->GetPrimaryKnownThreat());
	return Continue();
}

EventDesiredResult<CTFBot> CTFBotPF2GetArmor::OnStuck(CTFBot *me)
{
	return TryDone(RESULT_CRITICAL, "Stuck trying to reach armor");
}

EventDesiredResult<CTFBot> CTFBotPF2GetArmor::OnMoveToFailure(CTFBot *me, const Path *path, MoveToFailureType reason)
{
	return TryDone(RESULT_CRITICAL, "Failed to reach armor");
}
