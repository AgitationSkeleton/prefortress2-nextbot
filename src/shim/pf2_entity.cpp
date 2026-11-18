// pf2-nextbot game layer: entity proxy registry and CBaseEntity proxy implementation.
#include "cbase.h"
#include "game/vfunc.h"
#include "game/pf2_offsets.h"

using namespace pf2;

// ---------------------------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------------------------
// Indexed by entity-handle entry (0..4095): edict entities live below MAX_EDICTS, server-only
// entities (no edict: info_*, logic_*, filters, ...) above it.
static CBaseEntity *s_proxies[NUM_ENT_ENTRIES];
static CUtlVector<CBaseEntity *> s_graveyard;   // stale proxies kept alive until level shutdown
static void *s_proxyRaw[NUM_ENT_ENTRIES];
static int s_proxySerial[NUM_ENT_ENTRIES];

// Server-only entities are found through IServerTools (the game's full entity list). The table of
// live ones is rebuilt at most once per tick, on demand.
IServerTools *servertools = NULL;
static void *s_aliveRaw[NUM_ENT_ENTRIES];
static int s_aliveSerial[NUM_ENT_ENTRIES];
static int s_aliveTick = -1;

static void RefreshServerOnlyEntities()
{
	if (s_aliveTick == gpGlobals->framecount || !servertools)
		return;
	s_aliveTick = gpGlobals->framecount;
	memset(s_aliveRaw + MAX_EDICTS, 0, sizeof(void *) * (NUM_ENT_ENTRIES - MAX_EDICTS));
	for (CBaseEntity *e = servertools->FirstEntity(); e; e = servertools->NextEntity(e))
	{
		const CBaseHandle &h = VCall<const CBaseHandle &>(e, vt::GetRefEHandle);
		int entry = h.GetEntryIndex();
		if (entry >= MAX_EDICTS && entry < NUM_ENT_ENTRIES)
		{
			s_aliveRaw[entry] = e;
			s_aliveSerial[entry] = h.GetSerialNumber();
		}
	}
}

void PF2_InvalidateEntityCache() { s_aliveTick = -1; }

CBaseEntity *PF2_CreateProxyForClass(const char *classname);   // pf2_tf.cpp factory

static void *RawFromIndex(int index, int *serial)
{
	if (index >= MAX_EDICTS)
	{
		if (index >= NUM_ENT_ENTRIES)
			return NULL;
		RefreshServerOnlyEntities();
		if (serial)
			*serial = s_aliveSerial[index];
		return s_aliveRaw[index];
	}
	edict_t *ed = engine->PEntityOfEntIndex(index);
	if (!ed || ed->IsFree())
		return NULL;
	IServerUnknown *u = ed->GetUnknown();
	if (!u)
		return NULL;
	if (serial)
		*serial = u->GetRefEHandle().GetSerialNumber();
	return u->GetBaseEntity();
}

CBaseEntity *PF2_EntityByIndex(int index)
{
	if (index < 0 || index >= NUM_ENT_ENTRIES)
		return NULL;
	int serial = 0;
	void *raw = RawFromIndex(index, &serial);
	if (!raw)
		return NULL;
	CBaseEntity *p = s_proxies[index];
	if (p && s_proxyRaw[index] == raw && s_proxySerial[index] == serial)
		return p;
	if (p)
		s_graveyard.AddToTail(p);
	p = PF2_CreateProxyForClass(PF2_RawClassname(raw, index));
	p->PF2_Bind(raw, index);
	s_proxies[index] = p;
	s_proxyRaw[index] = raw;
	s_proxySerial[index] = serial;
	return p;
}

void PF2_InstallProxy(int index, CBaseEntity *proxy)
{
	if (index < 0 || index >= NUM_ENT_ENTRIES)
		return;
	int serial = 0;
	void *raw = RawFromIndex(index, &serial);
	if (s_proxies[index] && s_proxies[index] != proxy)
		s_graveyard.AddToTail(s_proxies[index]);
	if (proxy)
		proxy->PF2_Bind(raw, index);
	s_proxies[index] = proxy;
	s_proxyRaw[index] = raw;
	s_proxySerial[index] = serial;
}

void PF2_UninstallProxy(int index, CBaseEntity *proxy)
{
	if (index >= 0 && index < NUM_ENT_ENTRIES && s_proxies[index] == proxy)
	{
		s_proxies[index] = NULL;
		s_proxyRaw[index] = NULL;
	}
}

void PF2_ResetProxies()
{
	s_aliveTick = -1;
	for (int i = 0; i < NUM_ENT_ENTRIES; ++i)
	{
		// bots own their proxy objects (CTFBot); everything else is ours
		if (s_proxies[i] && !s_proxies[i]->MyNextBotPointer())
			delete s_proxies[i];
		s_proxies[i] = NULL;
		s_proxyRaw[i] = NULL;
	}
	s_graveyard.PurgeAndDeleteElements();
}

