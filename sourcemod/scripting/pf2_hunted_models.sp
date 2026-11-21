/**
 * pf2_hunted_models - keep hunted maps' team-coloured custom models on the right players.
 *
 * hunted_actually_accurate_a1/a2 recolour RED's snipers (the assassins) with a trigger in RED's spawn:
 * trigger_multiple "PissOnEverySniper" + filter_tf_class "filter_sniper" fire
 * "!activator,SetCustomModel,models/player/sniper_ylw.mdl". That goes wrong two ways:
 *   - a player whose class changes right after spawning (class restriction plugin, bot class choice)
 *     can leave the trigger before it fires, and stays red;
 *   - nothing ever clears the model, so a player who switches to BLU or to another class keeps the
 *     yellow sniper model (Civilians looking like snipers).
 * On the listed maps this plugin owns the model instead: shortly after every spawn, class change and
 * team change, and in a periodic sweep, RED snipers get the model and anyone else wearing it has it
 * cleared. Other custom models (other plugins) are left alone.
 */
#include <sourcemod>
#include <sdktools>

#pragma semicolon 1
#pragma newdecls required

#define TEAM_RED 2
#define CLASS_SNIPER 2

public Plugin myinfo =
{
	name = "PF2 Hunted Models",
	author = "pf2-nextbot contributors",
	description = "Keep hunted maps' recoloured sniper models on RED snipers only",
	version = "1.0.0",
	url = ""
};

ConVar g_cvEnabled;
ConVar g_cvMaps;
ConVar g_cvModel;
bool g_bActive;
char g_sModel[PLATFORM_MAX_PATH];

public void OnPluginStart()
{
	g_cvEnabled = CreateConVar("sm_huntedmodels_enabled", "1", "Fix hunted maps' recoloured sniper models", _, true, 0.0, true, 1.0);
	g_cvMaps = CreateConVar("sm_huntedmodels_maps", "hunted_actually_accurate*",
		"Comma-separated map names (trailing * = prefix) where RED snipers wear sm_huntedmodels_model");
	g_cvModel = CreateConVar("sm_huntedmodels_model", "models/player/sniper_ylw.mdl", "The custom model RED snipers wear on those maps");
	AutoExecConfig(true, "pf2_hunted_models");

	HookEvent("player_spawn", Event_Player);
	HookEvent("player_team", Event_Player);
	HookEvent("player_changeclass", Event_Player);
}

public void OnConfigsExecuted()
{
	g_cvModel.GetString(g_sModel, sizeof(g_sModel));
	g_bActive = g_cvEnabled.BoolValue && MapListed() && g_sModel[0] != '\0';
	if (g_bActive)
		PrecacheModel(g_sModel, true);
	CreateTimer(2.0, Timer_Sweep, _, TIMER_REPEAT | TIMER_FLAG_NO_MAPCHANGE);
}

static bool MapListed()
{
	char map[PLATFORM_MAX_PATH], list[512], parts[16][PLATFORM_MAX_PATH];
	GetCurrentMap(map, sizeof(map));
	g_cvMaps.GetString(list, sizeof(list));
	int n = ExplodeString(list, ",", parts, sizeof(parts), sizeof(parts[]));
	for (int i = 0; i < n; i++)
	{
		TrimString(parts[i]);
		int len = strlen(parts[i]);
		if (len == 0)
			continue;
		if (parts[i][len - 1] == '*')
		{
			parts[i][len - 1] = '\0';
			if (StrContains(map, parts[i], false) == 0)
				return true;
		}
		else if (StrEqual(map, parts[i], false))
			return true;
	}
	return false;
}

public void Event_Player(Event event, const char[] name, bool dontBroadcast)
{
	if (!g_bActive)
		return;
	int userid = event.GetInt("userid");
	// after the game (and the map's own trigger, which fires 0.02 s after touch) has settled, and again later
	CreateTimer(0.2, Timer_Fix, userid, TIMER_FLAG_NO_MAPCHANGE);
	CreateTimer(1.0, Timer_Fix, userid, TIMER_FLAG_NO_MAPCHANGE);
}

public Action Timer_Fix(Handle timer, any userid)
{
	int client = GetClientOfUserId(userid);
	if (client > 0 && g_bActive)
		FixClient(client);
	return Plugin_Stop;
}

public Action Timer_Sweep(Handle timer)
{
	if (g_bActive)
		for (int client = 1; client <= MaxClients; client++)
			FixClient(client);
	return Plugin_Continue;
}

static void FixClient(int client)
{
	if (!IsClientInGame(client) || !IsPlayerAlive(client))
		return;
	char current[PLATFORM_MAX_PATH];
	GetEntPropString(client, Prop_Send, "m_iszCustomModel", current, sizeof(current));
	bool wear = GetClientTeam(client) == TEAM_RED && GetEntProp(client, Prop_Send, "m_iClass") == CLASS_SNIPER;
	if (wear && !StrEqual(current, g_sModel, false))
	{
		if (current[0] != '\0')
			return;   // someone else's custom model: leave it
		SetModel(client, g_sModel);
	}
	else if (!wear && StrEqual(current, g_sModel, false))
		SetModel(client, "");
}

static void SetModel(int client, const char[] model)
{
	SetVariantString(model);
	if (!AcceptEntityInput(client, "SetCustomModel"))
		SetEntPropString(client, Prop_Send, "m_iszCustomModel", model);
	SetEntProp(client, Prop_Send, "m_bUseClassAnimations", model[0] != '\0' ? 1 : 0);
}
