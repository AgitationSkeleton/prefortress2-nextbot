// pf2-nextbot game layer: entity proxies.
//
// Valve's NextBot/TFBot code is written against the game's own entity classes, which only exist
// inside PF2's server.dll. Here each real entity is represented by a long-lived *proxy object*
// (one per entity index, recreated when the index is reused). Proxies read the real entity's
// fields by sendprop/datamap name and call its virtual functions by verified vtable slot.
// For a bot's player slot the proxy object *is* the CTFBot, so CTFBot : NextBotPlayer<CTFPlayer>
// holds exactly as in TF2.
#pragma once
#include "pf2_sdk.h"
// PF2NB: the property accessors below need the __declspec keyword (MSVC; Clang with -fdeclspec).
// tier0/platform.h defines __declspec away on POSIX, so lift that macro for this header only.
#if !defined(_MSC_VER)
#pragma push_macro("__declspec")
#undef __declspec
#endif

class CBaseEntity;
class CBaseCombatCharacter;
class CBasePlayer;
class CBaseCombatWeapon;
class CTFPlayer;
class INextBot;
class CNavArea;
class IServerVehicle;
struct edict_t;

// --------------------------------------------------------------------------------------------
// CHandle
// --------------------------------------------------------------------------------------------
CBaseEntity *PF2_EntityFromHandle(const CBaseHandle &h);

template <class T>
class CHandle : public CBaseHandle
{
public:
	CHandle() {}
	CHandle(int iEntry, int iSerial) { Init(iEntry, iSerial); }
	CHandle(const CBaseHandle &h) : CBaseHandle(h) {}
	CHandle(T *pVal) { Set(pVal); }
	static CHandle<T> FromIndex(int index) { CHandle<T> r; r.m_Index = index; return r; }
	T *Get() const { return (T *)PF2_EntityFromHandle(*this); }
	void Set(const T *pVal);
	operator T *() { return Get(); }
	operator T *() const { return Get(); }
	bool operator!() const { return !Get(); }
	bool operator==(T *val) const { return Get() == val; }
	bool operator!=(T *val) const { return Get() != val; }
	const CBaseHandle &operator=(const T *val) { Set(val); return *this; }
	T *operator->() const { return Get(); }
};
typedef CHandle<CBaseEntity> EHANDLE;

// --------------------------------------------------------------------------------------------
// Collision property facade (engine ICollideable of the real entity)
// --------------------------------------------------------------------------------------------
class CCollisionProperty
{
public:
	ICollideable *Coll() const;
	const Vector &OBBMins() const;
	const Vector &OBBMaxs() const;
	Vector OBBSize() const { return OBBMaxs() - OBBMins(); }
	Vector OBBCenter() const { return (OBBMins() + OBBMaxs()) * 0.5f; }
	const Vector &GetCollisionOrigin() const;
	const QAngle &GetCollisionAngles() const;
	const Vector &WorldSpaceCenter() const;
	void WorldSpaceAABB(Vector *mins, Vector *maxs) const;
	void WorldSpaceSurroundingBounds(Vector *mins, Vector *maxs) const;
	float BoundingRadius() const;
	SolidType_t GetSolid() const;
	int GetSolidFlags() const;
	bool IsSolid() const;
	bool IsPointInBounds(const Vector &pt) const;
	void CalcNearestPoint(const Vector &vecWorldPt, Vector *pVecNearestWorldPt) const;
	float CalcDistanceFromPoint(const Vector &vecWorldPt) const;
	void CollisionToWorldSpace(const Vector &in, Vector *out) const;
	CBaseEntity *m_owner = NULL;
	mutable Vector m_center;
};

// --------------------------------------------------------------------------------------------
class CTeam;
class IPhysicsObject;

// Proxies implement IHandleEntity so Valve code can hand entity pointers to trace filters as it does
// natively; filters resolve raw engine entities and proxies alike through the ref handle.
class CBaseEntity : public IHandleEntity
{
public:
	typedef CBaseEntity ThisClass;
	CBaseEntity();
	virtual ~CBaseEntity();
	void SetRefEHandle(const CBaseHandle &handle) override {}

	// Game entities are allocated zero-filled (CBaseEntity::operator new -> PvAllocEntPrivateData), and
	// Valve's entity/NextBot constructors rely on it (e.g. INextBot's base component pointers).
	static void *operator new(size_t size) { void *p = ::operator new(size); memset(p, 0, size); return p; }
	static void operator delete(void *p) { ::operator delete(p); }

	// ---- proxy plumbing ----
	void PF2_Bind(void *raw, int index);
	void *GetRaw() const;                     // real CBaseEntity*, or NULL if this proxy went stale
	void *GetRawUnchecked() const { return m_pRaw; }
	bool IsProxyValid() const { return GetRaw() != NULL; }
	edict_t *edict() const;
	int entindex() const { return m_index; }
	const CBaseHandle &GetRefEHandle() const override { return m_refHandle; }
	IServerNetworkable *GetNetworkable() const;

