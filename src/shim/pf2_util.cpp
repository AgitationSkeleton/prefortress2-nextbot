// pf2-nextbot game layer: UTIL_* implementations, trace filters, entity list.
#include "cbase.h"
#include "game/vfunc.h"
#include "game/pf2_offsets.h"
#include <stdarg.h>

static CPF2NullScriptVM s_nullVM;
CPF2NullScriptVM *g_pScriptVM = &s_nullVM;
CGlobalEntityList gEntList;
ConVarRef developer("developer", true);
ConVarRef tf_avoidteammates("tf_avoidteammates", true);
ConVarRef tf_avoidteammates_pushaway("tf_avoidteammates_pushaway", true);

float IntervalTimer::Now(void) const { return gpGlobals->curtime; }
float CountdownTimer::Now(void) const { return gpGlobals->curtime; }

// ---------------------------------------------------------------------------------------------
// Traces
// ---------------------------------------------------------------------------------------------
static bool PassServerEntityFilter(const IHandleEntity *pTouch, const IHandleEntity *pPass)
{
	if (!pPass)
		return true;
	CBaseEntity *touch = PF2_EntityFromHandleEntity((IHandleEntity *)pTouch);
	CBaseEntity *pass = PF2_EntityFromHandleEntity((IHandleEntity *)pPass);   // may be raw or proxy
	if (!touch || !pass)
		return true;
	if (touch == pass)
		return false;
	// don't clip against own missiles / owner
	if (touch->GetOwnerEntity() == pass)
		return false;
	if (pass->GetOwnerEntity() == touch)
		return false;
	return true;
}

CTraceFilterSimple::CTraceFilterSimple(const IHandleEntity *passedict, int collisionGroup, ShouldHitFunc_t pExtraShouldHitFunc)
	: m_pPassEnt(passedict), m_collisionGroup(collisionGroup), m_pExtraShouldHitCheckFunction(pExtraShouldHitFunc)
{
}

// CTFGameRules::ShouldCollide / CGameRules::ShouldCollide collision-group rules that matter for
// movement traces (Source SDK 2013, tf_gamerules.cpp / gamerules.cpp)
static bool GameRulesShouldCollide(int g0, int g1)
{
	if (g0 > g1) { int t = g0; g0 = g1; g1 = t; }
	if (g0 == COLLISION_GROUP_PLAYER_MOVEMENT && g1 == COLLISION_GROUP_WEAPON) return false;
	if (g0 == COLLISION_GROUP_PLAYER_MOVEMENT && g1 == COLLISION_GROUP_PROJECTILE) return false;
	if (g0 == COLLISION_GROUP_DEBRIS && g1 == COLLISION_GROUP_DEBRIS) return false;
	if ((g0 == COLLISION_GROUP_PLAYER || g0 == COLLISION_GROUP_PLAYER_MOVEMENT) && g1 == COLLISION_GROUP_PUSHAWAY) return false;
	if (g0 == COLLISION_GROUP_DEBRIS || g1 == COLLISION_GROUP_DEBRIS)
		return g0 == COLLISION_GROUP_NONE ? (g1 == COLLISION_GROUP_DEBRIS ? false : true) : false;   // debris only hits world/none
	if (g0 == COLLISION_GROUP_DEBRIS_TRIGGER || g1 == COLLISION_GROUP_DEBRIS_TRIGGER) return false;
	if (g0 == COLLISION_GROUP_IN_VEHICLE || g1 == COLLISION_GROUP_IN_VEHICLE) return false;
	return true;
}

bool CTraceFilterSimple::ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask)
{
	if (!PassServerEntityFilter(pHandleEntity, m_pPassEnt))
		return false;
	CBaseEntity *ent = PF2_EntityFromHandleEntity(pHandleEntity);
	if (!ent)
		return false;
	// the entity's own rule (team-only spawn barriers, teammates, buildings), as CTraceFilterSimple does
	void *raw = ent->GetRaw();
	if (raw && !pf2::VCall<bool>(raw, pf2::vt::ShouldCollide, m_collisionGroup, contentsMask))
		return false;
	if (!GameRulesShouldCollide(m_collisionGroup, ent->GetCollisionGroup()))
		return false;
	if (m_pExtraShouldHitCheckFunction && !m_pExtraShouldHitCheckFunction(pHandleEntity, contentsMask))
		return false;
	return true;
}