CBaseEntity *PF2_EntityFromRaw(const void *raw)
{
	if (!raw)
		return NULL;
	// the caller got raw from the engine/game this tick, so it is live; its handle gives the slot
	const CBaseHandle &h = VCall<const CBaseHandle &>(raw, vt::GetRefEHandle);
	if (!h.IsValid())
		return NULL;
	return PF2_EntityByIndex(h.GetEntryIndex());
}

const char *PF2_RawClassname(const void *raw, int index)
{
	if (index < MAX_EDICTS)
	{
		edict_t *ed = engine->PEntityOfEntIndex(index);
		if (ed && !ed->IsFree())
			return ed->GetClassName();
	}
	static PropSlot slot = { 0, -2 };
	int o = CachedFieldOffset(slot, raw, "m_iClassname", false);
	if (o < 0)
		return "";
	string_t s = *(string_t *)((char *)raw + o);
	return STRING(s) ? STRING(s) : "";
}

CBaseEntity *PF2_EntityFromHandleEntity(IHandleEntity *ent)
{
	// for server entities IHandleEntity* is the entity itself (CBaseEntity : IServerEntity : IHandleEntity)
	if (!ent)
		return NULL;
	const CBaseHandle &h = ent->GetRefEHandle();
	if (!h.IsValid())
		return NULL;
	CBaseEntity *p = PF2_EntityByIndex(h.GetEntryIndex());
	return (p && p->GetRefEHandle() == h) ? p : NULL;
}

CBaseEntity *PF2_EntityFromHandle(const CBaseHandle &h)
{
	if (!h.IsValid())
		return NULL;
	CBaseEntity *p = PF2_EntityByIndex(h.GetEntryIndex());
	if (!p)
		return NULL;
	return p->GetRefEHandle().GetSerialNumber() == h.GetSerialNumber() ? p : NULL;
}

CBaseEntity *CBaseEntity::Instance(const edict_t *ed)
{
	return ed ? PF2_EntityByIndex(engine->IndexOfEdict(ed)) : NULL;
}

CBaseEntity *CBaseEntity::Instance(int index) { return PF2_EntityByIndex(index); }

// ---------------------------------------------------------------------------------------------
// CBaseEntity
// ---------------------------------------------------------------------------------------------
CBaseEntity::CBaseEntity() : m_pRaw(NULL), m_index(-1), m_lastKnownArea(NULL)
{
	m_collision.m_owner = this;
}

CBaseEntity::~CBaseEntity() {}

void CBaseEntity::PF2_Bind(void *raw, int index)
{
	m_pRaw = raw;
	m_index = index;
	m_collision.m_owner = this;
	if (raw)
		m_refHandle = ((IHandleEntity *)raw)->GetRefEHandle();
	else
		m_refHandle.Term();
}

void *CBaseEntity::GetRaw() const
{
	if (!m_pRaw || m_index < 0)
		return NULL;
	int serial = 0;
	void *raw = RawFromIndex(m_index, &serial);
	if (raw != m_pRaw || serial != m_refHandle.GetSerialNumber())
		return NULL;
	return m_pRaw;
}

edict_t *CBaseEntity::edict() const { return (m_index >= 0 && m_index < MAX_EDICTS) ? engine->PEntityOfEntIndex(m_index) : NULL; }

IServerNetworkable *CBaseEntity::GetNetworkable() const
{
	edict_t *ed = edict();
	return ed ? ed->GetNetworkable() : NULL;
}

const char *CBaseEntity::GetClassname() const
{
	void *raw = GetRaw();
	return raw ? PF2_RawClassname(raw, m_index) : "";
}

bool CBaseEntity::ClassMatches(const char *name) const
{
	const char *cls = GetClassname();
	size_t n = strlen(name);
	if (n && name[n - 1] == '*')
		return !V_strnicmp(cls, name, n - 1);
	return !V_stricmp(cls, name);
}

string_t CBaseEntity::GetEntityName() const
{
	string_t *p = Field<string_t>("m_iName");
	return p ? *p : NULL_STRING;
}

bool CBaseEntity::NameMatches(const char *name) const
{
	string_t n = GetEntityName();
	return name && STRING(n) && !V_stricmp(STRING(n), name);
}

