/**
 * PF2 spawn fix: keeps players (in practice: Civilians) out of the enemy team's spawn.
 *
 * Root cause (shipped server binary, CTFPlayer::SelectSpawnSpot / EntSelectSpawnPoint):
 *   SelectSpawnSpot walks every info_player_teamspawn, starting after the last spot the player's
 *   team used, and accepts the first spot that is on the player's team, enabled, clear and whose
 *   spawnflags class mask allows the class (spawnflags == 0 or bit class-1). When nothing matches it
 *   returns false but leaves the cursor on the spot it started from, and EntSelectSpawnPoint spawns
 *   the player THERE anyway - without checking that spot's team, enabled state or class mask.
 *   Many maps tick "all classes" as spawnflags 511 (classes 1-9), which leaves out PF2's Civilian
 *   (class 10, bit 512), so on those maps every Civilian spawn takes that fallback. Usually the
 *   start spot is the own team's next spot, but when the team's last used spot is the last one of
 *   its block in entity order (or no teammate has spawned yet this map), the start spot belongs to
 *   the other team and the Civilian appears in the enemy spawn room.
 *
 * What this plugin does:
 *   1. sm_spawnfix_civmask: on maps where a team has NO spawn the Civilian may use, it adds the
 *      Civilian bit to that team's spawns that already allow all nine normal classes. Civilians
 *      then pass the engine's own selection like everyone else (no teleport needed). Maps that
 *      give the Civilian its own spawns (hunted/VIP maps, plr_highertower...) are left alone.
 *   2. sm_spawnfix_enabled: safety net for whatever still falls back (all own spawns occupied,
 *      the hunted VIP spawn blocked, ...). One frame after player_spawn, if the player stands on a
 *      teamspawn of ANOTHER team (the only way the engine puts anyone there), no enabled own-team
 *      spawn is nearby and the player is not inside an own-team func_respawnroom, the player is
 *      moved to an enabled own-team spawn that allows their class (never a hunted "cage" spawn).
 *      Every correction is logged.
 */
#include <sourcemod>
#include <sdktools>
#include <sdkhooks>

#pragma newdecls required
#pragma semicolon 1

#define PL_VERSION "1.0.0"

#define TEAM_RED 2
#define TEAM_BLU 3
#define CLASS_CIVILIAN 10
#define NORMAL_CLASSES 0x1FF                       // classes 1-9
#define CIV_BIT (1 << (CLASS_CIVILIAN - 1))        // 512

#define LAND_XY_TOL   24.0     // the engine puts the player exactly on the spot's x/y; allow one tick of movement
#define LAND_Z_ABOVE  16.0     // player origin may be this far above the spot ...
#define LAND_Z_BELOW  1024.0   // ... or this far below it (DropToGround)
#define CAGE_PAD      16.0

#if defined SPAWNFIX_LOGONLY
	#define DEF_ENABLED "2"
	#define DEF_CIVMASK "0"
#else
	#define DEF_ENABLED "1"
	#define DEF_CIVMASK "1"
#endif

public Plugin myinfo =
{
	name        = "PF2 Spawn Fix",
	author      = "pf2-nextbot",
	description = "Stops PF2's spawn fallback from putting players (Civilians) in the enemy spawn",
	version     = PL_VERSION,
	url         = ""
};

static const char g_sTeamName[4][] = { "neutral", "spectator", "RED", "BLU" };
static const char g_sClassName[11][] = { "Unknown", "Scout", "Sniper", "Soldier", "Demoman", "Medic",
	"Heavy", "Pyro", "Spy", "Engineer", "Civilian" };

ConVar g_cvEnabled;
ConVar g_cvCivMask;
ConVar g_cvOwnRadius;

bool      g_bCivPatchTeam[4];   // this map: team has no Civilian-capable spawn -> patch its all-class spawns
int       g_iPatched;           // spawns patched this map (counting re-created entities again)
ArrayList g_hCages;             // damaging trigger_hurt boxes (hunted "torment room" spawns)
int       g_iFixed, g_iWouldFix, g_iSkipped, g_iNoTarget;