CTraceFilterSkipTwoEntities::CTraceFilterSkipTwoEntities(const IHandleEntity *passentity, const IHandleEntity *passentity2, int collisionGroup)
	: CTraceFilterSimple(passentity, collisionGroup), m_pPassEnt2(passentity2)
{
}

bool CTraceFilterSkipTwoEntities::ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask)
{
	if (!PassServerEntityFilter(pHandleEntity, m_pPassEnt2))
		return false;
	return CTraceFilterSimple::ShouldHitEntity(pHandleEntity, contentsMask);
}

bool CTraceFilterNoNPCsOrPlayer::ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask)
{
	if (!CTraceFilterSimple::ShouldHitEntity(pHandleEntity, contentsMask))
		return false;
	CBaseEntity *ent = PF2_EntityFromHandleEntity(pHandleEntity);
	return ent && !ent->IsPlayer() && !ent->IsNPC();
}

bool CTraceFilterNoCombatCharacters::ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask)
{
	if (!CTraceFilterSimple::ShouldHitEntity(pHandleEntity, contentsMask))
		return false;
	CBaseEntity *ent = PF2_EntityFromHandleEntity(pHandleEntity);
	return ent && !ent->MyCombatCharacterPointer();
}

bool CTraceFilterIgnoreTeammates::ShouldHitEntity(IHandleEntity *pServerEntity, int contentsMask)
{
	CBaseEntity *ent = PF2_EntityFromHandleEntity(pServerEntity);
	if (ent && ent->IsPlayer() && ent->GetTeamNumber() == m_iIgnoreTeam)
		return false;
	return CTraceFilterSimple::ShouldHitEntity(pServerEntity, contentsMask);
}

bool CTraceFilterChain::ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask)
{
	bool r1 = m_pTraceFilter1 ? m_pTraceFilter1->ShouldHitEntity(pHandleEntity, contentsMask) : true;
	bool r2 = m_pTraceFilter2 ? m_pTraceFilter2->ShouldHitEntity(pHandleEntity, contentsMask) : true;
	return r1 && r2;
}

void PF2_FixupTrace(trace_t *tr)
{
	if (tr && tr->m_pEnt)
		tr->m_pEnt = PF2_EntityFromRaw(tr->m_pEnt);   // engine gives the raw entity
}

void UTIL_TraceRay(const Ray_t &ray, unsigned int mask, ITraceFilter *pFilter, trace_t *ptr)
{
	enginetrace->TraceRay(ray, mask, pFilter, ptr);
	PF2_FixupTrace(ptr);
}

void UTIL_TraceLine(const Vector &a, const Vector &b, unsigned int mask, const CBaseEntity *ignore, int collisionGroup, trace_t *ptr)
{
	Ray_t ray;
	ray.Init(a, b);
	CTraceFilterSimple filter(PF2_HandleEntity(ignore), collisionGroup);
	UTIL_TraceRay(ray, mask, &filter, ptr);
}

void UTIL_TraceLine(const Vector &a, const Vector &b, unsigned int mask, ITraceFilter *pFilter, trace_t *ptr)
{
	Ray_t ray;
	ray.Init(a, b);
	UTIL_TraceRay(ray, mask, pFilter, ptr);
}

void UTIL_TraceHull(const Vector &a, const Vector &b, const Vector &mins, const Vector &maxs, unsigned int mask, const CBaseEntity *ignore, int collisionGroup, trace_t *ptr)
{
	Ray_t ray;
	ray.Init(a, b, mins, maxs);
	CTraceFilterSimple filter(PF2_HandleEntity(ignore), collisionGroup);
	UTIL_TraceRay(ray, mask, &filter, ptr);
}