	static CBaseEntity *Instance(const edict_t *ed);
	static CBaseEntity *Instance(int index);
	static CBaseEntity *Instance(const CBaseHandle &h) { return PF2_EntityFromHandle(h); }

	// ---- identity / type ----
	const char *GetClassname() const;
	bool ClassMatches(const char *name) const;   // supports a trailing '*' wildcard like the game
	string_t GetEntityName() const;
	bool NameMatches(const char *name) const;
	const char *GetDebugName() { return GetClassname(); }
	bool IsWorld() const { return m_index == 0; }
	virtual bool IsPlayer() const { return false; }
	virtual bool IsBaseObject() const { return false; }
	virtual bool IsBaseCombatWeapon() const { return false; }
	virtual bool IsCombatItem() const { return false; }
	virtual bool IsNPC() const { return false; }
	virtual CBaseCombatCharacter *MyCombatCharacterPointer() { return NULL; }
	virtual INextBot *MyNextBotPointer() { return NULL; }
	virtual CBaseCombatWeapon *MyCombatWeaponPointer() { return NULL; }
	virtual bool IsNetClient() const { return false; }
	virtual int Classify() { return 0; }

	CTeam *GetTeam() const;
	bool IsAIWalkable() { return true; }
	int EntityText(int text_offset, const char *text, float flDuration, int r = 255, int g = 255, int b = 255, int a = 255) { return 0; }
	void SUB_Remove() {}
	void DrawAbsBoxOverlay() {}
	bool ShouldBlockNav() const { return true; }
	bool IsDeflectable() { return false; }   // PF2 has no compression blast
	// TF2 code uses TakeDamage only to kill bots (tf_bot_kill); lethal self-damage on a player becomes
	// the game's own "kill" command, anything else is ignored (an addon applies no damage)
	int TakeDamage(const class CTakeDamageInfo &info);
	ICollideable *GetCollideable() const { return m_collision.Coll(); }
	void (CBaseEntity::*m_pfnThink)() = nullptr;
	virtual void OnNavAreaRemoved(CNavArea *removedArea) {}

	// ---- state ----
	int GetTeamNumber() const;
	void PF2_SetPendingTeam(int team) { m_pendingTeam = team; }   // team of a not-yet-spawned bot (class choice)
	bool InSameTeam(const CBaseEntity *other) const { return other && GetTeamNumber() == other->GetTeamNumber(); }
	virtual bool IsAlive();
	int GetHealth() const;
	int GetMaxHealth() const;
	int GetFlags() const;
	bool IsEFlagSet(int nEFlagMask) const;
	int GetEFlags() const;
	int GetEffects() const;
	bool IsEffectActive(int nEffectMask) const { return (GetEffects() & nEffectMask) != 0; }
	MoveType_t GetMoveType() const;
	int GetCollisionGroup() const;
	SolidType_t GetSolid() const { return CollisionProp()->GetSolid(); }
	bool IsSolid() const { return CollisionProp()->IsSolid(); }
	bool IsSolidFlagSet(int f) const { return (CollisionProp()->GetSolidFlags() & f) != 0; }
	int GetWaterLevel() const;
	bool IsTransparent() const { return false; }
	bool IsMarkedForDeletion() const { return GetRaw() == NULL; }
	bool IsDormant() const { return false; }
	int GetSpawnFlags() const;
	bool HasSpawnFlags(int f) const { return (GetSpawnFlags() & f) != 0; }
	int GetModelIndex() const;
	const char *GetModelName() const;
	float GetSimulationTime() const;

	int GetTakeDamage() const;
	int GetLifeState() const;
	__declspec(property(get = GetTakeDamage)) int m_takedamage;
	__declspec(property(get = GetLifeState)) int m_lifeState;
	__declspec(property(get = GetHealth)) int m_iHealth;

	// ---- spatial ----
	const Vector &GetAbsOrigin() const;
	const Vector &GetLocalOrigin() const { return GetAbsOrigin(); }
	const QAngle &GetAbsAngles() const;
	const QAngle &GetLocalAngles() const { return GetAbsAngles(); }
	const Vector &GetAbsVelocity() const;
	Vector GetSmoothedVelocity() { return GetAbsVelocity(); }
	void GetVelocity(Vector *vVelocity, AngularImpulse *vAngVelocity = NULL);
	const Vector &WorldSpaceCenter() const;
	virtual Vector EyePosition() const;
	virtual const QAngle &EyeAngles();
	virtual const QAngle &LocalEyeAngles() { return EyeAngles(); }
	virtual Vector BodyTarget(const Vector &posSrc, bool bNoisy = true) { return WorldSpaceCenter(); }
	virtual Vector HeadTarget(const Vector &posSrc) { return EyePosition(); }
	const Vector &WorldAlignMins() const { return CollisionProp()->OBBMins(); }
	const Vector &WorldAlignMaxs() const { return CollisionProp()->OBBMaxs(); }
	Vector WorldAlignSize() const { return CollisionProp()->OBBSize(); }
	CCollisionProperty *CollisionProp() const;
	void GetVectors(Vector *forward, Vector *right, Vector *up) const;
	float BoundingRadius() const { return CollisionProp()->BoundingRadius(); }

