// pf2-nextbot: Metamod:Source plugin entry
#pragma once
#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iplayerinfo.h>
#include <sh_vector.h>

static_assert(METAMOD_PLAPI_VERSION == 16, "PF2 runs Metamod 1.11 (plugin API 16); pin METAMOD_PLAPI_VERSION to 16");

#define PF2NB_NAME    "PF2 NextBot"
#define PF2NB_VERSION "0.1.0"
#define PF2NB_TAG     "PF2NB"

class IVEngineServer;
class IServerGameDLL;
class IServerGameClients;
class IServerGameEnts;
class IEngineTrace;
class ICvar;
class IFileSystem;
class IServerPluginHelpers;
class IStaticPropMgrServer;
class CGlobalVars;
class IUniformRandomStream;
class IVDebugOverlay;
class ISpatialPartition;

extern IVEngineServer       *engine;
extern IServerGameDLL       *gamedll;
extern IServerGameClients   *gameclients;
extern IServerGameEnts      *gameents;
extern IEngineTrace         *enginetrace;
extern IGameEventManager2   *gameeventmanager;
extern IPlayerInfoManager   *playerinfomanager;
extern IFileSystem          *filesystem;
extern IServerPluginHelpers *helpers;
#if defined(POSIX) && !defined(random)
#include <stdlib.h>
#define random random_valve   // as game/shared/sharedInterface.h: stdlib.h declares random()
#endif
extern IUniformRandomStream *random;
extern CGlobalVars          *gpGlobals;

class PF2NextBotPlugin : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
	bool Unload(char *error, size_t maxlen) override;
	bool Pause(char *error, size_t maxlen) override { return true; }
	bool Unpause(char *error, size_t maxlen) override { return true; }
	void AllPluginsLoaded() override {}
	const char *GetAuthor() override      { return "pf2-nextbot contributors"; }
	const char *GetName() override        { return PF2NB_NAME; }
	const char *GetDescription() override { return "Valve NextBot/TFBot ported to Pre-Fortress 2 as a server addon"; }
	const char *GetURL() override         { return ""; }
	const char *GetLicense() override     { return "See LICENSE (own code) and third_party/valve/LICENSE (Source 1 SDK License)"; }
	const char *GetVersion() override     { return PF2NB_VERSION; }
	const char *GetDate() override        { return __DATE__; }
	const char *GetLogTag() override      { return PF2NB_TAG; }

	bool Hook_LevelInit(const char *map, const char *ents, const char *oldLevel, const char *landmark, bool loadGame, bool background);
	void Hook_ServerActivate(struct edict_t *list, int count, int clientMax);
	void Hook_LevelShutdown();
	void Hook_GameFrame(bool simulating);
	void Hook_ClientPutInServer(struct edict_t *ent, const char *name);
	void Hook_ClientDisconnect(struct edict_t *ent);
};

extern PF2NextBotPlugin g_Plugin;
PLUGIN_GLOBALVARS();

void PF2NB_Log(const char *fmt, ...);
