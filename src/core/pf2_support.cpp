// pf2-nextbot: PF2 melee team support (see pf2_support.h)
#include "cbase.h"
#include "botdriver.h"
#include "pf2_support.h"
#include "bot/tf_bot.h"
#include "NextBotVisionInterface.h"

ConVar tf_bot_pf2_team_support("tf_bot_pf2_team_support", "1", FCVAR_NONE, "Medics syringe-heal/cure and engineers wrench-repair the armor of nearby teammates");
ConVar tf_bot_pf2_team_support_range("tf_bot_pf2_team_support_range", "160", FCVAR_NONE, "How far a teammate may be for medic/engineer melee support");

struct SupportState
{
	const CTFBot *bot = NULL;
	EHANDLE target;
	float until = 0.0f;
	float nextLook = 0.0f;
};
static SupportState s_state[MAX_PLAYERS + 1];

static SupportState &StateOf(const CTFBot *bot)
{
	int i = bot ? bot->entindex() : 0;
	if (i < 0 || i > MAX_PLAYERS)
		i = 0;
	if (s_state[i].bot != bot)
	{
		s_state[i] = SupportState();
		s_state[i].bot = bot;
	}
	return s_state[i];
}

bool PF2Support_IsBusy(const CTFBot *bot)
{
	SupportState &s = StateOf(bot);
	return s.target.Get() != NULL && gpGlobals->curtime < s.until;
}

void PF2Support_Forget(const CTFBot *bot)
{
	int i = bot ? bot->entindex() : 0;
	if (i > 0 && i <= MAX_PLAYERS)
		s_state[i] = SupportState();
}

static bool MedicCanHelp(CTFPlayer *mate)
{
	return mate->m_Shared.InCond(TF_COND_INFECTED) || mate->m_Shared.InCond(TF_COND_BURNING) ||
		   mate->m_Shared.InCond(TF_COND_NAPALM_BURNING) || mate->m_Shared.InCond(TF_COND_TRANQUILIZED) ||
		   mate->m_Shared.InCond(TF_COND_LEG_DAMAGED) || mate->m_Shared.InCond(TF_COND_PF2_LEGSHOT) ||
		   mate->GetHealth() < mate->GetMaxHealth() - 25;
}

static bool EngineerCanHelp(CTFBot *me, CTFPlayer *mate)
{
	return mate->GetMaxArmor() > 0 && mate->GetArmorValue() < mate->GetMaxArmor() - 10 && me->GetAvailableMetal() >= 5;
}

static CTFWeaponBase *SupportWeapon(CTFBot *me, int wantId)
{
	CTFWeaponBase *w = dynamic_cast<CTFWeaponBase *>(me->Weapon_GetSlot(TF_WPN_TYPE_MELEE));
	return (w && w->GetWeaponID() == wantId) ? w : NULL;
}

void PF2Support_Think(CTFBot *me, CUserCmd *cmd)
{
	SupportState &s = StateOf(me);
	if (!tf_bot_pf2_team_support.GetBool() || !me->IsAlive() || me->IsGrenadePrimed() || me->HasTheFlag())
	{
		s.target = NULL;
		return;
	}
	bool medic = me->IsPlayerClass(TF_CLASS_MEDIC), engineer = me->IsPlayerClass(TF_CLASS_ENGINEER);
	if (!medic && !engineer)
		return;
	CTFWeaponBase *tool = SupportWeapon(me, medic ? TF_WEAPON_SYRINGE : TF_WEAPON_WRENCH);
	if (!tool)
		return;

	// only between fights
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat(true);
	if (threat && threat->IsVisibleRecently())
	{
		s.target = NULL;
		return;
	}
	// engineers busy placing a building keep their blueprint
	if (engineer && me->m_Shared.GetActiveTFWeapon() && me->m_Shared.GetActiveTFWeapon()->IsWeapon(TF_WEAPON_BUILDER))
		return;

	float range = tf_bot_pf2_team_support_range.GetFloat();
	CTFPlayer *target = ToTFPlayer(s.target.Get());
	if (target && (!target->IsAlive() || !me->IsRangeLessThan(target, range) ||
				   !(medic ? MedicCanHelp(target) : EngineerCanHelp(me, target))))
		target = NULL;
	if (!target && gpGlobals->curtime >= s.nextLook)
	{
		s.nextLook = gpGlobals->curtime + 0.5f;
		float best = range;
		for (int i = 1; i <= gpGlobals->maxClients; ++i)
		{
			CTFPlayer *p = ToTFPlayer(UTIL_PlayerByIndex(i));
			if (!p || p == me || !p->IsAlive() || p->GetTeamNumber() != me->GetTeamNumber())
				continue;
			if (!(medic ? MedicCanHelp(p) : EngineerCanHelp(me, p)))
				continue;
			float d = (p->WorldSpaceCenter() - me->WorldSpaceCenter()).Length();
			if (d < best && me->IsLineOfSightClear(p))
			{
				best = d;
				target = p;
			}
		}
	}
	s.target = target;
	if (!target)
		return;
	s.until = gpGlobals->curtime + 0.5f;

	if (me->GetActiveWeapon() != tool)
		me->Weapon_Switch(tool);

	// face the teammate and close in to melee reach
	Vector to = target->WorldSpaceCenter() - me->EyePosition();
	QAngle a;
	VectorAngles(to, a);
	cmd->viewangles = a;
	float dist2D = to.AsVector2D().Length();
	cmd->forwardmove = dist2D > 50.0f ? 300.0f : 0.0f;
	cmd->sidemove = 0.0f;
	if (dist2D < 70.0f && me->GetActiveWeapon() == tool)
		cmd->buttons |= IN_ATTACK;
}