#define INT_FIELD(fn, name, def) int CBaseEntity::fn() const { int *p = Field<int>(name); return p ? *p : (def); }
INT_FIELD(GetHealth, "m_iHealth", 0)
INT_FIELD(GetFlags, "m_fFlags", 0)
INT_FIELD(GetEFlags, "m_iEFlags", 0)
INT_FIELD(GetEffects, "m_fEffects", 0)
INT_FIELD(GetCollisionGroup, "m_CollisionGroup", 0)
INT_FIELD(GetSpawnFlags, "m_spawnflags", 0)
INT_FIELD(GetModelIndex, "m_nModelIndex", 0)
#undef INT_FIELD

int CBaseEntity::GetTeamNumber() const
{
	if (!m_pRaw)
		return m_pendingTeam;
	int *p = Field<int>("m_iTeamNum");
	return p ? *p : 0;
}

int CBaseEntity::GetTakeDamage() const { char *p = Field<char>("m_takedamage"); return p ? *p : 0; }
int CBaseEntity::GetLifeState() const { char *p = Field<char>("m_lifeState"); return p ? *p : LIFE_DEAD; }
int CBaseEntity::GetWaterLevel() const { char *p = Field<char>("m_nWaterLevel"); return p ? *p : 0; }
bool CBaseEntity::IsEFlagSet(int mask) const { return (GetEFlags() & mask) != 0; }
MoveType_t CBaseEntity::GetMoveType() const { char *p = Field<char>("m_MoveType"); return p ? (MoveType_t)*p : MOVETYPE_NONE; }
float CBaseEntity::GetSimulationTime() const { float *p = Field<float>("m_flSimulationTime"); return p ? *p : 0.0f; }

const char *CBaseEntity::GetModelName() const
{
	string_t *p = Field<string_t>("m_ModelName");
	return p ? STRING(*p) : "";
}

bool CBaseEntity::IsAlive()
{
	void *raw = GetRaw();
	return raw && GetLifeState() == LIFE_ALIVE;
}

int CBaseEntity::GetMaxHealth() const
{
	void *raw = GetRaw();
	return raw ? VCall<int>(raw, vt::GetMaxHealth) : 1;
}

static const Vector s_zero(0, 0, 0);
static const QAngle s_zeroAng(0, 0, 0);

const Vector &CBaseEntity::GetAbsOrigin() const
{
	ICollideable *c = m_collision.Coll();
	return c ? c->GetCollisionOrigin() : s_zero;
}

const QAngle &CBaseEntity::GetAbsAngles() const
{
	ICollideable *c = m_collision.Coll();
	return c ? c->GetCollisionAngles() : s_zeroAng;
}

const Vector &CBaseEntity::GetAbsVelocity() const
{
	Vector *p = Field<Vector>("m_vecAbsVelocity");
	return p ? *p : s_zero;
}

void CBaseEntity::GetVelocity(Vector *vVelocity, AngularImpulse *vAngVelocity)
{
	if (vVelocity) *vVelocity = GetAbsVelocity();
	if (vAngVelocity) vAngVelocity->Init();
}

const Vector &CBaseEntity::WorldSpaceCenter() const
{
	void *raw = GetRaw();
	return raw ? VCall<const Vector &>(raw, vt::WorldSpaceCenter) : s_zero;
}

Vector CBaseEntity::EyePosition() const
{
	void *raw = GetRaw();
	return raw ? VCall<Vector>(raw, vt::EyePosition) : vec3_origin;
}

const QAngle &CBaseEntity::EyeAngles()
{
	void *raw = GetRaw();
	if (!raw)
		return s_zeroAng;
	m_eyeAnglesCache = VCall<const QAngle &>(raw, vt::EyeAngles);
	return m_eyeAnglesCache;
}

CCollisionProperty *CBaseEntity::CollisionProp() const { return &m_collision; }

void CBaseEntity::GetVectors(Vector *forward, Vector *right, Vector *up) const
{
	AngleVectors(GetAbsAngles(), forward, right, up);
}

CBaseEntity *CBaseEntity::HandleField(const char *name) const
{
	CBaseHandle *h = Field<CBaseHandle>(name);
	return h ? PF2_EntityFromHandle(*h) : NULL;
}

CBaseEntity *CBaseEntity::GetGroundEntity() const { return HandleField("m_hGroundEntity"); }
CBaseEntity *CBaseEntity::GetOwnerEntity() const { return HandleField("m_hOwnerEntity"); }
CBaseEntity *CBaseEntity::GetMoveParent() { return HandleField("moveparent"); }

CBaseEntity *CBaseEntity::GetRootMoveParent()
{
	CBaseEntity *e = this;
	for (int i = 0; i < 16; ++i)
	{
		CBaseEntity *p = e->GetMoveParent();
		if (!p)
			break;
		e = p;
	}
	return e;
}

void CBaseEntity::Teleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	void *raw = GetRaw();
	if (raw)
		VCall<void>(raw, vt::Teleport, newPosition, newAngles, newVelocity);
}

