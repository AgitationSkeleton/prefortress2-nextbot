// pf2-nextbot: PF2 hunted/VIP maps and per-map ladders. See pf2_hunted.h.
#include "cbase.h"
#include "pf2_hunted.h"
#include "nav_mesh.h"
#include "nav_ladder.h"
#include "tf_nav_mesh.h"
#include "bot/behavior/tf_bot_escort.h"
#include "bot/behavior/tf_bot_seek_and_destroy.h"

ConVar tf_bot_pf2_hunted_rules("tf_bot_pf2_hunted_rules", "1", FCVAR_NONE, "Apply addons/pf2nextbot/data/hunted.cfg (class limits, VIP objectives) on hunted maps");
ConVar tf_bot_pf2_hunted_bot_vip("tf_bot_pf2_hunted_bot_vip", "1", FCVAR_NONE, "On hunted maps, a BLU bot plays the Civilian VIP when no one else does");
ConVar tf_bot_pf2_ladders("tf_bot_pf2_ladders", "1", FCVAR_NONE, "Add nav ladders from addons/pf2nextbot/data/ladders.cfg (PF2 info_ladder volumes) at map load");

static PF2HuntedRules s_rules;
const PF2HuntedRules &PF2Hunted_Rules() { return s_rules; }

static const char *s_classKeys[] = { "scout", "sniper", "soldier", "demoman", "medic", "heavyweapons", "pyro", "spy", "engineer", "civilian" };