public void OnPluginStart()
{
	CreateConVar("sm_spawnfix_version", PL_VERSION, "PF2 Spawn Fix version", FCVAR_NOTIFY | FCVAR_DONTRECORD);
	g_cvEnabled = CreateConVar("sm_spawnfix_enabled", DEF_ENABLED,
		"Wrong-team spawn safety net: 0 = off, 1 = move the player to an own-team spawn, 2 = log only.",
		_, true, 0.0, true, 2.0);
	g_cvCivMask = CreateConVar("sm_spawnfix_civmask", DEF_CIVMASK,
		"On maps where a team has no spawn that allows the Civilian, let the Civilian use that team's all-class spawns.",
		_, true, 0.0, true, 1.0);
	g_cvOwnRadius = CreateConVar("sm_spawnfix_ownradius", "384",
		"Do not act if an enabled own-team spawn is within this many units of the player (mixed/shared spawn areas).",
		_, true, 0.0);
	g_cvCivMask.AddChangeHook(OnCivMaskChanged);

	HookEvent("player_spawn", Event_PlayerSpawn);
	HookEvent("teamplay_round_start", Event_RoundStart, EventHookMode_PostNoCopy);

	RegAdminCmd("sm_spawnfix_status", Cmd_Status, ADMFLAG_GENERIC, "Show the spawn layout and corrections for this map");

	g_hCages = new ArrayList(6);
	Analyze("plugin start");
}

public void OnMapStart()
{
	g_iPatched = g_iFixed = g_iWouldFix = g_iSkipped = g_iNoTarget = 0;
	Analyze("map start");
}

public void OnMapEnd()
{
	// spots of the next map are created before OnMapStart; never patch them with this map's decision
	g_bCivPatchTeam[TEAM_RED] = g_bCivPatchTeam[TEAM_BLU] = false;
}

public void Event_RoundStart(Event event, const char[] name, bool dontBroadcast)
{
	Analyze("round start");
}

public void OnCivMaskChanged(ConVar cv, const char[] oldv, const char[] newv)
{
	if (cv.BoolValue)
		Analyze("sm_spawnfix_civmask 1");
	// turning it off does not un-patch; the next map starts clean
}

/* ---------------------------------------------------------------- map analysis / civ mask patch */

bool IsHuntedMap()
{
	char map[PLATFORM_MAX_PATH];
	GetCurrentMap(map, sizeof(map));
	GetMapDisplayName(map, map, sizeof(map));
	return StrContains(map, "hunted", false) != -1 || strncmp(map, "vip_", 4, false) == 0;
}

bool CivCanUse(int flags)
{
	return flags == 0 || (flags & CIV_BIT) != 0;   // engine: spawnflags == 0 or bit (class-1) set
}

void Analyze(const char[] why)
{
	// cage boxes, same rule as pf2_hunted_classrestrict
	g_hCages.Clear();
	int ent = -1;
	while ((ent = FindEntityByClassname(ent, "trigger_hurt")) != -1)
	{
		if (HasEntProp(ent, Prop_Data, "m_flDamage") && GetEntPropFloat(ent, Prop_Data, "m_flDamage") <= 0.0)
			continue;
		if (HasEntProp(ent, Prop_Data, "m_bDisabled") && GetEntProp(ent, Prop_Data, "m_bDisabled"))
			continue;
		float o[3], mn[3], mx[3], box[6];
		GetEntPropVector(ent, Prop_Data, "m_vecAbsOrigin", o);
		GetEntPropVector(ent, Prop_Data, "m_vecMins", mn);
		GetEntPropVector(ent, Prop_Data, "m_vecMaxs", mx);
		for (int i = 0; i < 3; i++)
		{
			box[i] = o[i] + mn[i];
			box[i + 3] = o[i] + mx[i];
		}
		g_hCages.PushArray(box);
	}

	int civOk[4], total[4];
	ent = -1;
	while ((ent = FindEntityByClassname(ent, "info_player_teamspawn")) != -1)
	{
		int team = GetEntProp(ent, Prop_Data, "m_iTeamNum");
		if (team != TEAM_RED && team != TEAM_BLU)
			continue;
		total[team]++;
		if (CivCanUse(GetEntProp(ent, Prop_Data, "m_spawnflags")))
			civOk[team]++;
	}
	// the decision sticks for the whole map (after patching, civOk is no longer 0); OnMapEnd resets it
	bool hunted = IsHuntedMap();
	for (int t = TEAM_RED; t <= TEAM_BLU; t++)
		if (total[t] > 0 && civOk[t] == 0 && !hunted)
			g_bCivPatchTeam[t] = true;

	if (!g_cvCivMask.BoolValue)
	{
		if (g_bCivPatchTeam[TEAM_RED] || g_bCivPatchTeam[TEAM_BLU])
			LogMessage("%s: RED %d/%d, BLU %d/%d spawns usable by the Civilian (civmask patch is off)",
				why, civOk[TEAM_RED], total[TEAM_RED], civOk[TEAM_BLU], total[TEAM_BLU]);
		return;
	}

	int n = 0;
	ent = -1;
	while ((ent = FindEntityByClassname(ent, "info_player_teamspawn")) != -1)
		if (PatchSpot(ent))
			n++;
	if (n > 0)
		LogMessage("%s: Civilian had no usable spawn (RED %d/%d, BLU %d/%d); added the Civilian to %d all-class spawns",
			why, civOk[TEAM_RED], total[TEAM_RED], civOk[TEAM_BLU], total[TEAM_BLU], n);
}

