// pf2-nextbot: PF2 hand grenades for TFBots.
//
// PF2 grenades are primed by holding IN_GRENADE1/IN_GRENADE2 and thrown on release (the game forces
// pf_grenade_press_throw 0 for its bots). Timing follows the GoldSrc foxbot model: hold at least
// 0.8 s, release when the remaining fuse roughly matches the flight time, never hold past a safety
// margin. Which grenades each class carries comes from PF2's class scripts.
#include "cbase.h"
#include "botdriver.h"
#include "pf2_grenades.h"
#include "bot/tf_bot.h"
#include "NextBotVisionInterface.h"

ConVar tf_bot_grenades("tf_bot_grenades", "1", FCVAR_NONE, "Allow TFBots to use PF2 hand grenades");
ConVar tf_bot_grenade_fuse("tf_bot_grenade_fuse", "3.5", FCVAR_NONE, "Grenade fuse the bots plan with (seconds)");
ConVar tf_bot_grenade_throw_speed("tf_bot_grenade_throw_speed", "700", FCVAR_NONE, "Approximate grenade travel speed used for timing (units/s)");
ConVar tf_bot_grenade_min_range("tf_bot_grenade_min_range", "250", FCVAR_NONE);
ConVar tf_bot_grenade_max_range("tf_bot_grenade_max_range", "1200", FCVAR_NONE);

enum GrenadeRole { GREN_NONE, GREN_OFFENSIVE, GREN_HEAL, GREN_AREA_DENIAL, GREN_ESCAPE };

static GrenadeRole RoleOf(int weaponId)
{
	switch (weaponId)
	{
	case TF_WEAPON_GRENADE_HEAL: return GREN_HEAL;
	case TF_WEAPON_GRENADE_CALTROP: return GREN_AREA_DENIAL;
	case TF_WEAPON_GRENADE_SMOKE_BOMB: return GREN_ESCAPE;
	case 0: return GREN_NONE;
	default: return GREN_OFFENSIVE;   // normal, concussion, nail, mirv, napalm, gas, emp, pipebomb, demoman
	}
}

struct GrenadeState
{
	CTFBot *bot = NULL;
	int slot = -1;            // 0/1 while priming
	float primeStart = 0.0f;
	float holdFor = 0.0f;
	float nextConsider = 0.0f;
	int releaseTicks = 0;     // keep the throw angle for a couple of ticks after release
	QAngle throwAngles;
	Vector target;
};
static GrenadeState s_state[PF2NB_MAXPLAYERS + 1];

void PF2Grenades_Forget(CTFBot *bot)
{
	for (GrenadeState &g : s_state)
		if (g.bot == bot)
			g = GrenadeState();
}

static bool InNoGrenadeZone(const Vector &pos)
{
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "func_nogrenades"); e; e = gEntList.FindEntityByClassname(e, "func_nogrenades"))
	{
		if (!e->CollisionProp()->IsPointInBounds(pos))
			continue;
		Ray_t ray;
		ray.Init(pos, pos);
		trace_t tr;
		enginetrace->ClipRayToEntity(ray, MASK_ALL, (IHandleEntity *)e->GetRaw(), &tr);
		if (tr.startsolid)
			return true;
	}
	return false;
}

static float ConsiderChancePerSecond(CTFBot *bot)
{
	switch (bot->GetDifficulty())
	{
	case CTFBot::EASY: return 0.05f;
	case CTFBot::NORMAL: return 0.15f;
	case CTFBot::HARD: return 0.25f;
	default: return 0.35f;
	}
}

// pitch (degrees, negative = up) to lob a grenade a horizontal distance with a rough ballistic arc
static float LobPitch(float dist, float dz)
{
	float speed = tf_bot_grenade_throw_speed.GetFloat();
	static ConVarRef sv_gravity("sv_gravity");
	float g = sv_gravity.IsValid() ? sv_gravity.GetFloat() : 800.0f;
	// solve for the lower of the two launch angles; fall back to 45 degrees when out of range
	float v2 = speed * speed;
	float disc = v2 * v2 - g * (g * dist * dist + 2.0f * dz * v2);
	float angle = disc < 0.0f ? (float)M_PI_F / 4.0f : atanf((v2 - sqrtf(disc)) / (g * MAX(dist, 1.0f)));
	return -RAD2DEG(angle);
}