static bool ParseBox(const char *s, Vector *mins, Vector *maxs)
{
	float v[6];
	if (!s || sscanf(s, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
		return false;
	mins->Init(MIN(v[0], v[3]), MIN(v[1], v[4]), MIN(v[2], v[5]));
	maxs->Init(MAX(v[0], v[3]), MAX(v[1], v[4]), MAX(v[2], v[5]));
	return true;
}

static void LoadLadders(const char *mapname)
{
	if (!tf_bot_pf2_ladders.GetBool() || !TheNavMesh)
		return;
	KeyValues *kv = new KeyValues("PF2Ladders");
	int n = 0;
	if (kv->LoadFromFile(filesystem, "addons/pf2nextbot/data/ladders.cfg", "MOD"))
	{
		if (KeyValues *map = kv->FindKey(mapname))
		{
			// ladders saved in the .nav (e.g. by pf2nb_nav_seed_ladders) are replaced by fresh ones, so each
			// connects to the areas that exist now (the generator saves them unconnected)
			TheNavMesh->PF2_DestroyLadders();
			for (KeyValues *l = map->GetFirstValue(); l; l = l->GetNextValue())
			{
				Vector mins, maxs;
				if (ParseBox(l->GetString(), &mins, &maxs))
				{
					TheNavMesh->CreateLadder(mins, maxs, 0.0f);
					++n;
				}
			}
		}
	}
	kv->deleteThis();
	if (n)
		Msg("[PF2NB] %d ladders added to the nav mesh\n", n);
}

void PF2Hunted_LoadForMap(const char *mapname)
{
	s_rules = PF2HuntedRules();
	LoadLadders(mapname);
	if (!tf_bot_pf2_hunted_rules.GetBool())
		return;
	KeyValues *kv = new KeyValues("PF2HuntedRules");
	if (kv->LoadFromFile(filesystem, "addons/pf2nextbot/data/hunted.cfg", "MOD"))
	{
		if (KeyValues *map = kv->FindKey(mapname))
		{
			s_rules.active = true;
			const char *teams[2] = { "red", "blue" };
			for (int t = 0; t < 2; ++t)
			{
				const char *list = map->GetString(teams[t], NULL);
				if (!list)
					continue;
				unsigned mask = 0;
				for (int c = 0; c < 10; ++c)
					if (V_stristr(list, s_classKeys[c]))
						mask |= 1u << c;
				s_rules.classMask[t == 0 ? TF_TEAM_RED : TF_TEAM_BLUE] = mask;
			}
			s_rules.hasGoal = ParseBox(map->GetString("vip_goal", NULL), &s_rules.goalMins, &s_rules.goalMaxs);
			s_rules.goalWait = map->GetBool("vip_wait", false);
			V_strncpy(s_rules.gateDoor, map->GetString("gate_door", ""), sizeof(s_rules.gateDoor));
			for (int i = 0; i < 16; ++i)
			{
				Vector p;
				const char *s = map->GetString(UTIL_VarArgs("button%d", i), NULL);
				if (s && sscanf(s, "%f %f %f", &p.x, &p.y, &p.z) == 3)
					s_rules.buttons.AddToTail(p);
			}
			Msg("[PF2NB] hunted rules for %s: red=%x blue=%x goal=%d wait=%d gate=%s buttons=%d\n", mapname,
				s_rules.classMask[TF_TEAM_RED], s_rules.classMask[TF_TEAM_BLUE], (int)s_rules.hasGoal, (int)s_rules.goalWait,
				s_rules.gateDoor, s_rules.buttons.Count());
		}
	}
	kv->deleteThis();
}

bool PF2Hunted_ClassAllowed(int team, int tfclass, bool *hasRule)
{
	*hasRule = false;
	if (!s_rules.active || team < 0 || team >= TF_TEAM_COUNT || tfclass < 1 || tfclass > 10)
		return true;
	// the VIP (Civilian) belongs to the escorting team only - also after the map swaps teams
	if (tfclass == TF_CLASS_CIVILIAN && s_rules.hasGoal && team != TF_TEAM_BLUE)
	{
		*hasRule = true;
		return false;
	}
	if (!s_rules.classMask[team])
		return true;
	*hasRule = true;
	return (s_rules.classMask[team] & (1u << (tfclass - 1))) != 0;
}

CTFPlayer *PF2Hunted_FindVIP(int team)
{
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CTFPlayer *p = ToTFPlayer(UTIL_PlayerByIndex(i));
		if (p && p->GetTeamNumber() == team && p->IsPlayerClass(TF_CLASS_CIVILIAN))
			return p;
	}
	return NULL;
}

// The VIP is BLU's Civilian. If BLU has none (and the map's rules allow one), the BLU bot with the
// lowest entity index takes the role when it next chooses a class.
bool PF2Hunted_WantsBotAsVIP(const CTFBot *bot)
{
	if (!s_rules.active || !s_rules.hasGoal || !tf_bot_pf2_hunted_bot_vip.GetBool() || bot->GetTeamNumber() != TF_TEAM_BLUE)
		return false;
	bool rule;
	if (!PF2Hunted_ClassAllowed(TF_TEAM_BLUE, TF_CLASS_CIVILIAN, &rule))
		return false;
	// only one VIP: anyone else already playing Civilian on BLU (human first) keeps the role
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CTFPlayer *p = ToTFPlayer(UTIL_PlayerByIndex(i));
		if (p && p != bot && p->GetTeamNumber() == TF_TEAM_BLUE && p->IsPlayerClass(TF_CLASS_CIVILIAN))
			return false;
	}
	for (int i = 1; i < bot->entindex(); ++i)
	{
		CTFPlayer *p = ToTFPlayer(UTIL_PlayerByIndex(i));
		if (p && p->IsBot() && p->GetTeamNumber() == TF_TEAM_BLUE)
			return false;   // a lower-index BLU bot is the designated VIP
	}
	return true;
}

static CBaseEntity *FindGateDoor()
{
	if (!s_rules.gateDoor[0])
		return NULL;
	return gEntList.FindEntityByName(NULL, s_rules.gateDoor);
}

bool PF2Hunted_GateOpen()
{
	if (!s_rules.gateDoor[0])
		return true;
	bool sawDoor = false;
	for (CBaseEntity *d = gEntList.FindEntityByName(NULL, s_rules.gateDoor); d; d = gEntList.FindEntityByName(d, s_rules.gateDoor))
	{
		sawDoor = true;
		int *toggle = d->Field<int>("m_toggle_state");
		if (toggle && *toggle != TS_AT_BOTTOM)
			return true;   // opening or open
	}
	return !sawDoor;
}

