// pf2-nextbot: the per-bot hook point. PF2's own puppet-bot loop (Bot_RunAll -> Bot_Think ->
// RunPlayerMove) calls CBasePlayer::PlayerRunCommand once per frame per fake client. We hook that
// call and let the bot's brain rewrite the usercmd, which is what NextBotPlayer::PhysicsSimulate
// does natively in TF2.
#pragma once
#include <mathlib/vector.h>

#define PF2NB_MAXPLAYERS 101   // slots 1..100 (srcds hard max is 64 for sdk2013 + headroom)

class CUserCmd;   // PF2 layout, defined in src/shim/pf2_player.h
typedef CUserCmd PF2UserCmd;

class IPF2BotDriver
{
public:
	virtual ~IPF2BotDriver() {}
	// Called from inside PlayerRunCommand (pre) for this bot, once per server frame.
	virtual void OnRunCommand(PF2UserCmd *cmd) = 0;
};

namespace pf2 {
void Drivers_Set(int entindex, IPF2BotDriver *driver);
IPF2BotDriver *Drivers_Get(int entindex);
int  Drivers_HookCalls(int entindex);          // number of hooked RunCommand calls seen for this slot
bool Drivers_HookHealthy();                    // false once a bot went 5 s without a hooked call
void Drivers_InstallHook(void *anyPlayerRaw);  // vtable hook (shared by all players); idempotent
void Drivers_RemoveHook();
void Drivers_Frame();                          // health check
}
