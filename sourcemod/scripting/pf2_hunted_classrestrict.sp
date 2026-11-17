/**
 * PF2 Hunted class restrictions
 *
 * On PF2 "hunted"/VIP maps, info_player_teamspawn spawnflags carry a class bitmask (bit = class-1,
 * 0 = any class) and the spawns meant for the wrong classes sit inside a damaging trigger_hurt (the
 * "torment room" cage). This plugin derives the allowed classes per team from that evidence at map
 * start (same logic as pf2-nextbot's tools/hunted_rules.py) and enforces them for everyone:
 *   - joinclass from humans is refused for a disallowed class (chat message lists the allowed ones),
 *   - anyone who spawns as a disallowed class is switched to an allowed one and respawned at once.
 * Maps without caged spawns are left untouched. On restricted maps the Civilian (VIP) is never
 * allowed on RED, and BLU may have only one human Civilian (pf2-nextbot bots step down by themselves).
 *
 * Overrides: addons/sourcemod/configs/hunted_classrestrict.cfg (per-map "red"/"blue" class lists,
 * or "enabled" "0").
 */
#include <sourcemod>
#include <sdktools>

#pragma newdecls required
#pragma semicolon 1

#define PL_VERSION "1.0.0"

#define TEAM_RED 2
#define TEAM_BLU 3
#define CLASS_CIVILIAN 10
#define ALL_CLASSES 0x3FF
#define CAGE_PAD 16.0

public Plugin myinfo =
{
	name        = "PF2 Hunted Class Restrictions",
	author      = "pf2-nextbot",
	description = "Enforces the class rules of PF2 hunted/VIP maps (derived from caged spawns)",
	version     = PL_VERSION,
	url         = ""
};

static const char g_sClassCmd[11][] = { "", "scout", "sniper", "soldier", "demoman", "medic",
	"heavyweapons", "pyro", "spy", "engineer", "civilian" };
static const char g_sClassName[11][] = { "", "Scout", "Sniper", "Soldier", "Demoman", "Medic",
	"Heavy", "Pyro", "Spy", "Engineer", "Civilian" };

ConVar g_cvEnabled;
Handle g_hForceRespawn;

bool g_bActive;            // this map has rules
int  g_iAllowed[4];        // class bitmask per team (index 2/3)
int  g_iCage[4];           // caged spawns per team
int  g_iFree[4];           // non-caged spawns per team
int  g_iSpawns, g_iHurts;
char g_sSource[32];        // "derived", "override", "override-disabled", "none"

float g_fForceWindow[MAXPLAYERS + 1];
int   g_iForceCount[MAXPLAYERS + 1];

public void OnPluginStart()
{
	CreateConVar("sm_huntedclass_version", PL_VERSION, "PF2 Hunted Class Restrictions version", FCVAR_NOTIFY | FCVAR_DONTRECORD);
	g_cvEnabled = CreateConVar("sm_huntedclass_enabled", "1", "Enforce class rules on PF2 hunted/VIP maps (derived from caged spawns).", _, true, 0.0, true, 1.0);

	GameData conf = new GameData("pf2");
	if (conf != null)
	{
		StartPrepSDKCall(SDKCall_Player);
		if (PrepSDKCall_SetFromConf(conf, SDKConf_Virtual, "ForceRespawn"))
			g_hForceRespawn = EndPrepSDKCall();
		delete conf;
	}
	if (g_hForceRespawn == null)
		LogMessage("ForceRespawn unavailable (gamedata pf2.txt); falling back to suicide + respawn as the allowed class");

	AddCommandListener(Cmd_JoinClass, "joinclass");
	AddCommandListener(Cmd_JoinClass, "join_class");
	HookEvent("player_spawn", Event_PlayerSpawn);

	RegAdminCmd("sm_huntedclass_status", Cmd_Status, ADMFLAG_GENERIC, "Show the class rules derived for this map");
	RegAdminCmd("sm_huntedclass_rederive", Cmd_Rederive, ADMFLAG_CONFIG, "Re-derive the class rules for this map");
	RegAdminCmd("sm_huntedclass_test", Cmd_Test, ADMFLAG_CHEATS, "sm_huntedclass_test <#userid|name> <class>: report what joinclass would do, then respawn the player as that class (the spawn check should switch a disallowed class back)");
}