static Vector GoalPoint()
{
	// where a player stands at the centre of the goal volume: the highest floor inside it (Stonehenge's
	// lift volume starts at the ground but the pillar top is 190 units up, so the bottom would be inside
	// the solid pillar and the path would end beside it)
	Vector p((s_rules.goalMins.x + s_rules.goalMaxs.x) * 0.5f, (s_rules.goalMins.y + s_rules.goalMaxs.y) * 0.5f, s_rules.goalMins.z);
	Vector top(p.x, p.y, s_rules.goalMaxs.z - 1.0f);
	float z;
	if (s_rules.goalWait && TheNavMesh && TheNavMesh->GetGroundHeight(top, &z) && z >= s_rules.goalMins.z - 64.0f && z <= s_rules.goalMaxs.z)
		p.z = z;
	return p;
}

Action<CTFBot> *PF2Hunted_ScenarioAction(CTFBot *me)
{
	if (!s_rules.active || !s_rules.hasGoal || me->GetTeamNumber() != TF_TEAM_BLUE)
		return NULL;
	if (me->IsPlayerClass(TF_CLASS_CIVILIAN))
		return new CTFBotPF2HuntedVIP;
	if (me->IsPlayerClass(TF_CLASS_MEDIC))
		return NULL;   // medics keep TFBot's own heal behavior (they follow and heal the VIP's escort)
	return new CTFBotPF2HuntedBodyguard;
}

// ---------------------------------------------------------------------------------------------
// VIP
// ---------------------------------------------------------------------------------------------
ActionResult<CTFBot> CTFBotPF2HuntedVIP::OnStart(CTFBot *me, Action<CTFBot> *priorAction)
{
	m_path.SetMinLookAheadDistance(me->GetDesiredPathLookAheadRange());
	m_repathTimer.Invalidate();
	return Continue();
}

ActionResult<CTFBot> CTFBotPF2HuntedVIP::Update(CTFBot *me, float interval)
{
	if (!me->IsPlayerClass(TF_CLASS_CIVILIAN) || !s_rules.hasGoal)
		return Done("No longer the VIP");

	// only one VIP per map: if someone else (a human, or a lower-index bot) is also the Civilian,
	// step down; the class change applies on the next respawn
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CTFPlayer *p = ToTFPlayer(UTIL_PlayerByIndex(i));
		if (!p || p == me || p->GetTeamNumber() != me->GetTeamNumber() || !p->IsPlayerClass(TF_CLASS_CIVILIAN))
			continue;
		if (!p->IsBot() || p->entindex() < me->entindex())
		{
			me->HandleCommand_JoinClass(me->GetNextSpawnClassname());
			return ChangeTo(new CTFBotPF2HuntedBodyguard, "Another player is the VIP");
		}
	}

	if (!PF2Hunted_GateOpen())
	{
		// wait for the bodyguards to open the way; watch for assassins meanwhile
		me->UpdateLookingAroundForIncomingPlayers(true);
		return Continue();
	}

	Vector goal = GoalPoint();
	bool inside = me->GetAbsOrigin().x >= s_rules.goalMins.x && me->GetAbsOrigin().x <= s_rules.goalMaxs.x &&
		me->GetAbsOrigin().y >= s_rules.goalMins.y && me->GetAbsOrigin().y <= s_rules.goalMaxs.y &&
		me->GetAbsOrigin().z >= s_rules.goalMins.z - 64.0f && me->GetAbsOrigin().z <= s_rules.goalMaxs.z;
	if (inside && s_rules.goalWait)
	{
		// e.g. Stonehenge: stand still on the lift; the map raises and teleports the VIP. Stand at the very
		// centre - a player hanging over the pillar's edge blocks the rising func_door, which then reverses.
		Vector centre((s_rules.goalMins.x + s_rules.goalMaxs.x) * 0.5f, (s_rules.goalMins.y + s_rules.goalMaxs.y) * 0.5f, me->GetAbsOrigin().z);
		if ((centre - me->GetAbsOrigin()).AsVector2D().IsLengthGreaterThan(8.0f))
			me->GetLocomotionInterface()->Approach(centre);
		else
			me->GetLocomotionInterface()->Stop();
		me->UpdateLookingAroundForIncomingPlayers(true);
		return Continue();
	}

	if (m_repathTimer.IsElapsed())
	{
		m_repathTimer.Start(1.0f);
		CTFBotPathCost cost(me, SAFEST_ROUTE);
		m_path.Compute(me, goal, cost);
	}
	m_path.Update(me);
	return Continue();
}

