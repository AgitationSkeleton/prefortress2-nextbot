// pf2-nextbot: bot lifecycle glue (spawning PF2 puppet bots, quota, map changes)
#pragma once
struct edict_t;

namespace pf2 {
void BotManager_OnLoad(bool late);
void BotManager_OnUnload();
void BotManager_OnLevelInit(const char *map);
void BotManager_OnServerActivate();
void BotManager_OnLevelShutdown();
void BotManager_OnGameFrame(bool simulating);
void BotManager_OnClientPutInServer(edict_t *ent, const char *name);
void BotManager_OnClientDisconnect(edict_t *ent);

// Spawn one PF2 puppet bot through the game's own "bot" command (cheat flag lifted for the call).
// team: "red"/"blue"; cls: PF2 class name ("soldier", "civilian", ...). Returns entindex or 0.
typedef void (*SpawnCallback)(int entindex, void *ctx);   // entindex 0 = failed
void Puppet_Request(const char *team, const char *cls, const char *name, SpawnCallback cb, void *ctx);
int  Puppet_Pending();
void Puppet_Frame();
void *RawEntity(int entindex);          // real CBaseEntity* for an index, or NULL
}