public void OnMapStart()
{
	Derive();
	char line[256];
	Describe(line, sizeof(line));
	LogMessage("%s", line);
}

public void OnConfigsExecuted()
{
	// classrestrict.smx is configured per map by map cfgs; warn when its limits would contradict ours
	CreateTimer(1.0, Timer_CheckClassRestrict, _, TIMER_FLAG_NO_MAPCHANGE);
}

public void OnClientPutInServer(int client)
{
	g_iForceCount[client] = 0;
	g_fForceWindow[client] = 0.0;
}

/* ---------------------------------------------------------------- rule derivation */

void Derive()
{
	g_bActive = false;
	g_iSpawns = g_iHurts = 0;
	for (int t = 0; t < 4; t++)
	{
		g_iAllowed[t] = ALL_CLASSES;
		g_iCage[t] = g_iFree[t] = 0;
	}
	strcopy(g_sSource, sizeof(g_sSource), "none");

	char map[PLATFORM_MAX_PATH];
	GetCurrentMap(map, sizeof(map));
	GetMapDisplayName(map, map, sizeof(map));

	// damaging, enabled trigger_hurt brushes in world space
	ArrayList hurts = new ArrayList(6);
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
		hurts.PushArray(box);
	}
	g_iHurts = hurts.Length;

	int found[4];
	ent = -1;
	while ((ent = FindEntityByClassname(ent, "info_player_teamspawn")) != -1)
	{
		if (HasEntProp(ent, Prop_Data, "m_bDisabled") && GetEntProp(ent, Prop_Data, "m_bDisabled"))
			continue;
		int team = GetEntProp(ent, Prop_Data, "m_iTeamNum");
		if (team != TEAM_RED && team != TEAM_BLU)
			continue;
		g_iSpawns++;
		float p[3];
		GetEntPropVector(ent, Prop_Data, "m_vecAbsOrigin", p);
		if (InAnyBox(hurts, p))
		{
			g_iCage[team]++;
			continue;
		}
		g_iFree[team]++;
		int mask = GetEntProp(ent, Prop_Data, "m_spawnflags") & ALL_CLASSES;
		found[team] |= (mask == 0) ? ALL_CLASSES : mask;
	}
	delete hurts;

	if (g_iCage[TEAM_RED] + g_iCage[TEAM_BLU] > 0)
	{
		g_bActive = true;
		strcopy(g_sSource, sizeof(g_sSource), "derived");
		for (int t = TEAM_RED; t <= TEAM_BLU; t++)
			g_iAllowed[t] = found[t] ? found[t] : ALL_CLASSES;   // no free spawns at all: do not lock the team out
	}

	ApplyOverride(map);

	if (g_bActive)
		g_iAllowed[TEAM_RED] &= ~(1 << (CLASS_CIVILIAN - 1));   // the VIP is BLU only
}

bool InAnyBox(ArrayList boxes, const float p[3])
{
	float b[6];
	for (int i = 0; i < boxes.Length; i++)
	{
		boxes.GetArray(i, b);
		if (p[0] >= b[0] - CAGE_PAD && p[0] <= b[3] + CAGE_PAD
		 && p[1] >= b[1] - CAGE_PAD && p[1] <= b[4] + CAGE_PAD
		 && p[2] >= b[2] - CAGE_PAD && p[2] <= b[5] + CAGE_PAD)
			return true;
	}
	return false;
}

void ApplyOverride(const char[] map)
{
	char path[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, path, sizeof(path), "configs/hunted_classrestrict.cfg");
	if (!FileExists(path))
		return;
	KeyValues kv = new KeyValues("HuntedClassRestrict");
	if (!kv.ImportFromFile(path))
	{
		LogError("could not parse %s", path);
		delete kv;
		return;
	}
	if (kv.JumpToKey(map))
	{
		if (!kv.GetNum("enabled", 1))
		{
			g_bActive = false;
			g_iAllowed[TEAM_RED] = g_iAllowed[TEAM_BLU] = ALL_CLASSES;
			strcopy(g_sSource, sizeof(g_sSource), "override-disabled");
		}
		else
		{
			char buf[256];
			static const char keys[2][] = { "red", "blue" };
			for (int k = 0; k < 2; k++)
			{
				kv.GetString(keys[k], buf, sizeof(buf));
				if (buf[0])
				{
					int mask = ParseClassList(buf);
					if (mask)
					{
						g_iAllowed[TEAM_RED + k] = mask;
						g_bActive = true;
						strcopy(g_sSource, sizeof(g_sSource), "override");
					}
					else
						LogError("override for %s: no valid class in \"%s\" = \"%s\"", map, keys[k], buf);
				}
			}
		}
	}
	delete kv;
}