void UTIL_TraceHull(const Vector &a, const Vector &b, const Vector &mins, const Vector &maxs, unsigned int mask, ITraceFilter *pFilter, trace_t *ptr)
{
	Ray_t ray;
	ray.Init(a, b, mins, maxs);
	UTIL_TraceRay(ray, mask, pFilter, ptr);
}

void UTIL_TraceEntity(CBaseEntity *pEntity, const Vector &a, const Vector &b, unsigned int mask, ITraceFilter *pFilter, trace_t *ptr)
{
	UTIL_TraceHull(a, b, pEntity->WorldAlignMins(), pEntity->WorldAlignMaxs(), mask, pFilter, ptr);
}

int UTIL_PointContents(const Vector &vec) { return enginetrace->GetPointContents(vec); }

// ---------------------------------------------------------------------------------------------
// Entity list
// ---------------------------------------------------------------------------------------------
static int NextIndex(CBaseEntity *after) { return after ? after->entindex() + 1 : 0; }

CBaseEntity *CGlobalEntityList::FirstEnt() { return NextEnt(NULL); }

CBaseEntity *CGlobalEntityList::NextEnt(CBaseEntity *cur)
{
	for (int i = NextIndex(cur); i < NUM_ENT_ENTRIES; ++i)
		if (CBaseEntity *e = PF2_EntityByIndex(i))
			return e;
	return NULL;
}

CBaseEntity *CGlobalEntityList::FindEntityByClassname(CBaseEntity *start, const char *name)
{
	size_t n = strlen(name);
	bool wild = n && name[n - 1] == '*';
	for (int i = NextIndex(start); i < NUM_ENT_ENTRIES; ++i)
	{
		const char *cls;
		if (i < MAX_EDICTS)
		{
			edict_t *ed = engine->PEntityOfEntIndex(i);
			if (!ed || ed->IsFree() || !ed->GetUnknown())
				continue;
			cls = ed->GetClassName();
		}
		else
		{
			CBaseEntity *e = PF2_EntityByIndex(i);   // server-only (no edict)
			if (!e)
				continue;
			cls = e->GetClassname();
		}
		bool match = wild ? !V_strnicmp(cls, name, n - 1) : !V_stricmp(cls, name);
		if (match)
			if (CBaseEntity *e = PF2_EntityByIndex(i))
				return e;
	}
	return NULL;
}

CBaseEntity *CGlobalEntityList::FindEntityByName(CBaseEntity *start, const char *name, CBaseEntity *, CBaseEntity *, CBaseEntity *, void *)
{
	if (!name || !*name)
		return NULL;
	for (CBaseEntity *e = NextEnt(start); e; e = NextEnt(e))
		if (e->NameMatches(name))
			return e;
	return NULL;
}

CBaseEntity *CGlobalEntityList::FindEntityByClassnameNearest(const char *name, const Vector &src, float radius)
{
	CBaseEntity *best = NULL;
	float bestSq = radius > 0 ? radius * radius : FLT_MAX;
	for (CBaseEntity *e = FindEntityByClassname(NULL, name); e; e = FindEntityByClassname(e, name))
	{
		float d = (e->GetAbsOrigin() - src).LengthSqr();
		if (d < bestSq) { bestSq = d; best = e; }
	}
	return best;
}

CBaseEntity *CGlobalEntityList::FindEntityInSphere(CBaseEntity *start, const Vector &c, float r)
{
	for (CBaseEntity *e = NextEnt(start); e; e = NextEnt(e))
		if (e->edict() && (e->GetAbsOrigin() - c).IsLengthLessThan(r))
			return e;
	return NULL;
}

int UTIL_EntitiesInBox(CBaseEntity **list, int listMax, const Vector &mins, const Vector &maxs, int flagMask)
{
	int n = 0;
	for (CBaseEntity *e = gEntList.FirstEnt(); e && n < listMax; e = gEntList.NextEnt(e))
	{
		if (flagMask && !(e->GetFlags() & flagMask))
			continue;
		Vector emin, emax;
		e->CollisionProp()->WorldSpaceAABB(&emin, &emax);
		if (IsBoxIntersectingBox(mins, maxs, emin, emax))
			list[n++] = e;
	}
	return n;
}