int CBaseEntity::TakeDamage(const CTakeDamageInfo &info)
{
	if (IsPlayer() && info.GetAttacker() == this && info.GetDamage() >= GetHealth())
		static_cast<CBasePlayer *>(this)->CommitSuicide();
	return 0;
}

bool CBaseEntity::AcceptInput(const char *, CBaseEntity *, CBaseEntity *, const char *)
{
	return false;   // not needed by the bots; map I/O is left to the game
}

// ---------------------------------------------------------------------------------------------
// CCollisionProperty
// ---------------------------------------------------------------------------------------------
ICollideable *CCollisionProperty::Coll() const
{
	void *raw = m_owner ? m_owner->GetRaw() : NULL;
	return raw ? VCall<ICollideable *>(raw, vt::GetCollideable) : NULL;
}

const Vector &CCollisionProperty::OBBMins() const { ICollideable *c = Coll(); return c ? c->OBBMins() : s_zero; }
const Vector &CCollisionProperty::OBBMaxs() const { ICollideable *c = Coll(); return c ? c->OBBMaxs() : s_zero; }
const Vector &CCollisionProperty::GetCollisionOrigin() const { ICollideable *c = Coll(); return c ? c->GetCollisionOrigin() : s_zero; }
const QAngle &CCollisionProperty::GetCollisionAngles() const { ICollideable *c = Coll(); return c ? c->GetCollisionAngles() : s_zeroAng; }
SolidType_t CCollisionProperty::GetSolid() const { ICollideable *c = Coll(); return c ? c->GetSolid() : SOLID_NONE; }
int CCollisionProperty::GetSolidFlags() const { ICollideable *c = Coll(); return c ? c->GetSolidFlags() : FSOLID_NOT_SOLID; }
bool CCollisionProperty::IsSolid() const { return GetSolid() != SOLID_NONE && !(GetSolidFlags() & FSOLID_NOT_SOLID); }

void CCollisionProperty::CollisionToWorldSpace(const Vector &in, Vector *out) const
{
	ICollideable *c = Coll();
	if (!c) { *out = in; return; }
	VectorTransform(in, c->CollisionToWorldTransform(), *out);
}

const Vector &CCollisionProperty::WorldSpaceCenter() const
{
	CollisionToWorldSpace(OBBCenter(), &m_center);
	return m_center;
}

void CCollisionProperty::WorldSpaceAABB(Vector *mins, Vector *maxs) const
{
	ICollideable *c = Coll();
	if (!c) { *mins = *maxs = s_zero; return; }
	TransformAABB(c->CollisionToWorldTransform(), OBBMins(), OBBMaxs(), *mins, *maxs);
}

void CCollisionProperty::WorldSpaceSurroundingBounds(Vector *mins, Vector *maxs) const
{
	ICollideable *c = Coll();
	if (!c) { *mins = *maxs = s_zero; return; }
	c->WorldSpaceSurroundingBounds(mins, maxs);
}

float CCollisionProperty::BoundingRadius() const { return (OBBMaxs() - OBBMins()).Length() * 0.5f; }

bool CCollisionProperty::IsPointInBounds(const Vector &pt) const
{
	Vector mins, maxs;
	WorldSpaceAABB(&mins, &maxs);
	return pt.x >= mins.x && pt.y >= mins.y && pt.z >= mins.z && pt.x <= maxs.x && pt.y <= maxs.y && pt.z <= maxs.z;
}

void CCollisionProperty::CalcNearestPoint(const Vector &vecWorldPt, Vector *pVecNearestWorldPt) const
{
	Vector mins, maxs;
	WorldSpaceAABB(&mins, &maxs);
	CalcClosestPointOnAABB(mins, maxs, vecWorldPt, *pVecNearestWorldPt);
}

float CCollisionProperty::CalcDistanceFromPoint(const Vector &vecWorldPt) const
{
	Vector p;
	CalcNearestPoint(vecWorldPt, &p);
	return vecWorldPt.DistTo(p);
}

void CBaseEntity::SetMoveType(MoveType_t val, MoveCollide_t)
{
	// used by NextBotPlayerLocomotion to step off ladders, as the game itself does
	if (char *p = Field<char>("m_MoveType"))
		*p = (char)val;
}

IPhysicsObject *CBaseEntity::VPhysicsGetObject() const
{
	IPhysicsObject **p = Field<IPhysicsObject *>("m_pPhysicsObject");
	return p ? *p : NULL;
}

float CBaseEntity::GetModelScale() const
{
	float *p = Field<float>("m_flModelScale");
	return (p && *p > 0.0f) ? *p : 1.0f;
}