int ParseClassList(const char[] list)
{
	char parts[12][16];
	int n = ExplodeString(list, " ", parts, sizeof(parts), sizeof(parts[]));
	int mask = 0;
	for (int i = 0; i < n; i++)
	{
		TrimString(parts[i]);
		if (StrEqual(parts[i], "all", false))
			mask |= ALL_CLASSES;
		int c = ClassFromName(parts[i]);
		if (c)
			mask |= 1 << (c - 1);
	}
	return mask;
}

int ClassFromName(const char[] name)
{
	for (int c = 1; c <= 10; c++)
		if (StrEqual(name, g_sClassCmd[c], false) || StrEqual(name, g_sClassName[c], false))
			return c;
	if (StrEqual(name, "heavy", false))
		return 6;
	if (StrEqual(name, "demo", false))
		return 4;
	if (StrEqual(name, "engie", false))
		return 9;
	if (StrEqual(name, "civ", false) || StrEqual(name, "vip", false))
		return CLASS_CIVILIAN;
	return 0;
}

void MaskToString(int mask, char[] buf, int len, bool display)
{
	buf[0] = '\0';
	for (int c = 1; c <= 10; c++)
	{
		if (mask & (1 << (c - 1)))
		{
			if (buf[0])
				StrCat(buf, len, display ? ", " : " ");
			StrCat(buf, len, display ? g_sClassName[c] : g_sClassCmd[c]);
		}
	}
	if (!buf[0])
		strcopy(buf, len, "-");
}

void Describe(char[] line, int len)
{
	char map[PLATFORM_MAX_PATH], red[128], blu[128];
	GetCurrentMap(map, sizeof(map));
	if (!g_bActive)
	{
		Format(line, len, "map %s: no restrictions (source %s; %d team spawns, %d damaging trigger_hurt)",
			map, g_sSource, g_iSpawns, g_iHurts);
		return;
	}
	MaskToString(g_iAllowed[TEAM_RED], red, sizeof(red), false);
	MaskToString(g_iAllowed[TEAM_BLU], blu, sizeof(blu), false);
	Format(line, len, "map %s: red=[%s] blue=[%s] (source %s; cage spawns red %d blue %d, free red %d blue %d; %d damaging trigger_hurt; 1 human civilian on BLU)",
		map, red, blu, g_sSource, g_iCage[TEAM_RED], g_iCage[TEAM_BLU], g_iFree[TEAM_RED], g_iFree[TEAM_BLU], g_iHurts);
}

/* ---------------------------------------------------------------- enforcement */

bool Enforcing()
{
	return g_bActive && g_cvEnabled.BoolValue;
}

// Civilian on BLU is capped at one human; bots are left to pf2-nextbot (its bot VIP steps down for a human).
int OtherHumanVIP(int client)
{
	for (int i = 1; i <= MaxClients; i++)
	{
		if (i == client || !IsClientInGame(i) || IsFakeClient(i) || GetClientTeam(i) != TEAM_BLU)
			continue;
		if (GetEntProp(i, Prop_Send, "m_iDesiredPlayerClass") == CLASS_CIVILIAN
		 || (GetEntProp(i, Prop_Send, "m_iClass") == CLASS_CIVILIAN && IsPlayerAlive(i)
		     && GetEntProp(i, Prop_Send, "m_iDesiredPlayerClass") == 0))
			return i;
	}
	return 0;
}

bool ClassAllowed(int client, int team, int cls, int &vip = 0)
{
	vip = 0;
	if ((team != TEAM_RED && team != TEAM_BLU) || cls < 1 || cls > 10)
		return true;
	if (!(g_iAllowed[team] & (1 << (cls - 1))))
		return false;
	if (cls == CLASS_CIVILIAN && team == TEAM_BLU && !IsFakeClient(client))
	{
		vip = OtherHumanVIP(client);
		return vip == 0;
	}
	return true;
}