int UTIL_EntitiesInSphere(CBaseEntity **list, int listMax, const Vector &center, float radius, int flagMask)
{
	int n = 0;
	for (CBaseEntity *e = gEntList.FirstEnt(); e && n < listMax; e = gEntList.NextEnt(e))
	{
		if (flagMask && !(e->GetFlags() & flagMask))
			continue;
		if (e->CollisionProp()->CalcDistanceFromPoint(center) <= radius)
			list[n++] = e;
	}
	return n;
}

// ---------------------------------------------------------------------------------------------
// Players & misc
// ---------------------------------------------------------------------------------------------
CBasePlayer *UTIL_PlayerByIndex(int i)
{
	if (i < 1 || i > gpGlobals->maxClients)
		return NULL;
	edict_t *ed = engine->PEntityOfEntIndex(i);
	if (!ed || ed->IsFree())
		return NULL;
	IPlayerInfo *info = playerinfomanager->GetPlayerInfo(ed);
	if (!info || !info->IsConnected())
		return NULL;
	CBaseEntity *e = PF2_EntityByIndex(i);
	return (e && e->IsPlayer()) ? static_cast<CBasePlayer *>(e) : NULL;
}

CBasePlayer *UTIL_PlayerByUserId(int userID)
{
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		edict_t *ed = engine->PEntityOfEntIndex(i);
		if (ed && !ed->IsFree() && engine->GetPlayerUserId(ed) == userID)
			return UTIL_PlayerByIndex(i);
	}
	return NULL;
}

CBasePlayer *UTIL_PlayerByName(const char *name)
{
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBasePlayer *p = UTIL_PlayerByIndex(i);
		if (p && !V_stricmp(p->GetPlayerName(), name))
			return p;
	}
	return NULL;
}

CBasePlayer *UTIL_GetListenServerHost(void) { return NULL; }
CBasePlayer *UTIL_GetCommandClient(void) { return NULL; }
int UTIL_GetCommandClientIndex(void) { return 0; }
bool UTIL_IsCommandIssuedByServerAdmin(void) { return true; }   // our commands are server-console/rcon only
bool IsDedicatedServer() { return engine->IsDedicatedServer(); }
CBaseEntity *UTIL_EntityByIndex(int i) { return PF2_EntityByIndex(i); }

int UTIL_ClientsInGame(void)
{
	int n = 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
		if (UTIL_PlayerByIndex(i))
			++n;
	return n;
}

bool UTIL_KickBotFromTeam(int kickTeam);   // defined by the bot manager (kicks one of our bots)

void UTIL_ClientPrintAll(int, const char *msg_name, const char *, const char *, const char *, const char *)
{
	Msg("%s\n", msg_name ? msg_name : "");
}

void UTIL_SayTextAll(const char *pText, CBasePlayer *, bool) { Msg("%s\n", pText); }

void UTIL_LogPrintf(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	V_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	engine->LogPrint(buf);
}

char *UTIL_VarArgs(const char *format, ...)
{
	static char string[1024];
	va_list ap;
	va_start(ap, format);
	V_vsnprintf(string, sizeof(string), format, ap);
	va_end(ap);
	return string;
}

void UTIL_Remove(CBaseEntity *) {}   // the bots never delete game entities
void UTIL_SetOrigin(CBaseEntity *entity, const Vector &vecOrigin, bool) { if (entity) entity->SetAbsOrigin(vecOrigin); }

bool UTIL_IsFacingWithinTolerance(CBaseEntity *pViewer, const Vector &vecPosition, float flDotTolerance, float *pflDot)
{
	if (pflDot) *pflDot = 0.0f;
	if (!pViewer) return false;
	Vector forward;
	pViewer->GetVectors(&forward, NULL, NULL);
	Vector dir = vecPosition - pViewer->GetAbsOrigin();
	VectorNormalize(dir);
	float dot = forward.Dot(dir);
	if (pflDot) *pflDot = dot;
	return dot >= flDotTolerance;
}

