// pf2-nextbot game layer: UTIL_*, trace filters, entity list, debug overlay, misc game globals.
// Trace results are rewritten so trace_t::m_pEnt holds our proxy, not the raw game entity.
#pragma once
#include "pf2_sdk.h"
#include "pf2_entity.h"

// ---------------------------------------------------------------------------------------------
// Traces
// ---------------------------------------------------------------------------------------------
typedef bool (*ShouldHitFunc_t)(IHandleEntity *pHandleEntity, int contentsMask);

class CTraceFilterSimple : public CTraceFilter
{
public:
	CTraceFilterSimple(const IHandleEntity *passentity, int collisionGroup, ShouldHitFunc_t pExtraShouldHitCheckFn = NULL);
	virtual bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask);
	virtual void SetPassEntity(const IHandleEntity *pPassEntity) { m_pPassEnt = pPassEntity; }
	virtual void SetCollisionGroup(int iCollisionGroup) { m_collisionGroup = iCollisionGroup; }
	const IHandleEntity *GetPassEntity(void) { return m_pPassEnt; }
	int GetCollisionGroup() const { return m_collisionGroup; }
private:
	const IHandleEntity *m_pPassEnt;   // raw game entity (engine side)
	int m_collisionGroup;
	ShouldHitFunc_t m_pExtraShouldHitCheckFunction;
};

class CTraceFilterSkipTwoEntities : public CTraceFilterSimple
{
public:
	CTraceFilterSkipTwoEntities(const IHandleEntity *passentity = NULL, const IHandleEntity *passentity2 = NULL, int collisionGroup = COLLISION_GROUP_NONE);
	virtual bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask);
	virtual void SetPassEntity2(const IHandleEntity *pPassEntity2) { m_pPassEnt2 = pPassEntity2; }
private:
	const IHandleEntity *m_pPassEnt2;
};

class CTraceFilterNoNPCsOrPlayer : public CTraceFilterSimple
{
public:
	CTraceFilterNoNPCsOrPlayer(const IHandleEntity *passentity = NULL, int collisionGroup = COLLISION_GROUP_NONE)
		: CTraceFilterSimple(passentity, collisionGroup) {}
	virtual bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask);
};

class CTraceFilterNoCombatCharacters : public CTraceFilterSimple
{
public:
	CTraceFilterNoCombatCharacters(const IHandleEntity *passentity = NULL, int collisionGroup = COLLISION_GROUP_NONE)
		: CTraceFilterSimple(passentity, collisionGroup) {}
	virtual bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask);
};

class CTraceFilterChain : public CTraceFilter
{
public:
	CTraceFilterChain(ITraceFilter *pTraceFilter1, ITraceFilter *pTraceFilter2) : m_pTraceFilter1(pTraceFilter1), m_pTraceFilter2(pTraceFilter2) {}
	virtual bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask);
private:
	ITraceFilter *m_pTraceFilter1;
	ITraceFilter *m_pTraceFilter2;
};

class CTraceFilterIgnoreTeammates : public CTraceFilterSimple
{
public:
	CTraceFilterIgnoreTeammates(const IHandleEntity *passentity, int collisionGroup, int iIgnoreTeam)
		: CTraceFilterSimple(passentity, collisionGroup), m_iIgnoreTeam(iIgnoreTeam) {}
	virtual bool ShouldHitEntity(IHandleEntity *pServerEntity, int contentsMask);
	int m_iIgnoreTeam;
};
inline void ClientPutInServerOverride(void *) {}
class CTraceFilterIgnoreFriendlyCombatItems : public CTraceFilterSimple
{
public:
	CTraceFilterIgnoreFriendlyCombatItems(const IHandleEntity *passentity, int collisionGroup, int iIgnoreTeam, bool bIsProjectile = false)
		: CTraceFilterSimple(passentity, collisionGroup) {}
};
extern ConVarRef tf_avoidteammates;
extern ConVarRef tf_avoidteammates_pushaway;

// raw (engine) handle for an entity proxy, for filters and engine calls
inline const IHandleEntity *PF2_HandleEntity(const CBaseEntity *e) { return e; }