bool PatchSpot(int ent)
{
	if (!g_cvCivMask.BoolValue)
		return false;
	int team = GetEntProp(ent, Prop_Data, "m_iTeamNum");
	if ((team != TEAM_RED && team != TEAM_BLU) || !g_bCivPatchTeam[team])
		return false;
	int flags = GetEntProp(ent, Prop_Data, "m_spawnflags");
	if ((flags & NORMAL_CLASSES) != NORMAL_CLASSES || (flags & CIV_BIT))
		return false;   // only spots that already allow every normal class
	SetEntProp(ent, Prop_Data, "m_spawnflags", flags | CIV_BIT);
	g_iPatched++;
	return true;
}

// spots re-created by a round restart are patched as they spawn, before players respawn on them
public void OnEntityCreated(int entity, const char[] classname)
{
	if (StrEqual(classname, "info_player_teamspawn"))
		SDKHook(entity, SDKHook_SpawnPost, OnTeamSpawnSpawned);
}

public void OnTeamSpawnSpawned(int entity)
{
	if (IsValidEntity(entity))
		PatchSpot(entity);
}

/* ---------------------------------------------------------------- safety net */

public void Event_PlayerSpawn(Event event, const char[] name, bool dontBroadcast)
{
	if (g_cvEnabled.IntValue)
		RequestFrame(Frame_CheckSpawn, event.GetInt("userid"));
}

public void Frame_CheckSpawn(int userid)
{
	int mode = g_cvEnabled.IntValue;
	int client = GetClientOfUserId(userid);
	if (!mode || !client || !IsClientInGame(client) || !IsPlayerAlive(client))
		return;
	int team = GetClientTeam(client);
	if (team != TEAM_RED && team != TEAM_BLU)
		return;

	float pos[3];
	GetClientAbsOrigin(client, pos);

	// which teamspawn did the engine put this player on?
	int landed = -1, ownHere = -1;
	float best = LAND_XY_TOL + 1.0;
	int ent = -1;
	while ((ent = FindEntityByClassname(ent, "info_player_teamspawn")) != -1)
	{
		float sp[3];
		GetEntPropVector(ent, Prop_Data, "m_vecAbsOrigin", sp);
		float dz = sp[2] - pos[2];
		if (dz < -LAND_Z_ABOVE || dz > LAND_Z_BELOW)
			continue;
		float dxy = SquareRoot((sp[0] - pos[0]) * (sp[0] - pos[0]) + (sp[1] - pos[1]) * (sp[1] - pos[1]));
		if (dxy > LAND_XY_TOL)
			continue;
		if (GetEntProp(ent, Prop_Data, "m_iTeamNum") == team)
			ownHere = ent;
		if (dxy < best)
		{
			best = dxy;
			landed = ent;
		}
	}
	if (landed == -1 || ownHere != -1)
		return;   // not on a spot, or on/overlapping an own-team spot: nothing to do
	int spotTeam = GetEntProp(landed, Prop_Data, "m_iTeamNum");
	if (spotTeam == team)
		return;

	int cls = GetEntProp(client, Prop_Send, "m_iClass");
	if (cls < 0 || cls > 10)
		cls = 0;
	float lp[3];
	GetEntPropVector(landed, Prop_Data, "m_vecAbsOrigin", lp);

	// conservative: leave mixed/shared spawn areas (DM maps etc.) and own respawn rooms alone
	float radius = g_cvOwnRadius.FloatValue;
	int near = NearestEnabledOwnSpawn(team, pos, radius);
	if (near != -1 || InOwnRespawnRoom(team, pos))
	{
		g_iSkipped++;
		LogMessage("%L (%s, %s) spawned on %s teamspawn #%d (%.0f %.0f %.0f), but an own-team spawn/respawn room is close; left alone",
			client, g_sTeamName[team], g_sClassName[cls], g_sTeamName[spotTeam & 3], landed, lp[0], lp[1], lp[2]);
		return;
	}

	if (mode == 2)
	{
		g_iWouldFix++;
		LogMessage("[log only] %L (%s, %s) spawned in the wrong spawn: %s teamspawn #%d (%.0f %.0f %.0f)",
			client, g_sTeamName[team], g_sClassName[cls], g_sTeamName[spotTeam & 3], landed, lp[0], lp[1], lp[2]);
		return;
	}

	float dest[3], ang[3];
	int tier;
	int target = PickOwnSpawn(client, team, cls, dest, ang, tier);
	if (target == -1)
	{
		g_iNoTarget++;
		LogMessage("%L (%s, %s) spawned in the wrong spawn: %s teamspawn #%d (%.0f %.0f %.0f); no usable own-team spawn, left in place",
			client, g_sTeamName[team], g_sClassName[cls], g_sTeamName[spotTeam & 3], landed, lp[0], lp[1], lp[2]);
		return;
	}

	float zero[3];
	TeleportEntity(client, dest, ang, zero);
	g_iFixed++;
	LogMessage("%L (%s, %s) spawned in the wrong spawn: %s teamspawn #%d (%.0f %.0f %.0f); moved to %s teamspawn #%d (%.0f %.0f %.0f)%s",
		client, g_sTeamName[team], g_sClassName[cls], g_sTeamName[spotTeam & 3], landed, lp[0], lp[1], lp[2],
		g_sTeamName[team], target, dest[0], dest[1], dest[2], tier >= 3 ? " [no spawn allows this class; class mask ignored]" : "");
}