	// ---- relations ----
	CBaseEntity *GetGroundEntity() const;
	Vector GetViewOffset() const { Vector *p = Field<Vector>("m_vecViewOffset"); return p ? *p : vec3_origin; }
	CBaseEntity *GetOwnerEntity() const;
	CBaseEntity *GetMoveParent();
	CBaseEntity *GetParent() { return GetMoveParent(); }
	CBaseEntity *GetRootMoveParent();
	CBaseEntity *GetFollowedEntity() { return GetMoveParent(); }

	// ---- actions (real side effects on the game entity) ----
	void Teleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity);
	void SetAbsOrigin(const Vector &origin) { Teleport(&origin, NULL, NULL); }
	bool AcceptInput(const char *szInputName, CBaseEntity *pActivator, CBaseEntity *pCaller, const char *value);
	void EmitSound(const char *soundname) {}
	void SetLastKnownArea(CNavArea *area) { m_lastKnownArea = area; }
	void SetMoveType(MoveType_t val, MoveCollide_t moveCollide = MOVECOLLIDE_DEFAULT);
	void SetAbsVelocity(const Vector &vel) { Teleport(NULL, NULL, &vel); }
	IPhysicsObject *VPhysicsGetObject() const;
	float GetModelScale() const;

	// game-side data access helpers for subclasses
	template <typename T> T *Field(const char *name) const;
	template <typename T> T *SendField(const char *name) const;
	CBaseEntity *HandleField(const char *name) const;

	// default-constructed placeholder used by Valve containers (e.g. CUtlVector<CHandle>)
protected:
	void *m_pRaw;
	int m_index;
	CBaseHandle m_refHandle;
	mutable CCollisionProperty m_collision;
	mutable QAngle m_eyeAnglesCache;
	CNavArea *m_lastKnownArea;
	int m_pendingTeam = 0;
	friend class CCollisionProperty;
};

// proxy registry
CBaseEntity *PF2_EntityFromRaw(const void *raw);             // raw CBaseEntity* -> proxy (or NULL)
const char *PF2_RawClassname(const void *raw, int index);
class IServerTools;
extern IServerTools *servertools;
CBaseEntity *PF2_EntityByIndex(int index);
CBaseEntity *PF2_EntityFromHandleEntity(IHandleEntity *ent); // engine IHandleEntity -> proxy
void PF2_InstallProxy(int index, CBaseEntity *proxy);        // e.g. a CTFBot for its player slot
void PF2_ResetProxies();                                     // level shutdown

template <class T>
inline void CHandle<T>::Set(const T *pVal)
{
	if (pVal)
		*(CBaseHandle *)this = pVal->GetRefEHandle();
	else
		Term();
}

#include "pf2_props.h"
template <typename T> inline T *CBaseEntity::Field(const char *name) const
{
	void *raw = GetRaw();
	if (!raw) return NULL;
	int o = pf2::FieldOffset(raw, name);
	return o < 0 ? NULL : (T *)((char *)raw + o);
}
template <typename T> inline T *CBaseEntity::SendField(const char *name) const
{
	void *raw = GetRaw();
	if (!raw) return NULL;
	int o = pf2::SendPropOffset(raw, name);
	return o < 0 ? NULL : (T *)((char *)raw + o);
}

inline CBaseEntity *GetContainingEntity(edict_t *pent) { return CBaseEntity::Instance(pent); }
inline int ENTINDEX(CBaseEntity *pEnt) { return pEnt ? pEnt->entindex() : 0; }
inline int ENTINDEX(edict_t *ed) { extern IVEngineServer *engine; return ed ? engine->IndexOfEdict(ed) : 0; }
inline edict_t *INDEXENT(int i) { extern IVEngineServer *engine; return engine->PEntityOfEntIndex(i); }
inline bool FClassnameIs(CBaseEntity *pEntity, const char *szClassname) { return pEntity && pEntity->ClassMatches(szClassname); }
inline CBaseEntity *EntityFromEntityHandle(IHandleEntity *pHandleEntity) { return PF2_EntityFromHandleEntity(pHandleEntity); }
inline const CBaseEntity *EntityFromEntityHandle(const IHandleEntity *pHandleEntity) { return PF2_EntityFromHandleEntity((IHandleEntity *)pHandleEntity); }

#if !defined(_MSC_VER)
#pragma pop_macro("__declspec")
#endif
