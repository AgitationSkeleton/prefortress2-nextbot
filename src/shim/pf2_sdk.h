// pf2-nextbot game layer: engine-public SDK headers shared by everything (game PCH stand-in).
#pragma once
#pragma warning(disable: 4100 4189 4127 4706 4701 4703 4456 4457 4458 4459)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tier0/platform.h>
#include <tier0/basetypes.h>
#include <tier0/dbg.h>
#include <tier0/vprof.h>
#include <tier1/strtools.h>
#include <tier1/utlvector.h>
#include <tier1/utlmap.h>
#include <tier1/utlhash.h>
#include <tier1/utllinkedlist.h>
#include <tier1/utlbuffer.h>
#include <tier1/utlstring.h>
#include <tier1/utldict.h>
#include <tier1/KeyValues.h>
#include <tier1/convar.h>
#include <tier1/fmtstr.h>
#include <tier1/utlsymbol.h>
#include <tier1/UtlSortVector.h>
#include <tier1/utlstack.h>
#include <mathlib/mathlib.h>
#include <mathlib/vector.h>
#include <mathlib/vmatrix.h>
#include <const.h>
#include <string_t.h>
#include <basehandle.h>
#include <ihandleentity.h>
#include <edict.h>
#include <eiface.h>
#include <iserverunknown.h>
#include <iservernetworkable.h>
#include <engine/ICollideable.h>
#include <engine/IEngineTrace.h>
#include <gametrace.h>
#include <cmodel.h>
#include <bspflags.h>
#include <worldsize.h>
#include <collisionutils.h>
#include <filesystem.h>
#include <igameevents.h>
#include <iplayerinfo.h>
#include <vstdlib/random.h>
#include <Color.h>
#include <datamap.h>
#include <vphysics_interface.h>
#include <toolframework/itoolentity.h>
#include "shareddefs.h"
#include "in_buttons.h"
#include "ai_activity.h"
#include "pf2_timers.h"
#include "pf2_vscript.h"

// AI speech concepts (NextBot OnSpokeConcept); PF2 bots never speak via the response system
typedef int AIConcept_t;
class AI_Response;
struct animevent_t;
struct inputdata_t {};
extern ConVar NextBotStop;
extern ConVarRef developer;   // engine cvar

extern IVEngineServer       *engine;
extern IServerGameDLL       *gamedll;
extern IServerGameClients   *gameclients;
extern IServerGameEnts      *gameents;
extern IEngineTrace         *enginetrace;
extern IGameEventManager2   *gameeventmanager;
extern IPlayerInfoManager   *playerinfomanager;
extern IFileSystem          *filesystem;
#if defined(POSIX) && !defined(random)
#define random random_valve   // as game/shared/sharedInterface.h: stdlib.h declares random()
#endif
extern IUniformRandomStream *random;
extern CGlobalVars          *gpGlobals;

#ifndef DECLARE_CLASS
#define DECLARE_CLASS(className, baseClassName) typedef baseClassName BaseClass; typedef className ThisClass;
#define DECLARE_CLASS_NOBASE(className) typedef className ThisClass;
#define DECLARE_CLASS_GAMEROOT(className, baseClassName) typedef baseClassName BaseClass; typedef className ThisClass;
#endif
#define DECLARE_DATADESC()
#define DECLARE_SERVERCLASS()
#define LINK_ENTITY_TO_CLASS(a, b)
#define BEGIN_DATADESC(c)
#define END_DATADESC()
#define assert_cast static_cast

#ifndef MAX_PLAYERS
#define MAX_PLAYERS 101
#endif

typedef CBaseHandle EHANDLE_BASE;
class CTakeDamageInfo;
class CBaseEntity;
float UTIL_VecToYaw(const Vector &vec);