void UTIL_TraceLine(const Vector &vecAbsStart, const Vector &vecAbsEnd, unsigned int mask, const CBaseEntity *ignore, int collisionGroup, trace_t *ptr);
void UTIL_TraceLine(const Vector &vecAbsStart, const Vector &vecAbsEnd, unsigned int mask, ITraceFilter *pFilter, trace_t *ptr);
void UTIL_TraceHull(const Vector &vecAbsStart, const Vector &vecAbsEnd, const Vector &hullMin, const Vector &hullMax, unsigned int mask, const CBaseEntity *ignore, int collisionGroup, trace_t *ptr);
void UTIL_TraceHull(const Vector &vecAbsStart, const Vector &vecAbsEnd, const Vector &hullMin, const Vector &hullMax, unsigned int mask, ITraceFilter *pFilter, trace_t *ptr);
void UTIL_TraceRay(const Ray_t &ray, unsigned int mask, ITraceFilter *pFilter, trace_t *ptr);
void UTIL_TraceEntity(CBaseEntity *pEntity, const Vector &vecAbsStart, const Vector &vecAbsEnd, unsigned int mask, ITraceFilter *pFilter, trace_t *ptr);
int  UTIL_PointContents(const Vector &vec);
void PF2_FixupTrace(trace_t *tr);   // raw m_pEnt -> proxy

// ---------------------------------------------------------------------------------------------
// Entity list
// ---------------------------------------------------------------------------------------------
class CGlobalEntityList
{
public:
	CBaseEntity *FirstEnt();
	CBaseEntity *NextEnt(CBaseEntity *pCurrentEnt);
	CBaseEntity *FindEntityByClassname(CBaseEntity *pStartEntity, const char *szName);
	CBaseEntity *FindEntityByName(CBaseEntity *pStartEntity, const char *szName, CBaseEntity *pSearchingEntity = NULL, CBaseEntity *pActivator = NULL, CBaseEntity *pCaller = NULL, void *pFilter = NULL);
	CBaseEntity *FindEntityByClassnameNearest(const char *szName, const Vector &vecSrc, float flRadius);
	CBaseEntity *FindEntityInSphere(CBaseEntity *pStartEntity, const Vector &vecCenter, float flRadius);
	CBaseEntity *FindEntityByModel(CBaseEntity *pStartEntity, const char *szModelName) { return NULL; }
};
extern CGlobalEntityList gEntList;

int UTIL_EntitiesInBox(CBaseEntity **pList, int listMax, const Vector &mins, const Vector &maxs, int flagMask);
int UTIL_EntitiesInSphere(CBaseEntity **pList, int listMax, const Vector &center, float radius, int flagMask);

// ---------------------------------------------------------------------------------------------
// Players
// ---------------------------------------------------------------------------------------------
CBasePlayer *UTIL_PlayerByIndex(int playerIndex);
CBasePlayer *UTIL_PlayerByUserId(int userID);
CBasePlayer *UTIL_PlayerByName(const char *name);
CBasePlayer *UTIL_GetListenServerHost(void);
CBasePlayer *UTIL_GetCommandClient(void);
int UTIL_GetCommandClientIndex(void);
bool UTIL_IsCommandIssuedByServerAdmin(void);
int UTIL_ClientsInGame(void);
CBaseEntity *UTIL_EntityByIndex(int entityIndex);
bool UTIL_KickBotFromTeam(int kickTeam);
void UTIL_ClientPrintAll(int msg_dest, const char *msg_name, const char *param1 = NULL, const char *param2 = NULL, const char *param3 = NULL, const char *param4 = NULL);
void UTIL_SayTextAll(const char *pText, CBasePlayer *pPlayer = NULL, bool bChat = false);
void UTIL_LogPrintf(PRINTF_FORMAT_STRING const char *fmt, ...);
char *UTIL_VarArgs(PRINTF_FORMAT_STRING const char *format, ...);
void UTIL_Remove(CBaseEntity *oldObj);
void UTIL_SetOrigin(CBaseEntity *entity, const Vector &vecOrigin, bool bFireTriggers = false);
bool UTIL_IsFacingWithinTolerance(CBaseEntity *pViewer, const Vector &vecPosition, float flDotTolerance, float *pflDot = NULL);
bool UTIL_IsFacingWithinTolerance(CBaseEntity *pViewer, CBaseEntity *pTarget, float flDotTolerance, float *pflDot = NULL);
float UTIL_VecToYaw(const Vector &vec);
float UTIL_VecToPitch(const Vector &vec);
float UTIL_AngleDiff(float destAngle, float srcAngle);
inline float UTIL_AngleMod(float a) { return anglemod(a); }
bool IsDedicatedServer();

#define HUD_PRINTNOTIFY  1
#define HUD_PRINTCONSOLE 2
#define HUD_PRINTTALK    3
#define HUD_PRINTCENTER  4