int PickAllowed(int client, int team)
{
	int pool[10], n = 0;
	for (int c = 1; c <= 9; c++)   // never force someone into the VIP role
		if (ClassAllowed(client, team, c))
			pool[n++] = c;
	if (!n && ClassAllowed(client, team, CLASS_CIVILIAN))
		pool[n++] = CLASS_CIVILIAN;
	return n ? pool[GetRandomInt(0, n - 1)] : 0;
}

void Refuse(int client, int team, int cls, int vip)
{
	char allowed[160];
	MaskToString(g_iAllowed[team], allowed, sizeof(allowed), true);
	if (vip)
		PrintToChat(client, "\x04[Hunted]\x01 BLU already has its VIP (%N). Allowed on BLU: %s", vip, allowed);
	else
		PrintToChat(client, "\x04[Hunted]\x01 %s is not allowed for %s on this map. Allowed: %s",
			g_sClassName[cls], team == TEAM_RED ? "RED" : "BLU", allowed);
}

public Action Cmd_JoinClass(int client, const char[] command, int argc)
{
	if (!Enforcing() || client < 1 || !IsClientInGame(client) || IsFakeClient(client) || argc < 1)
		return Plugin_Continue;
	int team = GetClientTeam(client);
	if (team != TEAM_RED && team != TEAM_BLU)
		return Plugin_Continue;

	char arg[32];
	GetCmdArg(1, arg, sizeof(arg));
	if (StrEqual(arg, "random", false) || StrEqual(arg, "auto", false))
	{
		int c = PickAllowed(client, team);
		if (!c)
			return Plugin_Continue;
		FakeClientCommandEx(client, "joinclass %s", g_sClassCmd[c]);
		return Plugin_Handled;
	}
	int cls = ClassFromName(arg);
	int vip;
	if (cls && !ClassAllowed(client, team, cls, vip))
	{
		Refuse(client, team, cls, vip);
		ShowVGUIPanel(client, team == TEAM_BLU ? "class_blue" : "class_red");
		return Plugin_Handled;
	}
	return Plugin_Continue;
}

public void Event_PlayerSpawn(Event event, const char[] name, bool dontBroadcast)
{
	if (Enforcing())
		RequestFrame(Frame_CheckSpawn, event.GetInt("userid"));
}

public void Frame_CheckSpawn(int userid)
{
	int client = GetClientOfUserId(userid);
	if (!Enforcing() || !client || !IsClientInGame(client) || !IsPlayerAlive(client))
		return;
	int team = GetClientTeam(client);
	int cls = GetEntProp(client, Prop_Send, "m_iClass");
	int vip;
	if (ClassAllowed(client, team, cls, vip))
		return;

	// loop guard: something else (another plugin) keeps putting the player back
	float now = GetGameTime();
	if (now - g_fForceWindow[client] > 10.0)
	{
		g_fForceWindow[client] = now;
		g_iForceCount[client] = 0;
	}
	if (++g_iForceCount[client] > 3)
	{
		if (g_iForceCount[client] == 4)
			LogMessage("giving up on %L (%s on team %d keeps coming back; another class plugin?)", client, g_sClassCmd[cls], team);
		return;
	}

	int to = PickAllowed(client, team);
	if (!to)
		return;
	LogMessage("%L spawned as disallowed %s on %s -> %s", client, g_sClassCmd[cls], team == TEAM_RED ? "RED" : "BLU", g_sClassCmd[to]);
	if (!IsFakeClient(client))
	{
		Refuse(client, team, cls, vip);
		PrintToChat(client, "\x04[Hunted]\x01 You have been switched to %s.", g_sClassName[to]);
	}
	SetEntProp(client, Prop_Send, "m_iDesiredPlayerClass", to);
	if (g_hForceRespawn != null)
		SDKCall(g_hForceRespawn, client);   // CTFPlayer::ForceRespawn applies the desired class
	else
		ForcePlayerSuicide(client);
}

/* ---------------------------------------------------------------- classrestrict.smx interplay */

