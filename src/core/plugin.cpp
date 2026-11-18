// pf2-nextbot: Metamod:Source plugin entry, interfaces, frame hooks, PlayerRunCommand hook.
#include <stdio.h>
#include <stdarg.h>
#include "plugin.h"
#include "botdriver.h"
#include "botmanager.h"
#include <eiface.h>
#include <iserverunknown.h>
#include <iservernetworkable.h>
#include <convar.h>
#include <engine/IEngineTrace.h>
#include <filesystem.h>
#include <vstdlib/random.h>
#include <vphysics_interface.h>
#include <toolframework/itoolentity.h>
#include "game/pf2_offsets.h"
#include "perf.h"

SH_DECL_HOOK6(IServerGameDLL, LevelInit, SH_NOATTRIB, 0, bool, char const *, char const *, char const *, char const *, bool, bool);
SH_DECL_HOOK3_void(IServerGameDLL, ServerActivate, SH_NOATTRIB, 0, edict_t *, int, int);
SH_DECL_HOOK0_void(IServerGameDLL, LevelShutdown, SH_NOATTRIB, 0);
SH_DECL_HOOK1_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool);
SH_DECL_HOOK2_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, edict_t *, char const *);
SH_DECL_HOOK1_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, edict_t *);

class CUserCmd;
class IMoveHelper;
SH_DECL_MANUALHOOK2_void(PlayerRunCmdHook, pf2::vt::PlayerRunCommand, 0, 0, CUserCmd *, IMoveHelper *);

PF2NextBotPlugin g_Plugin;
PLUGIN_EXPOSE(PF2NextBotPlugin, g_Plugin);

IVEngineServer       *engine = NULL;
IServerGameDLL       *gamedll = NULL;
IServerGameClients   *gameclients = NULL;
IServerGameEnts      *gameents = NULL;
IEngineTrace         *enginetrace = NULL;
IGameEventManager2   *gameeventmanager = NULL;
IPlayerInfoManager   *playerinfomanager = NULL;
IFileSystem          *filesystem = NULL;
IServerPluginHelpers *helpers = NULL;
IUniformRandomStream *random = NULL;
CGlobalVars          *gpGlobals = NULL;
ICvar                *icvar = NULL;

class BaseAccessor : public IConCommandBaseAccessor
{
public:
	bool RegisterConCommandBase(ConCommandBase *p) override { return META_REGCVAR(p); }
} s_BaseAccessor;

void PF2NB_Log(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	V_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	META_CONPRINTF("[PF2NB] %s\n", buf);
}

// ---------------------------------------------------------------------------------------------
// PlayerRunCommand hook (one vtable hook shared by every CTFPlayer; we only act on our bots)
// ---------------------------------------------------------------------------------------------
void PF2_InvalidateEntityCache();
namespace pf2 {
using ::PF2_InvalidateEntityCache;
static IPF2BotDriver *s_drivers[PF2NB_MAXPLAYERS + 1];
static int   s_hookCalls[PF2NB_MAXPLAYERS + 1];
static float s_driverSince[PF2NB_MAXPLAYERS + 1];
static int   s_hookId = 0;
static bool  s_hookHealthy = true;

void Drivers_Set(int idx, IPF2BotDriver *d)
{
	if (idx < 1 || idx > PF2NB_MAXPLAYERS)
		return;
	s_drivers[idx] = d;
	s_hookCalls[idx] = 0;
	s_driverSince[idx] = gpGlobals ? gpGlobals->curtime : 0.0f;
}
IPF2BotDriver *Drivers_Get(int idx) { return (idx >= 1 && idx <= PF2NB_MAXPLAYERS) ? s_drivers[idx] : NULL; }
int Drivers_HookCalls(int idx) { return (idx >= 1 && idx <= PF2NB_MAXPLAYERS) ? s_hookCalls[idx] : 0; }
bool Drivers_HookHealthy() { return s_hookHealthy; }

static void Hook_PlayerRunCmd(CUserCmd *ucmd, IMoveHelper *)
{
	void *self = META_IFACEPTR(void);
	PF2_InvalidateEntityCache();   // entity deletions are flushed between calls
	edict_t *ed = gameents->BaseEntityToEdict((CBaseEntity *)self);
	int idx = ed ? engine->IndexOfEdict(ed) : 0;
	if (idx >= 1 && idx <= PF2NB_MAXPLAYERS && s_drivers[idx] && ucmd)
	{
		++s_hookCalls[idx];
		s_drivers[idx]->OnRunCommand((PF2UserCmd *)ucmd);
	}
	RETURN_META(MRES_IGNORED);
}

void Drivers_InstallHook(void *anyPlayerRaw)
{
	if (s_hookId || !anyPlayerRaw)
		return;
	s_hookId = SH_ADD_MANUALVPHOOK(PlayerRunCmdHook, anyPlayerRaw, SH_STATIC(Hook_PlayerRunCmd), false);
	PF2NB_Log("PlayerRunCommand hook installed at vtable slot %d (id %d)", (int)vt::PlayerRunCommand, s_hookId);
}

void Drivers_RemoveHook()
{
	if (s_hookId)
		SH_REMOVE_HOOK_ID(s_hookId);
	s_hookId = 0;
}

void Drivers_Frame()
{
	if (!s_hookHealthy || !gpGlobals)
		return;
	for (int i = 1; i <= gpGlobals->maxClients && i <= PF2NB_MAXPLAYERS; ++i)
	{
		if (s_drivers[i] && s_hookCalls[i] == 0 && gpGlobals->curtime - s_driverSince[i] > 5.0f)
		{
			s_hookHealthy = false;
			PF2NB_Log("ERROR: bot #%d got no PlayerRunCommand call in 5 s. The vtable slot (%d) is wrong "
				"for this server.dll build. Bot driving is disabled; update src/game/pf2_offsets.h.",
				i, (int)vt::PlayerRunCommand);
			return;
		}
	}
}
} // namespace pf2