// ---------------------------------------------------------------------------------------------
// Debug overlay: no listen-server client here, so these are no-ops (nb_debug output is console text)
// ---------------------------------------------------------------------------------------------
namespace NDebugOverlay
{
	inline void Box(const Vector &, const Vector &, const Vector &, int, int, int, int, float) {}
	inline void BoxDirection(const Vector &, const Vector &, const Vector &, const Vector &, int, int, int, int, float) {}
	inline void BoxAngles(const Vector &, const Vector &, const Vector &, const QAngle &, int, int, int, int, float) {}
	inline void SweptBox(const Vector &, const Vector &, const Vector &, const Vector &, const QAngle &, int, int, int, int, float) {}
	inline void EntityBounds(const CBaseEntity *, int, int, int, int, float) {}
	inline void Line(const Vector &, const Vector &, int, int, int, bool, float) {}
	inline void Triangle(const Vector &, const Vector &, const Vector &, int, int, int, int, bool, float) {}
	inline void EntityText(int, int, const char *, float, int = 255, int = 255, int = 255, int = 255) {}
	inline void EntityTextAtPosition(const Vector &, int, const char *, float, int = 255, int = 255, int = 255, int = 255) {}
	inline void Grid(const Vector &) {}
	inline void Text(const Vector &, const char *, bool, float) {}
	inline void ScreenText(float, float, const char *, int, int, int, int, float) {}
	inline void Cross3D(const Vector &, const Vector &, const Vector &, int, int, int, bool, float) {}
	inline void Cross3D(const Vector &, float, int, int, int, bool, float) {}
	inline void Cross3DOriented(const Vector &, const QAngle &, float, int, int, int, bool, float) {}
	inline void HorzArrow(const Vector &, const Vector &, float, int, int, int, int, bool, float) {}
	inline void YawArrow(const Vector &, float, float, float, int, int, int, int, bool, float) {}
	inline void VertArrow(const Vector &, const Vector &, float, int, int, int, int, bool, float) {}
	inline void Axis(const Vector &, const QAngle &, float, bool, float) {}
	inline void Sphere(const Vector &, float, int, int, int, bool, float) {}
	inline void Sphere(const Vector &, const QAngle &, float, int, int, int, int, bool, float) {}
	inline void Circle(const Vector &, float, int, int, int, int, bool, float) {}
	inline void Circle(const Vector &, const QAngle &, float, int, int, int, int, bool, float) {}
	inline void Circle(const Vector &, const Vector &, const Vector &, float, int, int, int, int, bool, float) {}
	inline void Clear() {}
}

// ---------------------------------------------------------------------------------------------
// Game event listener base (game's CGameEventListener)
// ---------------------------------------------------------------------------------------------
void PF2_InvalidateEntityCache();
class CGameEventListener : public IGameEventListener2
{
public:
	CGameEventListener() : m_bRegisteredForEvents(false) { m_adapter.m_outer = this; }
	virtual ~CGameEventListener() { StopListeningForAllEvents(); }
	void ListenForGameEvent(const char *name)
	{
		m_bRegisteredForEvents = true;
		gameeventmanager->AddListener(&m_adapter, name, true);
	}
	void StopListeningForAllEvents()
	{
		if (m_bRegisteredForEvents && gameeventmanager)
		{
			gameeventmanager->RemoveListener(&m_adapter);
			m_bRegisteredForEvents = false;
		}
	}
	virtual void FireGameEvent(IGameEvent *event) = 0;
private:
	// the engine calls the adapter; entity deletions may have happened since our last lookup
	struct Adapter : public IGameEventListener2
	{
		CGameEventListener *m_outer;
		void FireGameEvent(IGameEvent *event) override { PF2_InvalidateEntityCache(); m_outer->FireGameEvent(event); }
	} m_adapter;
	bool m_bRegisteredForEvents;
};

// misc
// IFileSystem::ReadFile grows the caller's CUtlBuffer inside the filesystem DLL (engine allocator), which
// this addon's CRT heap cannot free. PF2_ReadFile reads through Open/Read into memory allocated here.
bool PF2_ReadFile(const char *pFileName, const char *pPath, CUtlBuffer &buf);
#define MDLCACHE_CRITICAL_SECTION()
inline bool FStrEq(const char *a, const char *b) { return V_strcmp(a, b) == 0; }
struct CP4AutoEditAddFile { CP4AutoEditAddFile(const char *) {} };
template <size_t N> const char *GetCleanMapName(const char *pszUnCleanMapName, char (&pszTmp)[N]) { V_strncpy(pszTmp, pszUnCleanMapName, N); return pszTmp; }
class IPhysicsSurfaceProps;
extern IPhysicsSurfaceProps *physprops;
#define NDEBUG_PERSIST_TILL_NEXT_SERVER (0.01023f)
inline bool FNullEnt(const edict_t *pent) { return pent == NULL || pent->IsFree(); }
#define FCVAR_CHEAT_OR_DEVONLY FCVAR_CHEAT