public Action Timer_CheckClassRestrict(Handle timer)
{
	// cvars outlive an unloaded plugin, so require classrestrict.smx to be running
	Handle cr = FindPluginByFile("classrestrict.smx");
	ConVar en = FindConVar("sm_classrestrict_enabled");
	if (cr == null || GetPluginStatus(cr) != Plugin_Running || en == null || !en.BoolValue || !Enforcing())
		return Plugin_Stop;
	static const char cvarName[11][] = { "", "scouts", "snipers", "soldiers", "demomen", "medics",
		"heavies", "pyros", "spies", "engineers", "civilians" };
	char name[64], ours[128], theirs[128];
	for (int t = TEAM_RED; t <= TEAM_BLU; t++)
	{
		int mask = 0;
		for (int c = 1; c <= 10; c++)
		{
			Format(name, sizeof(name), "sm_classrestrict_%s_%s", t == TEAM_RED ? "red" : "blu", cvarName[c]);
			ConVar cv = FindConVar(name);
			if (cv == null || RoundToNearest(cv.FloatValue) != 0)
				mask |= 1 << (c - 1);
		}
		int both = mask & g_iAllowed[t];
		if (both != g_iAllowed[t])
		{
			MaskToString(g_iAllowed[t], ours, sizeof(ours), false);
			MaskToString(mask, theirs, sizeof(theirs), false);
			LogMessage("note: classrestrict.smx is enabled with different %s limits (it allows [%s], hunted rules allow [%s])%s",
				t == TEAM_RED ? "RED" : "BLU", theirs, ours,
				both ? "; players end up in the intersection" : "; NO CLASS SATISFIES BOTH - disable one of them for this map");
		}
	}
	return Plugin_Stop;
}

/* ---------------------------------------------------------------- admin commands */

public Action Cmd_Status(int client, int args)
{
	char line[256];
	Describe(line, sizeof(line));
	ReplyToCommand(client, "[huntedclass] %s%s", line, g_cvEnabled.BoolValue ? "" : " [sm_huntedclass_enabled 0]");
	if (!g_bActive)
		return Plugin_Handled;
	for (int i = 1; i <= MaxClients; i++)
	{
		if (!IsClientInGame(i))
			continue;
		int team = GetClientTeam(i);
		if (team != TEAM_RED && team != TEAM_BLU)
			continue;
		int cls = GetEntProp(i, Prop_Send, "m_iClass");
		ReplyToCommand(client, "  %-24N %s %-12s %s%s", i, team == TEAM_RED ? "RED" : "BLU", cls >= 1 && cls <= 10 ? g_sClassCmd[cls] : "-",
			ClassAllowed(i, team, cls) ? "ok" : "DISALLOWED", IsFakeClient(i) ? " (bot)" : "");
	}
	return Plugin_Handled;
}

public Action Cmd_Test(int client, int args)
{
	if (args < 2 || g_hForceRespawn == null)
	{
		ReplyToCommand(client, "[huntedclass] usage: sm_huntedclass_test <#userid|name> <class> (needs ForceRespawn gamedata)");
		return Plugin_Handled;
	}
	char who[64], arg[32];
	GetCmdArg(1, who, sizeof(who));
	GetCmdArg(2, arg, sizeof(arg));
	int target = FindTarget(client, who, false, false);
	int cls = ClassFromName(arg);
	if (target < 1 || !cls)
	{
		ReplyToCommand(client, "[huntedclass] bad target or class");
		return Plugin_Handled;
	}
	int team = GetClientTeam(target), vip;
	bool ok = ClassAllowed(target, team, cls, vip);
	ReplyToCommand(client, "[huntedclass] %N (%s) -> %s: joinclass would be %s%s", target, team == TEAM_RED ? "RED" : "BLU",
		g_sClassCmd[cls], ok || !Enforcing() ? "allowed" : "REFUSED", vip ? " (BLU already has a human VIP)" : "");
	SetEntProp(target, Prop_Send, "m_iDesiredPlayerClass", cls);
	SDKCall(g_hForceRespawn, target);
	return Plugin_Handled;
}

public Action Cmd_Rederive(int client, int args)
{
	Derive();
	char line[256];
	Describe(line, sizeof(line));
	ReplyToCommand(client, "[huntedclass] %s", line);
	return Plugin_Handled;
}