bool UTIL_IsFacingWithinTolerance(CBaseEntity *pViewer, CBaseEntity *pTarget, float flDotTolerance, float *pflDot)
{
	return pTarget && UTIL_IsFacingWithinTolerance(pViewer, pTarget->GetAbsOrigin(), flDotTolerance, pflDot);
}

float UTIL_VecToYaw(const Vector &vec)
{
	if (vec.y == 0 && vec.x == 0) return 0;
	float yaw = atan2(vec.y, vec.x);
	yaw = RAD2DEG(yaw);
	if (yaw < 0) yaw += 360;
	return yaw;
}

float UTIL_VecToPitch(const Vector &vec)
{
	if (vec.y == 0 && vec.x == 0) return vec.z < 0 ? 180.0f : -180.0f;
	float dist = vec.Length2D();
	return RAD2DEG(atan2(-vec.z, dist));
}

float UTIL_AngleDiff(float destAngle, float srcAngle)
{
	float delta = fmodf(destAngle - srcAngle, 360.0f);
	if (destAngle > srcAngle) { if (delta >= 180) delta -= 360; }
	else { if (delta <= -180) delta += 360; }
	return delta;
}

// NextBot.cpp (NextBotCombatCharacter) is not vendored; its global stop switch lives here
ConVar NextBotStop("nb_stop", "0", FCVAR_CHEAT | FCVAR_REPLICATED, "Stop all NextBots");

// nav globals normally owned by the game DLL
class IP4;
IP4 *p4 = NULL;
IPhysicsSurfaceProps *physprops = NULL;   // set at load from the vphysics factory

// Valve NextBot.cpp trace filter functions (Source SDK 2013)
bool IgnoreActorsTraceFilterFunction(IHandleEntity *pServerEntity, int contentsMask)
{
	CBaseEntity *entity = EntityFromEntityHandle(pServerEntity);
	return entity && entity->MyCombatCharacterPointer() == NULL;   // includes all bots, players, and buildings
}

bool VisionTraceFilterFunction(IHandleEntity *pServerEntity, int contentsMask)
{
	CBaseEntity *entity = EntityFromEntityHandle(pServerEntity);
	return entity && entity->MyCombatCharacterPointer() == NULL && !entity->IsEFlagSet(EFL_DONTBLOCKLOS);
}

// util_shared.cpp: trace results carry our proxies (see PF2_FixupTrace)
bool CGameTrace::DidHitWorld() const
{
	return m_pEnt && ((CBaseEntity *)m_pEnt)->IsWorld();
}

bool CGameTrace::DidHitNonWorldEntity() const
{
	return m_pEnt != NULL && !DidHitWorld();
}

int CGameTrace::GetEntityIndex() const
{
	return m_pEnt ? ((CBaseEntity *)m_pEnt)->entindex() : -1;
}

// tf_gamerules.cpp helper used by the spy bot's teleport-near-victim (MvM) path
bool IsSpaceToSpawnHere(const Vector &where)
{
	Vector bloat(5.0f, 5.0f, 0.0f);
	trace_t result;
	UTIL_TraceHull(where, where, VEC_HULL_MIN - bloat, VEC_HULL_MAX + bloat, MASK_PLAYERSOLID, (const CBaseEntity *)NULL, COLLISION_GROUP_PLAYER_MOVEMENT, &result);
	return result.fraction >= 1.0f;
}

bool PF2_ReadFile(const char *pFileName, const char *pPath, CUtlBuffer &buf)
{
	FileHandle_t fh = filesystem->Open(pFileName, "rb", pPath);
	if (!fh)
		return false;
	unsigned int size = filesystem->Size(fh);
	buf.EnsureCapacity(size + 1);   // allocated by this DLL
	int n = filesystem->Read(buf.Base(), size, fh);
	filesystem->Close(fh);
	if (n < 0)
		return false;
	buf.SeekPut(CUtlBuffer::SEEK_HEAD, n);
	((char *)buf.Base())[n] = 0;
	return true;
}
