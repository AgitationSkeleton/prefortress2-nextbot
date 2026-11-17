// pf2-nextbot: PF2 puppet-bot spawning (the game's own "bot" command) and raw entity access.
#include <stdio.h>
#include "plugin.h"
#include <tier0/platform.h>
#include "botdriver.h"
#include "botmanager.h"
#include <eiface.h>
#include <iserverunknown.h>
#include <iservernetworkable.h>
#include <convar.h>
#include <edict.h>
#include "game/pf2_props.h"
#include "game/pf2_offsets.h"
#include "game/vfunc.h"
#include <utlvector.h>

namespace pf2 {

void *RawEntity(int idx)
{
	edict_t *ed = engine->PEntityOfEntIndex(idx);
	if (!ed || ed->IsFree())
		return NULL;
	IServerUnknown *u = ed->GetUnknown();
	return u ? (void *)u->GetBaseEntity() : NULL;
}

static bool IsFakeClientIdx(int idx)
{
	void *raw = RawEntity(idx);
	if (!raw)
		return false;
	int *flags = PF2_SENDPROP_PTR(int, raw, "m_fFlags");
	return flags && (*flags & FL_FAKECLIENT);
}

// ConCommandBase::m_nFlags is private and sdk2013 has no RemoveFlags(); mirror the layout.
struct ConCommandBaseMirror { void *vt; void *next; bool registered; const char *name; const char *help; int flags; };
static int &CmdFlags(ConCommandBase *c) { return ((ConCommandBaseMirror *)c)->flags; }

// One spawn at a time, driven from GameFrame. The engine runs the queued "bot" command later in
// the frame (rcon/server command buffers defer ServerExecute), so the cheat flag stays lifted until
// the new fake client appears or 0.5 s pass.
struct SpawnReq { char team[16]; char cls[24]; char name[64]; SpawnCallback cb; void *ctx; };
static CUtlVector<SpawnReq> s_spawnQueue;
static bool  s_spawnBusy = false;
static float s_spawnStarted = 0.0f;
static int   s_spawnOldFlags = 0;
static bool  s_spawnSeen[PF2NB_MAXPLAYERS + 1];
static SpawnReq s_spawnCur;

void Puppet_Request(const char *team, const char *cls, const char *name, SpawnCallback cb, void *ctx)
{
	SpawnReq r;
	V_strncpy(r.team, team, sizeof(r.team));
	V_strncpy(r.cls, cls, sizeof(r.cls));
	V_strncpy(r.name, name, sizeof(r.name));
	r.cb = cb;
	r.ctx = ctx;
	s_spawnQueue.AddToTail(r);
}

int Puppet_Pending() { return s_spawnQueue.Count() + (s_spawnBusy ? 1 : 0); }

static void Puppet_Finish(int idx)
{
	ConCommand *botCmd = g_pCVar->FindCommand("bot");
	if (botCmd)
		CmdFlags(botCmd) = s_spawnOldFlags;
	s_spawnBusy = false;
	if (!idx)
		PF2NB_Log("spawn of '%s' failed (no new fake client)", s_spawnCur.name);
	if (s_spawnCur.cb)
		s_spawnCur.cb(idx, s_spawnCur.ctx);
}

void Puppet_Frame()
{
	if (s_spawnBusy)
	{
		for (int i = 1; i <= gpGlobals->maxClients; ++i)
			if (!s_spawnSeen[i] && RawEntity(i) && IsFakeClientIdx(i))
			{
				Puppet_Finish(i);
				return;
			}
		if ((float)Plat_FloatTime() - s_spawnStarted > 0.5f)
			Puppet_Finish(0);
		return;
	}
	if (!s_spawnQueue.Count())
		return;
	s_spawnCur = s_spawnQueue[0];
	s_spawnQueue.Remove(0);
	ConCommand *botCmd = g_pCVar->FindCommand("bot");
	if (!botCmd || (((CmdFlags(botCmd) & FCVAR_CHEAT) != 0) != botCmd->IsFlagSet(FCVAR_CHEAT)))
	{
		PF2NB_Log("ERROR: no usable 'bot' command (missing or ConCommandBase layout mismatch)");
		if (s_spawnCur.cb)
			s_spawnCur.cb(0, s_spawnCur.ctx);
		return;
	}
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
		s_spawnSeen[i] = RawEntity(i) != NULL;
	s_spawnOldFlags = CmdFlags(botCmd);
	CmdFlags(botCmd) &= ~FCVAR_CHEAT;
	char line[256];
	V_snprintf(line, sizeof(line), "bot -team %s -class %s -name \"%s\"\n", s_spawnCur.team, s_spawnCur.cls, s_spawnCur.name);
	engine->ServerCommand(line);
	s_spawnBusy = true;
	s_spawnStarted = (float)Plat_FloatTime();
}

} // namespace pf2