static bool PickThrow(CTFBot *bot, int *outSlot, Vector *outTarget)
{
	const PF2ClassData *cd = PF2_GetClassData(bot->GetPlayerClass()->GetClassIndex());
	if (!cd)
		return false;
	const Vector me = bot->GetAbsOrigin();
	const CKnownEntity *threat = bot->GetVisionInterface()->GetPrimaryKnownThreat(true);
	CBaseEntity *enemy = threat ? threat->GetEntity() : NULL;
	float enemyDist = enemy ? (enemy->GetAbsOrigin() - me).Length() : FLT_MAX;
	bool enemyVisible = threat && threat->IsVisibleRecently();

	for (int slot = 0; slot < 2; ++slot)
	{
		if (bot->GetGrenadeCount(slot) <= 0)
			continue;
		switch (RoleOf(cd->grenade[slot]))
		{
		case GREN_OFFENSIVE:
			if (enemy && enemyVisible && enemyDist >= tf_bot_grenade_min_range.GetFloat() && enemyDist <= tf_bot_grenade_max_range.GetFloat())
			{
				// lead slightly toward where the target is heading
				*outTarget = enemy->GetAbsOrigin() + enemy->GetAbsVelocity() * 0.5f;
				*outSlot = slot;
				return true;
			}
			break;
		case GREN_AREA_DENIAL:
			if (enemy && enemyVisible && enemyDist < 600.0f)
			{
				*outTarget = me + (enemy->GetAbsOrigin() - me) * 0.5f;   // between us
				*outSlot = slot;
				return true;
			}
			break;
		case GREN_ESCAPE:
			if (enemy && enemyVisible && enemyDist < 500.0f && bot->GetHealth() < bot->GetMaxHealth() / 2)
			{
				*outTarget = me;
				*outSlot = slot;
				return true;
			}
			break;
		case GREN_HEAL:
		{
			int hurt = bot->GetHealth() < bot->GetMaxHealth() * 0.6f ? 1 : 0;
			Vector centre = me * (float)hurt;
			for (int i = 1; i <= gpGlobals->maxClients; ++i)
			{
				CTFPlayer *p = ToTFPlayer(UTIL_PlayerByIndex(i));
				if (!p || p == bot || !p->IsAlive() || p->GetTeamNumber() != bot->GetTeamNumber())
					continue;
				if ((p->GetAbsOrigin() - me).IsLengthGreaterThan(700.0f) || p->GetHealth() >= p->GetMaxHealth() * 0.6f)
					continue;
				centre += p->GetAbsOrigin();
				++hurt;
			}
			if (hurt >= 2)
			{
				*outTarget = centre / (float)hurt;
				*outSlot = slot;
				return true;
			}
			break;
		}
		default:
			break;
		}
	}
	return false;
}

void PF2Grenades_Think(CTFBot *bot, CUserCmd *cmd)
{
	int idx = bot->entindex();
	if (idx < 1 || idx > PF2NB_MAXPLAYERS || !tf_bot_grenades.GetBool())
		return;
	GrenadeState &g = s_state[idx];
	if (g.bot != bot)
	{
		g = GrenadeState();
		g.bot = bot;
	}
	const float now = gpGlobals->curtime;

	if (g.releaseTicks > 0)
	{
		cmd->viewangles = g.throwAngles;
		--g.releaseTicks;
		return;
	}

	if (g.slot >= 0)
	{
		// priming: keep holding until the planned release time (or the safety margin)
		float held = now - g.primeStart;
		float safety = tf_bot_grenade_fuse.GetFloat() - 0.6f;
		if (held < g.holdFor && held < safety && bot->IsAlive())
		{
			cmd->buttons |= (g.slot == 0) ? IN_GRENADE1 : IN_GRENADE2;
			return;
		}
		// release: aim the lob at the target this tick and the next two
		Vector eye = bot->EyePosition();
		Vector to = g.target - eye;
		float dist = to.Length2D();
		QAngle a;
		VectorAngles(to, a);
		a.x = LobPitch(dist, to.z);
		g.throwAngles = a;
		cmd->viewangles = a;
		g.releaseTicks = 2;
		g.slot = -1;
		g.nextConsider = now + 2.0f;
		return;
	}

	if (now < g.nextConsider || now < bot->GetNextThrowTime() || bot->IsGrenadePrimed())
		return;
	g.nextConsider = now + 0.5f;
	if (!bot->IsAlive() || bot->m_Shared.InCond(TF_COND_STEALTHED) || bot->HasTheFlag() && RandomFloat() < 0.5f)
		return;
	if (RandomFloat() > ConsiderChancePerSecond(bot) * 0.5f)
		return;
	if (InNoGrenadeZone(bot->GetAbsOrigin()))
		return;

	int slot;
	Vector target;
	if (!PickThrow(bot, &slot, &target))
		return;
	if (InNoGrenadeZone(target))
		return;

	float dist = (target - bot->GetAbsOrigin()).Length();
	float flight = dist / MAX(1.0f, tf_bot_grenade_throw_speed.GetFloat());
	float fuse = tf_bot_grenade_fuse.GetFloat();
	g.slot = slot;
	g.target = target;
	g.primeStart = now;
	g.holdFor = clamp(fuse - flight - 0.4f, 0.8f, fuse - 0.6f);
	cmd->buttons |= (slot == 0) ? IN_GRENADE1 : IN_GRENADE2;
}