EventDesiredResult<CTFBot> CTFBotPF2HuntedVIP::OnStuck(CTFBot *me)
{
	m_repathTimer.Invalidate();
	return TryContinue();
}

// ---------------------------------------------------------------------------------------------
// Bodyguard
// ---------------------------------------------------------------------------------------------
ActionResult<CTFBot> CTFBotPF2HuntedBodyguard::OnStart(CTFBot *me, Action<CTFBot> *priorAction)
{
	m_path.SetMinLookAheadDistance(me->GetDesiredPathLookAheadRange());
	m_repathTimer.Invalidate();
	m_button = -1;
	m_unreachable = 0;
	return Continue();
}

ActionResult<CTFBot> CTFBotPF2HuntedBodyguard::Update(CTFBot *me, float interval)
{
	if (!PF2Hunted_GateOpen() && s_rules.buttons.Count() > 0)
	{
		// spread the bodyguards over the buttons by entity index
		if (m_button < 0)
			m_button = me->entindex() % s_rules.buttons.Count();
		const Vector &button = s_rules.buttons[m_button];
		if (me->IsRangeLessThan(button, 72.0f))
		{
			// touch-activated buttons fire on contact; for use-buttons look at it and press +use
			me->GetBodyInterface()->AimHeadTowards(button, IBody::CRITICAL, 0.5f, NULL, "Pressing the gate button");
			me->PressUseButton(0.3f);
			m_path.Invalidate();
			// walk into it (touch) and try the next button after a moment
			me->GetLocomotionInterface()->Approach(button);
			if (RandomInt(0, 99) == 0)
				m_button = (m_button + 1) % s_rules.buttons.Count();
			return Continue();
		}
		if (m_repathTimer.IsElapsed() || !m_path.IsValid())
		{
			m_repathTimer.Start(2.0f);
			CTFBotPathCost cost(me, FASTEST_ROUTE);
			// Compute() also succeeds with a partial path to the closest area; a button whose path ends
			// far away (e.g. behind the gate itself) is unreachable: try another
			bool computed = m_path.Compute(me, button, cost);
			// (Compute() returns false when the goal point lies just outside the closest area, e.g. a button on the
			// wall; what matters is whether the path ends next to it)
			bool ok = m_path.IsValid() && (m_path.GetEndPosition() - button).IsLengthLessThan(160.0f);
			if (!ok)
			{
				static ConVarRef dbg("pf2nb_debug");
				if (dbg.IsValid() && dbg.GetBool())
					Msg("[PF2NB] %s: gate button %d (%.0f %.0f %.0f) unreachable: computed=%d valid=%d end=(%.0f %.0f %.0f)\n", me->GetPlayerName(),
						m_button, button.x, button.y, button.z, computed, m_path.IsValid(), m_path.GetEndPosition().x,
						m_path.GetEndPosition().y, m_path.GetEndPosition().z);
				m_path.Invalidate();
				m_button = (m_button + 1) % s_rules.buttons.Count();
				if (++m_unreachable >= s_rules.buttons.Count())
				{
					m_unreachable = 0;
					return SuspendFor(new CTFBotSeekAndDestroy(10.0f), "No reachable gate button; hunting assassins");
				}
				return Continue();
			}
			m_unreachable = 0;
		}
		m_path.Update(me);
		return Continue();
	}

	CTFPlayer *vip = PF2Hunted_FindVIP(TF_TEAM_BLUE);
	if (vip && vip->IsAlive())
		return SuspendFor(new CTFBotEscort(vip), "Escorting the VIP");
	return SuspendFor(new CTFBotSeekAndDestroy(10.0f), "No VIP to escort; hunting assassins");
}