int NearestEnabledOwnSpawn(int team, const float pos[3], float radius)
{
	if (radius <= 0.0)
		return -1;
	int ent = -1;
	while ((ent = FindEntityByClassname(ent, "info_player_teamspawn")) != -1)
	{
		if (GetEntProp(ent, Prop_Data, "m_iTeamNum") != team || SpotDisabled(ent))
			continue;
		float sp[3];
		GetEntPropVector(ent, Prop_Data, "m_vecAbsOrigin", sp);
		if (GetVectorDistance(sp, pos) <= radius)
			return ent;
	}
	return -1;
}

bool InOwnRespawnRoom(int team, const float pos[3])
{
	int ent = -1;
	while ((ent = FindEntityByClassname(ent, "func_respawnroom")) != -1)
	{
		if (GetEntProp(ent, Prop_Data, "m_iTeamNum") != team)
			continue;
		float o[3], mn[3], mx[3];
		GetEntPropVector(ent, Prop_Data, "m_vecAbsOrigin", o);
		GetEntPropVector(ent, Prop_Data, "m_vecMins", mn);
		GetEntPropVector(ent, Prop_Data, "m_vecMaxs", mx);
		if (pos[0] >= o[0] + mn[0] && pos[0] <= o[0] + mx[0]
		 && pos[1] >= o[1] + mn[1] && pos[1] <= o[1] + mx[1]
		 && pos[2] >= o[2] + mn[2] - 8.0 && pos[2] <= o[2] + mx[2])
			return true;
	}
	return false;
}

bool SpotDisabled(int ent)
{
	return HasEntProp(ent, Prop_Data, "m_bDisabled") && GetEntProp(ent, Prop_Data, "m_bDisabled") != 0;
}

bool InCage(const float p[3])
{
	float b[6];
	for (int i = 0; i < g_hCages.Length; i++)
	{
		g_hCages.GetArray(i, b);
		if (p[0] >= b[0] - CAGE_PAD && p[0] <= b[3] + CAGE_PAD
		 && p[1] >= b[1] - CAGE_PAD && p[1] <= b[4] + CAGE_PAD
		 && p[2] >= b[2] - CAGE_PAD && p[2] <= b[5] + CAGE_PAD)
			return true;
	}
	return false;
}

/**
 * Own-team, enabled spawn, best tier first, random within a tier:
 *   1 class allowed, hull clear of world and enemies   2 class allowed, clear of world only
 *   3 any class (not a cage), clear                     4 any class (not a cage), clear of world only
 */