// ---------------------------------------------------------------------------------------------
bool PF2NextBotPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, gameeventmanager, IGameEventManager2, INTERFACEVERSION_GAMEEVENTSMANAGER2);
	GET_V_IFACE_CURRENT(GetEngineFactory, helpers, IServerPluginHelpers, INTERFACEVERSION_ISERVERPLUGINHELPERS);
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, enginetrace, IEngineTrace, INTERFACEVERSION_ENGINETRACE_SERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, random, IUniformRandomStream, VENGINE_SERVER_RANDOM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, gamedll, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
	GET_V_IFACE_ANY(GetServerFactory, playerinfomanager, IPlayerInfoManager, INTERFACEVERSION_PLAYERINFOMANAGER);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	{
		extern IServerTools *servertools;
		// v001 is enough: only FirstEntity/NextEntity are used (stable slots in every version)
		servertools = (IServerTools *)ismm->GetServerFactory(false)("VSERVERTOOLS001", NULL);
		if (!servertools)
			PF2NB_Log("WARNING: IServerTools unavailable; server-only entities (spawn points, filters) are invisible");
	}
	gpGlobals = ismm->GetCGlobals();
	{
		extern IPhysicsSurfaceProps *physprops;
		CreateInterfaceFn phys = ismm->GetPhysicsFactory();
		physprops = phys ? (IPhysicsSurfaceProps *)phys(VPHYSICS_SURFACEPROPS_INTERFACE_VERSION, NULL) : NULL;
	}

	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, LevelInit, gamedll, this, &PF2NextBotPlugin::Hook_LevelInit, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, ServerActivate, gamedll, this, &PF2NextBotPlugin::Hook_ServerActivate, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, LevelShutdown, gamedll, this, &PF2NextBotPlugin::Hook_LevelShutdown, false);
	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, GameFrame, gamedll, this, &PF2NextBotPlugin::Hook_GameFrame, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientPutInServer, gameclients, this, &PF2NextBotPlugin::Hook_ClientPutInServer, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, gameclients, this, &PF2NextBotPlugin::Hook_ClientDisconnect, false);

	g_pCVar = icvar;
	ConVar_Register(0, &s_BaseAccessor);

	pf2::BotManager_OnLoad(late);
	PF2NB_Log("%s %s loaded (built %s)", PF2NB_NAME, PF2NB_VERSION, __DATE__);
	return true;
}

bool PF2NextBotPlugin::Unload(char *error, size_t maxlen)
{
	pf2::BotManager_OnUnload();
	pf2::Drivers_RemoveHook();
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, LevelInit, gamedll, this, &PF2NextBotPlugin::Hook_LevelInit, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, ServerActivate, gamedll, this, &PF2NextBotPlugin::Hook_ServerActivate, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, LevelShutdown, gamedll, this, &PF2NextBotPlugin::Hook_LevelShutdown, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameFrame, gamedll, this, &PF2NextBotPlugin::Hook_GameFrame, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientPutInServer, gameclients, this, &PF2NextBotPlugin::Hook_ClientPutInServer, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, gameclients, this, &PF2NextBotPlugin::Hook_ClientDisconnect, false);
	ConVar_Unregister();
	return true;
}

bool PF2NextBotPlugin::Hook_LevelInit(const char *map, const char *, const char *, const char *, bool, bool)
{
	pf2::BotManager_OnLevelInit(map);
	RETURN_META_VALUE(MRES_IGNORED, true);
}

void PF2NextBotPlugin::Hook_ServerActivate(edict_t *, int, int)
{
	pf2::BotManager_OnServerActivate();
	RETURN_META(MRES_IGNORED);
}

void PF2NextBotPlugin::Hook_LevelShutdown()
{
	pf2::BotManager_OnLevelShutdown();
	RETURN_META(MRES_IGNORED);
}

void PF2NextBotPlugin::Hook_GameFrame(bool simulating)
{
	pf2::PF2_InvalidateEntityCache();
	pf2::Drivers_Frame();
	{
		perf::Scope s(perf::GAMEFRAME);
		pf2::BotManager_OnGameFrame(simulating);
	}
	perf::EndFrame();
	RETURN_META(MRES_IGNORED);
}

void PF2NextBotPlugin::Hook_ClientPutInServer(edict_t *ent, const char *name)
{
	pf2::BotManager_OnClientPutInServer(ent, name);
	RETURN_META(MRES_IGNORED);
}

void PF2NextBotPlugin::Hook_ClientDisconnect(edict_t *ent)
{
	pf2::BotManager_OnClientDisconnect(ent);
	RETURN_META(MRES_IGNORED);
}