EventDesiredResult<CTFBot> CTFBotPF2HuntedBodyguard::OnStuck(CTFBot *me)
{
	m_repathTimer.Invalidate();
	if (s_rules.buttons.Count())
		m_button = (m_button + 1) % s_rules.buttons.Count();
	return TryContinue();
}

// ---------------------------------------------------------------------------------------------
// Nav tooling: the shipped meshes were generated without ladders, so platforms reachable only by a
// ladder (hunted tower buttons, sniper perches) have no areas. Seed the generator at every ladder top
// that has no area and run an incremental generate (+ full analysis) on a TEST server; the mesh is
// saved to addons/pf2nextbot/nav (CNavMesh::GetFilename, PF2NB-patched). Bots are kicked meanwhile.
// ---------------------------------------------------------------------------------------------
extern ConVar nav_quicksave;
extern Vector NavTraceMins, NavTraceMaxs;

static bool LadderTopSeed(const CNavLadder *ladder, Vector *seed, Vector *normal)
{
	const Vector &top = ladder->m_top;
	for (int side = 0; side < 2; ++side)
	{
		NavDirType dir = side ? OppositeDirection(ladder->GetDir()) : ladder->GetDir();
		Vector center = top;
		AddDirectionVector(&center, dir, HalfHumanWidth);
		for (int d = -1; d < 2 * NUM_DIRECTIONS; ++d)
		{
			Vector p = center;
			if (d >= NUM_DIRECTIONS)
				AddDirectionVector(&p, (NavDirType)(d - NUM_DIRECTIONS), 2.0f * GenerationStepSize);
			else if (d >= 0)
				AddDirectionVector(&p, (NavDirType)d, GenerationStepSize);
			p.z += GenerationStepSize;
			p.x = TheNavMesh->SnapToGrid(p.x);
			p.y = TheNavMesh->SnapToGrid(p.y);
			Vector n;
			if (!TheNavMesh->GetGroundHeight(p, &p.z, &n) || fabs(p.z - top.z) > 48.0f)
				continue;   // the floor found is not the platform at the ladder top
			trace_t tr;
			UTIL_TraceHull(center + Vector(0, 0, 4), p + Vector(0, 0, 4), NavTraceMins, NavTraceMaxs,
				TheNavMesh->GetGenerationTraceMask(), NULL, COLLISION_GROUP_NONE, &tr);
			if (tr.fraction != 1.0f || tr.startsolid)
				continue;
			if (CNavArea *a = TheNavMesh->GetNavArea(p + Vector(0, 0, 8), 48.0f))
				return false;   // this ladder top is already covered
			*seed = p;
			*normal = n;
			return true;
		}
	}
	return false;
}