int PickOwnSpawn(int client, int team, int cls, float dest[3], float ang[3], int &tier)
{
	ArrayList cand[5];
	for (int t = 1; t <= 4; t++)
		cand[t] = new ArrayList();

	float mins[3], maxs[3];
	GetClientMins(client, mins);
	GetClientMaxs(client, maxs);

	int ent = -1;
	while ((ent = FindEntityByClassname(ent, "info_player_teamspawn")) != -1)
	{
		if (GetEntProp(ent, Prop_Data, "m_iTeamNum") != team || SpotDisabled(ent))
			continue;
		float sp[3];
		GetEntPropVector(ent, Prop_Data, "m_vecAbsOrigin", sp);
		if (sp[0] == 0.0 && sp[1] == 0.0 && sp[2] == 0.0)
			continue;   // the engine skips these too
		int flags = GetEntProp(ent, Prop_Data, "m_spawnflags");
		bool classOk = flags == 0 || (cls >= 1 && (flags & (1 << (cls - 1))) != 0);
		if (!classOk && InCage(sp))
			continue;
		bool worldClear = HullClear(client, sp, mins, maxs, MASK_PLAYERSOLID_BRUSHONLY);
		if (!worldClear)
			continue;
		bool clear = HullClear(client, sp, mins, maxs, MASK_PLAYERSOLID);
		int t = classOk ? (clear ? 1 : 2) : (clear ? 3 : 4);
		cand[t].Push(ent);
	}

	int pick = -1;
	tier = 0;
	for (int t = 1; t <= 4 && pick == -1; t++)
	{
		if (cand[t].Length > 0)
		{
			pick = cand[t].Get(GetRandomInt(0, cand[t].Length - 1));
			tier = t;
		}
	}
	for (int t = 1; t <= 4; t++)
		delete cand[t];
	if (pick == -1)
		return -1;

	float sp[3], down[3];
	GetEntPropVector(pick, Prop_Data, "m_vecAbsOrigin", sp);
	GetEntPropVector(pick, Prop_Data, "m_angRotation", ang);
	// like the engine: drop to the ground, then 1 unit up
	down[0] = sp[0];
	down[1] = sp[1];
	down[2] = sp[2] - 256.0;
	TR_TraceHullFilter(sp, down, mins, maxs, MASK_PLAYERSOLID_BRUSHONLY, TraceFilter_Passable, client);
	if (TR_DidHit() && !TR_StartSolid())
	{
		TR_GetEndPosition(dest);
		dest[2] += 1.0;
	}
	else
	{
		dest[0] = sp[0];
		dest[1] = sp[1];
		dest[2] = sp[2];
	}
	return pick;
}

bool HullClear(int client, const float p[3], const float mins[3], const float maxs[3], int mask)
{
	TR_TraceHullFilter(p, p, mins, maxs, mask, TraceFilter_Passable, client);
	return !TR_StartSolid() && !TR_AllSolid() && TR_GetFraction() >= 1.0;
}

// ignore the player and their teammates (team members do not collide in PF2)
public bool TraceFilter_Passable(int entity, int contentsMask, any data)
{
	int self = view_as<int>(data);
	if (entity == self)
		return false;
	if (entity >= 1 && entity <= MaxClients && IsClientInGame(entity) && GetClientTeam(entity) == GetClientTeam(self))
		return false;
	return true;
}


/* ---------------------------------------------------------------- status */

public Action Cmd_Status(int client, int args)
{
	int total[4], enabled[4], civOk[4];
	int ent = -1;
	while ((ent = FindEntityByClassname(ent, "info_player_teamspawn")) != -1)
	{
		int team = GetEntProp(ent, Prop_Data, "m_iTeamNum");
		if (team < 0 || team > 3)
			continue;
		total[team]++;
		if (!SpotDisabled(ent))
			enabled[team]++;
		if (CivCanUse(GetEntProp(ent, Prop_Data, "m_spawnflags")))
			civOk[team]++;
	}
	char map[PLATFORM_MAX_PATH];
	GetCurrentMap(map, sizeof(map));
	ReplyToCommand(client, "[spawnfix] %s: enabled=%d civmask=%d hunted=%d cages=%d", map,
		g_cvEnabled.IntValue, g_cvCivMask.IntValue, IsHuntedMap(), g_hCages.Length);
	ReplyToCommand(client, "[spawnfix] RED spawns %d (enabled %d, Civilian-usable %d, patch %d) | BLU spawns %d (enabled %d, Civilian-usable %d, patch %d) | neutral %d",
		total[TEAM_RED], enabled[TEAM_RED], civOk[TEAM_RED], g_bCivPatchTeam[TEAM_RED],
		total[TEAM_BLU], enabled[TEAM_BLU], civOk[TEAM_BLU], g_bCivPatchTeam[TEAM_BLU], total[0]);
	ReplyToCommand(client, "[spawnfix] this map: patched %d, moved %d, would move %d (log only), skipped %d, no target %d",
		g_iPatched, g_iFixed, g_iWouldFix, g_iSkipped, g_iNoTarget);
	return Plugin_Handled;
}