CON_COMMAND(pf2nb_nav_seed_ladders, "pf2nb_nav_seed_ladders [go]: list ladder tops without nav areas; 'go' = incremental generate from them (TEST servers only; kicks bots, saves the .nav)")
{
	if (!TheNavMesh || !TheNavMesh->IsLoaded())
	{
		Msg("[PF2NB] no nav mesh loaded\n");
		return;
	}
	PF2_InvalidateEntityCache();
	bool go = args.ArgC() > 1 && !V_stricmp(args[1], "go");
	int seeds = 0;
	FOR_EACH_VEC(TheNavMesh->GetLadders(), i)
	{
		Vector p, n;
		if (!LadderTopSeed(TheNavMesh->GetLadders()[i], &p, &n))
			continue;
		Msg("[PF2NB] ladder %d top (%.0f %.0f %.0f): seed (%.0f %.0f %.0f)\n", i, TheNavMesh->GetLadders()[i]->m_top.x,
			TheNavMesh->GetLadders()[i]->m_top.y, TheNavMesh->GetLadders()[i]->m_top.z, p.x, p.y, p.z);
		if (go)
		{
			if (!seeds)
				TheNavMesh->ClearWalkableSeeds();
			TheNavMesh->AddWalkableSeed(p, n);
		}
		++seeds;
	}
	int ladderSeeds = seeds;

	// player spawns with no nav nearby: e.g. spawn rooms behind doors that were shut when TF2's generator
	// sampled the map (plr_chariots: bots spawned off the mesh and never moved)
	CUtlVector<Vector> spawnSeeds;
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "info_player_teamspawn"); e; e = gEntList.FindEntityByClassname(e, "info_player_teamspawn"))
	{
		Vector pos = e->GetAbsOrigin();
		pos.z += HalfHumanHeight;
		Vector n;
		if (!TheNavMesh->GetGroundHeight(pos, &pos.z, &n))
			continue;
		if (TheNavMesh->GetNavArea(pos + Vector(0, 0, 8), 64.0f) || TheNavMesh->GetNearestNavArea(pos, false, 96.0f, false, false))
			continue;   // covered
		bool dup = false;
		FOR_EACH_VEC(spawnSeeds, s)
			if ((spawnSeeds[s] - pos).IsLengthLessThan(200.0f))
				dup = true;
		if (dup)
			continue;
		spawnSeeds.AddToTail(pos);
		pos.x = TheNavMesh->SnapToGrid(pos.x);
		pos.y = TheNavMesh->SnapToGrid(pos.y);
		Msg("[PF2NB] spawn (%.0f %.0f %.0f) has no nav: seed\n", pos.x, pos.y, pos.z);
		if (go)
		{
			if (!seeds)
				TheNavMesh->ClearWalkableSeeds();
			TheNavMesh->AddWalkableSeed(pos, n);
		}
		++seeds;
	}
	Msg("[PF2NB] %d uncovered ladder tops, %d uncovered spawns\n", ladderSeeds, spawnSeeds.Count());
	Msg("[PF2NB] %d seeds\n", seeds);
	if (go && seeds)
	{
		TheNavMesh->BeginGeneration(true);
		nav_quicksave.SetValue(0);   // BeginGeneration(incremental) forces quicksave; we want hiding spots etc. back
		Msg("[PF2NB] incremental generation started; the mesh is saved to addons/pf2nextbot/nav when done\n");
	}
}

// Full generation seeded from EVERY team spawn (stock seeds from the first spawn point only, which leaves
// maps whose first spawn sits in a closed box with an empty mesh). TEST servers only: sets the bot quota
// to 0 and kicks the bots, generates and saves to addons/pf2nextbot/nav; the map is NOT reloaded (do it after a humans check).
CON_COMMAND(pf2nb_nav_generate, "Generate a nav mesh seeded from all team spawns (TEST servers only; kicks bots, saves to addons/pf2nextbot/nav; reload the map yourself after a humans check)")
{
	if (!TheNavMesh)
		return;
	PF2_InvalidateEntityCache();
	engine->ServerCommand("tf_bot_quota 0\ntf_bot_kick all\n");
	engine->ServerExecute();
	TheNavMesh->ClearWalkableSeeds();
	int seeds = 0;
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "info_player_*"); e; e = gEntList.FindEntityByClassname(e, "info_player_*"))
	{
		Vector pos = e->GetAbsOrigin();
		pos.x = TheNavMesh->SnapToGrid(pos.x);
		pos.y = TheNavMesh->SnapToGrid(pos.y);
		Vector normal;
		pos.z += HalfHumanHeight;   // spawn origins sit on (or just in) the floor
		if (TheNavMesh->GetGroundHeight(pos, &pos.z, &normal))
		{
			TheNavMesh->AddWalkableSeed(pos, normal);
			++seeds;
		}
	}
	Msg("[PF2NB] %d walkable seeds from player spawns; generating\n", seeds);
	if (seeds)
		TheNavMesh->BeginGeneration(false);
}
