// pf2-nextbot game layer: TF/PF2 proxy implementations.
#include "cbase.h"
#include "tf_nav_mesh.h"
#include "game/vfunc.h"
#include "game/pf2_offsets.h"
#include <dt_send.h>
#include <server_class.h>

using namespace pf2;

namespace pf2vt { enum { GetWeaponID = pf2::vt::Wpn_GetWeaponID, Weapon_GetSlot2 = pf2::vt::Wpn_GetSlot /* CBaseCombatWeapon::GetSlot */ }; }

// ---------------------------------------------------------------------------------------------
// Sendtable array helper: offset of element i of a networked array (handles int/bool strides)
// ---------------------------------------------------------------------------------------------
static SendProp *FindSendPropDeep(SendTable *t, const char *name, int base, int *outOff)
{
	for (int i = 0; i < t->GetNumProps(); ++i)
	{
		SendProp *p = t->GetProp(i);
		if (p->GetName() && !V_strcmp(p->GetName(), name))
		{
			*outOff = base + p->GetOffset();
			return p;
		}
		if (p->GetType() == DPT_DataTable && p->GetDataTable())
			if (SendProp *r = FindSendPropDeep(p->GetDataTable(), name, base + p->GetOffset(), outOff))
				return r;
	}
	return NULL;
}

static int SendArrayElementOffset(const void *raw, const char *name, int index)
{
	if (!raw)
		return -1;
	ServerClass *sc = VCall<ServerClass *>(raw, vt::GetServerClass);
	int off = 0;
	SendProp *p = sc ? FindSendPropDeep(sc->m_pTable, name, 0, &off) : NULL;
	if (!p)
		return -1;
	if (p->GetType() == DPT_DataTable && p->GetDataTable())
	{
		SendTable *t = p->GetDataTable();
		if (index < 0 || index >= t->GetNumProps())
			return -1;
		return off + t->GetProp(index)->GetOffset();
	}
	return off;   // not an array table
}

template <typename T>
static T SendArrayGet(const CBaseEntity *e, const char *name, int index, T def)
{
	void *raw = e ? e->GetRaw() : NULL;
	int o = SendArrayElementOffset(raw, name, index);
	return o < 0 ? def : *(T *)((char *)raw + o);
}

// ---------------------------------------------------------------------------------------------
// Proxy factory
// ---------------------------------------------------------------------------------------------
static bool StartsWith(const char *s, const char *p) { return !V_strnicmp(s, p, strlen(p)); }
static bool EndsWith(const char *s, const char *p) { size_t a = strlen(s), b = strlen(p); return a >= b && !V_stricmp(s + a - b, p); }

class CTFWeaponProxy : public CTFWeaponBase {};

CBaseEntity *PF2_CreateProxyForClass(const char *cls)
{
	if (!cls) cls = "";
	if (!V_strcmp(cls, "player")) return new CTFPlayer;
	if (StartsWith(cls, "tf_projectile_pipe") || !V_strcmp(cls, "tf_weapon_grenade_pipebomb_projectile")) return new CTFGrenadePipebombProjectile;
	if (!V_strcmp(cls, "info_player_teamspawn")) return new CTFTeamSpawn;
	if (!V_strcmp(cls, "trigger_multiple")) return new CTriggerMultiple;
	if (StartsWith(cls, "tf_weapon_") && !EndsWith(cls, "_projectile") && !StartsWith(cls, "tf_weapon_grenade_mirv_bomb"))
	{
		if (!V_strcmp(cls, "tf_weapon_medigun")) return new CWeaponMedigun;
		if (!V_strcmp(cls, "tf_weapon_sniperrifle")) return new CTFSniperRifle;
		if (!V_strcmp(cls, "tf_weapon_pipebomblauncher")) return new CTFPipebombLauncher;
		if (!V_strcmp(cls, "tf_weapon_flamethrower")) return new CTFFlameThrower;
		if (!V_strcmp(cls, "tf_weapon_builder")) return new CTFWeaponBuilder;
		if (StartsWith(cls, "tf_weapon_grenade_")) return new CTFWeaponBaseGrenade;
		return new CTFWeaponProxy;
	}
	if (!V_strcmp(cls, "obj_sentrygun")) return new CObjectSentrygun;
	if (!V_strcmp(cls, "obj_dispenser")) return new CObjectDispenser;
	if (StartsWith(cls, "obj_teleporter")) return new CObjectTeleporter;
	if (StartsWith(cls, "obj_attachment_sapper")) return new CObjectSapper;
	if (StartsWith(cls, "obj_")) return new CBaseObject;
	if (!V_strcmp(cls, "item_teamflag")) return new CCaptureFlag;
	if (!V_strcmp(cls, "func_capturezone")) return new CCaptureZone;
	if (!V_strcmp(cls, "func_respawnroom")) return new CFuncRespawnRoom;
	if (!V_strcmp(cls, "team_control_point")) return new CTeamControlPoint;
	if (!V_strcmp(cls, "team_control_point_master")) return new CTeamControlPointMaster;
	if (!V_strcmp(cls, "trigger_capture_area")) return new CTriggerAreaCapture;
	if (!V_strcmp(cls, "team_train_watcher")) return new CTeamTrainWatcher;
	if (!V_strcmp(cls, "tf_team")) return new CTFTeam;
	if (!V_strcmp(cls, "team_round_timer")) return new CTeamRoundTimer;
	if (!V_strcmp(cls, "func_brush")) return new CFuncBrush;
	if (!V_strcmp(cls, "worldspawn")) return new CWorld;
	if (StartsWith(cls, "func_door")) return new CBaseDoor;
	if (StartsWith(cls, "prop_door")) return new CBasePropDoor;
	if (StartsWith(cls, "prop_physics")) return new CPhysicsProp;
	return new CBaseEntity;
}

// ---------------------------------------------------------------------------------------------
// Class data (scripts/playerclasses/*.txt through the game filesystem, so VPK content works)
// ---------------------------------------------------------------------------------------------
static PF2ClassData s_classData[TF_CLASS_COUNT_ALL];
static bool s_classDataLoaded = false;
static const char *s_classNames[TF_CLASS_COUNT_ALL] = { "undefined", "scout", "sniper", "soldier", "demoman", "medic", "heavyweapons", "pyro", "spy", "engineer", "civilian" };
static const char *s_ammoNames[TF_AMMO_COUNT] = { "DUMMY AMMO", "TF_AMMO_PRIMARY", "TF_AMMO_SECONDARY", "TF_AMMO_METAL", "TF_AMMO_GRENADES1", "TF_AMMO_GRENADES2" };
struct WeaponName { const char *name; int id; };
static const WeaponName s_weaponNames[] = {
#include "pf2_weaponnames.inc"
};

static int WeaponIdFromName(const char *n)
{
	if (!n || !*n) return 0;
	for (const WeaponName &w : s_weaponNames)
		if (!V_stricmp(w.name, n))
			return w.id;
	return 0;
}

int PF2_ClassFromName(const char *name)
{
	if (!name) return 0;
	for (int i = 1; i < TF_CLASS_COUNT_ALL; ++i)
		if (!V_stricmp(name, s_classNames[i]))
			return i;
	if (!V_stricmp(name, "heavy")) return TF_CLASS_HEAVYWEAPONS;
	if (!V_stricmp(name, "demo")) return TF_CLASS_DEMOMAN;
	if (!V_stricmp(name, "engie")) return TF_CLASS_ENGINEER;
	return 0;
}

const char *PF2_ClassName(int c) { return (c >= 0 && c < TF_CLASS_COUNT_ALL) ? s_classNames[c] : "undefined"; }

void PF2_LoadClassData()
{
	for (int c = 1; c < TF_CLASS_COUNT_ALL; ++c)
	{
		PF2ClassData &d = s_classData[c];
		memset(&d, 0, sizeof(d));
		V_strncpy(d.name, s_classNames[c], sizeof(d.name));
		d.maxHealth = 100; d.maxSpeed = 300.0f;
		KeyValues *kv = new KeyValues("PlayerClass");
		char path[128];
		V_snprintf(path, sizeof(path), "scripts/playerclasses/%s.txt", s_classNames[c]);
		if (kv->LoadFromFile(filesystem, path, "GAME"))
		{
			d.maxHealth = kv->GetInt("health_max", 100);
			d.maxArmor = kv->GetInt("armor_max", 0);
			d.maxSpeed = kv->GetFloat("speed_max", 300.0f);
			for (int i = 0; i < 6; ++i)
			{
				char key[16];
				V_snprintf(key, sizeof(key), "weapon%d", i + 1);
				d.weapon[i] = WeaponIdFromName(kv->GetString(key));
			}
			for (int g = 0; g < 2; ++g)
			{
				const char *gn = kv->GetString(g ? "grenade2" : "grenade1");
				V_strncpy(d.grenadeName[g], gn, sizeof(d.grenadeName[g]));
				d.grenade[g] = WeaponIdFromName(gn);
			}
			if (KeyValues *ammo = kv->FindKey("AmmoMax"))
				for (int a = 1; a < TF_AMMO_COUNT; ++a)
					d.maxAmmo[a] = ammo->GetInt(s_ammoNames[a], 0);
		}
		else
			Warning("[PF2NB] could not read %s\n", path);
		kv->deleteThis();
	}
	s_classDataLoaded = true;
}

TFPlayerClassData_t *GetPlayerClassData(int iClass)
{
	static TFPlayerClassData_t data[TF_CLASS_COUNT_ALL];
	if (iClass < 0 || iClass >= TF_CLASS_COUNT_ALL)
		return NULL;
	TFPlayerClassData_t &d = data[iClass];
	V_strncpy(d.m_szClassName, s_classNames[iClass], sizeof(d.m_szClassName));
	if (const PF2ClassData *c = PF2_GetClassData(iClass))
	{
		d.m_flMaxSpeed = c->maxSpeed;
		d.m_nMaxHealth = c->maxHealth;
		d.m_nMaxArmor = c->maxArmor;
	}
	return &d;
}

int GetClassIndexFromString(const char *pClassName, int nLastClassIndex)
{
	int c = PF2_ClassFromName(pClassName);
	return (c && c <= nLastClassIndex) ? c : TF_CLASS_UNDEFINED;
}

static PF2NullLocalize s_nullLocalize;
PF2NullLocalize *g_pVGuiLocalize = &s_nullLocalize;

const PF2ClassData *PF2_GetClassData(int c)
{
	if (!s_classDataLoaded)
		PF2_LoadClassData();
	return (c > 0 && c < TF_CLASS_COUNT_ALL) ? &s_classData[c] : NULL;
}

// ---------------------------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------------------------
int CTFWeaponBase::GetWeaponID() const
{
	void *raw = GetRaw();
	return raw ? VCall<int>(raw, pf2vt::GetWeaponID) : TF_WEAPON_NONE;
}

int CTFWeaponBase::GetSlot() const
{
	void *raw = GetRaw();
	return raw ? VCall<int>(raw, pf2vt::Weapon_GetSlot2) : 0;
}

bool CTFWeaponBase::IsMeleeWeapon() const
{
	switch (GetWeaponID())
	{
	case TF_WEAPON_BAT: case TF_WEAPON_BOTTLE: case TF_WEAPON_FIREAXE: case TF_WEAPON_CLUB: case TF_WEAPON_CROWBAR:
	case TF_WEAPON_KNIFE: case TF_WEAPON_FISTS: case TF_WEAPON_SHOVEL: case TF_WEAPON_WRENCH: case TF_WEAPON_BONESAW:
	case TF_WEAPON_BRANDINGIRON: case TF_WEAPON_MACHETE: case TF_WEAPON_MEDIKIT: case TF_WEAPON_PIPE:
	case TF_WEAPON_SYRINGE: case TF_WEAPON_UMBRELLA:	// PF2: medic's melee syringe, civilian's umbrella
		return true;
	}
	return GetSlot() == 2;
}

bool CTFWeaponBase::IsGrenade() const
{
	int id = GetWeaponID();
	return id >= TF_WEAPON_GRENADE_NORMAL && id <= TF_WEAPON_GRENADE_HEAL;
}

float CTFWeaponBase::GetProjectileSpeed()
{
	switch (GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER: return 1100.0f;
	case TF_WEAPON_GRENADELAUNCHER: return 1200.0f;
	case TF_WEAPON_PIPEBOMBLAUNCHER: return 900.0f;
	case TF_WEAPON_SYRINGEGUN_MEDIC: return 1000.0f;
	case TF_WEAPON_NAILGUN: return 1200.0f;			// PF2 scripts: projectile_nail 1200
	case TF_WEAPON_FLAMETHROWER_ROCKET: return 850.0f;	// pyro alt-fire (Secondary_ProjectileSpeed)
	case TF_WEAPON_FLAMETHROWER: return 850.0f;		// (only used when aiming the alt-fire rocket)
	case TF_WEAPON_TRANQ: return 3000.0f;			// projectile_dart 3000
	}
	return 0.0f;
}

const CTFWeaponInfo &CTFWeaponBase::GetTFWpnData() const
{
	m_info.m_WeaponData[0].m_flProjectileSpeed = const_cast<CTFWeaponBase *>(this)->GetProjectileSpeed();
	return m_info;
}

int CTFWeaponBase::GetMaxClip1() const { int *p = Field<int>("m_iMaxClip1"); return p ? *p : -1; }
CTFPlayer *CTFWeaponBase::GetTFPlayerOwner() const { return ToTFPlayer(GetOwner()); }

bool CTFWeaponBase::IsZoomed() const
{
	CTFPlayer *o = GetTFPlayerOwner();
	return o && o->m_Shared.InCond(TF_COND_ZOOMED);
}

float CTFWeaponBase::GetTimeSinceLastFired() const
{
	float *p = Field<float>("m_flLastFireTime");
	return p && *p > 0 ? gpGlobals->curtime - *p : 99999.9f;
}

// ---------------------------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------------------------
CTFPlayer::CTFPlayer()
{
	m_Shared.m_pOuter = this;
	m_PlayerClass.m_pOuter = this;
}

int CTFPlayerClass::GetClassIndex() const
{
	int *p = m_pOuter ? m_pOuter->Field<int>("m_iClass") : NULL;
	return p ? *p : TF_CLASS_UNDEFINED;
}

bool CTFPlayerShared::InCond(int nCond) const
{
	if (nCond < 0 || nCond >= 32 || !m_pOuter)
		return false;
	int *p = m_pOuter->SendField<int>("m_nPlayerCond");
	return p && (*p & (1 << nCond)) != 0;
}

bool CTFPlayerShared::IsCarryingObject() const { bool *p = m_pOuter ? m_pOuter->Field<bool>("m_bCarryingObject") : NULL; return p && *p; }
int CTFPlayerShared::GetDisguiseTeam() const { int *p = m_pOuter ? m_pOuter->Field<int>("m_nDisguiseTeam") : NULL; return p ? *p : TEAM_UNASSIGNED; }
int CTFPlayerShared::GetDisguiseClass() const { int *p = m_pOuter ? m_pOuter->Field<int>("m_nDisguiseClass") : NULL; return p ? *p : 0; }
int CTFPlayerShared::GetNumHealers() const { int *p = m_pOuter ? m_pOuter->Field<int>("m_nNumHealers") : NULL; return p ? *p : 0; }
int CTFPlayerShared::GetMaxBuffedHealth() const { return m_pOuter ? (int)(m_pOuter->GetMaxHealth() * 1.5f) : 150; }
float CTFPlayerShared::GetSpyCloakMeter() const { float *p = m_pOuter ? m_pOuter->Field<float>("m_flCloakMeter") : NULL; return p ? *p : 0.0f; }
CTFWeaponBase *CTFPlayerShared::GetActiveTFWeapon() const { return m_pOuter ? m_pOuter->GetActiveTFWeapon() : NULL; }

float CTFPlayerShared::GetPercentInvisible() const
{
	if (!InCond(TF_COND_STEALTHED))
		return 0.0f;
	float *done = m_pOuter->Field<float>("m_flInvisChangeCompleteTime");
	if (!done || gpGlobals->curtime >= *done)
		return 1.0f;
	return clamp(1.0f - (*done - gpGlobals->curtime), 0.0f, 1.0f);
}

void CTFPlayerShared::Disguise(int nTeam, int nClass, CTFPlayer *, bool)
{
	if (m_pOuter)
		m_pOuter->ClientCommand(UTIL_VarArgs("disguise %d %d", nClass, nTeam == TF_TEAM_BLUE ? 1 : 2));	// PF2's disguise command: 1 = BLU, anything else = RED
}

int CTFPlayer::GetDesiredPlayerClassIndex() const { int *p = Field<int>("m_iDesiredPlayerClass"); return p ? *p : TF_CLASS_UNDEFINED; }

CTFWeaponBase *CTFPlayer::Weapon_OwnsThisID(int id) const
{
	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CTFWeaponBase *w = ToTFWeaponBase(GetWeapon(i));
		if (w && w->GetWeaponID() == id)
			return w;
	}
	return NULL;
}

CTFWeaponBase *CTFPlayer::Weapon_GetWeaponByType(int iType)
{
	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CTFWeaponBase *w = ToTFWeaponBase(GetWeapon(i));
		if (!w)
			continue;
		int slot = w->GetSlot();
		if ((iType == TF_WPN_TYPE_PRIMARY && slot == 0) || (iType == TF_WPN_TYPE_SECONDARY && slot == 1) ||
			(iType == TF_WPN_TYPE_MELEE && slot == 2) || (iType == TF_WPN_TYPE_PDA && slot == 3) ||
			(iType == TF_WPN_TYPE_BUILDING && w->ClassMatches("tf_weapon_builder")))
			return w;
	}
	return NULL;
}

bool CTFPlayer::HasTheFlag() const
{
	CBaseEntity *item = GetItem();
	return item && item->ClassMatches("item_teamflag");
}

int CTFPlayer::GetMaxAmmo(int iAmmoIndex, int iClassNumber) const
{
	const PF2ClassData *d = PF2_GetClassData(iClassNumber > 0 ? iClassNumber : m_PlayerClass.GetClassIndex());
	return (d && iAmmoIndex >= 0 && iAmmoIndex < TF_AMMO_COUNT) ? d->maxAmmo[iAmmoIndex] : 0;
}

float CTFPlayer::GetNextThrowTime() const { float *p = Field<float>("m_flNextThrowTime"); return p ? *p : 0.0f; }
bool CTFPlayer::IsGrenadePrimed() const { int *p = Field<int>("m_iPrimed"); return p && *p != 0; }

Vector CTFPlayer::GetClassEyeHeight()
{
	Vector *v = Field<Vector>("m_vecViewOffset");
	return Vector(0, 0, v ? v->z : 68.0f);
}

void CTFPlayer::StartBuildingObjectOfType(int iType, int iMode)
{
	// shipped PF2 uses TF2's scheme: "build <type> <mode>" (teleporter mode 0 = entrance, 1 = exit)
	ClientCommand(UTIL_VarArgs("build %d %d", iType, iMode));
}

// ---- projectile impact estimation: Valve CTFPlayer code (Source SDK 2013, tf_player.cpp) ----
Vector CTFPlayer::EstimateProjectileImpactPosition(CTFWeaponBaseGun *weapon)
{
	if (!weapon)
		return GetAbsOrigin();
	const QAngle &angles = EyeAngles();
	float initVel = weapon->IsWeapon(TF_WEAPON_PIPEBOMBLAUNCHER) ? 900.0f : weapon->GetProjectileSpeed();
	return EstimateProjectileImpactPosition(angles.x, angles.y, initVel);
}

Vector CTFPlayer::EstimateStickybombProjectileImpactPosition(float pitch, float yaw, float charge)
{
	float initVel = charge * (TF_PIPEBOMB_MAX_CHARGE_VEL - TF_PIPEBOMB_MIN_CHARGE_VEL) + TF_PIPEBOMB_MIN_CHARGE_VEL;
	return EstimateProjectileImpactPosition(pitch, yaw, initVel);
}

Vector CTFPlayer::EstimateProjectileImpactPosition(float pitch, float yaw, float initVel)
{
	Vector vecForward, vecRight, vecUp;
	QAngle angles(pitch, yaw, 0.0f);
	AngleVectors(angles, &vecForward, &vecRight, &vecUp);
	float fRight = 8.f;
	Vector vecSrc = Weapon_ShootPosition();
	vecSrc += vecForward * 16.0f + vecRight * fRight + vecUp * -6.0f;
	const float initVelScale = 0.9f;
	Vector vecVelocity = initVelScale * ((vecForward * initVel) + (vecUp * 200.0f));
	const float timeStep = 0.01f;
	const float maxTime = 5.0f;
	Vector pos = vecSrc;
	Vector lastPos = pos;
	static ConVarRef sv_gravity("sv_gravity");
	const float g = sv_gravity.IsValid() ? sv_gravity.GetFloat() : 800.0f;
	Vector alongDir = vecForward;
	alongDir.z = 0.0f;
	alongDir.NormalizeInPlace();
	float alongVel = FastSqrt(vecVelocity.x * vecVelocity.x + vecVelocity.y * vecVelocity.y);
	trace_t trace;
	trace.endpos = vec3_origin;
	CTraceFilterNoCombatCharacters traceFilter(NULL, COLLISION_GROUP_NONE);
	for (float t = 0.0f; t < maxTime; t += timeStep)
	{
		float along = alongVel * t;
		float height = vecVelocity.z * t - 0.5f * g * t * t;
		pos.x = vecSrc.x + alongDir.x * along;
		pos.y = vecSrc.y + alongDir.y * along;
		pos.z = vecSrc.z + height;
		UTIL_TraceHull(lastPos, pos, -Vector(8, 8, 8), Vector(8, 8, 8), MASK_SOLID_BRUSHONLY, &traceFilter, &trace);
		if (trace.DidHit())
			break;
		lastPos = pos;
	}
	return trace.endpos;
}

CUtlVector<CHandle<CTFGrenadePipebombProjectile> > &CTFPipebombLauncher::GetPipeBombVector()
{
	static CUtlVector<CHandle<CTFGrenadePipebombProjectile> > v;
	v.RemoveAll();
	CBaseEntity *owner = GetOwner();
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "tf_projectile_pipe_remote"); e; e = gEntList.FindEntityByClassname(e, "tf_projectile_pipe_remote"))
		if (e->HandleField("m_hThrower") == owner || e->GetOwnerEntity() == owner)
			v.AddToTail(static_cast<CTFGrenadePipebombProjectile *>(e));
	return v;
}

// m_hObjectBeingBuilt is neither networked nor in the datamap in PF2; the blueprint is the object
// this engineer owns that is still being placed.
CBaseEntity *CTFWeaponBuilder::GetPlacementObject() const
{
	CBaseEntity *owner = GetOwner();
	if (!owner)
		return NULL;
	for (int i = 0; i < IBaseObjectAutoList::AutoList().Count(); ++i)
	{
		CBaseObject *o = static_cast<CBaseObject *>(IBaseObjectAutoList::AutoList()[i]);
		if (o->GetBuilder() == owner && o->IsPlacing())
			return o;
	}
	return NULL;
}

// PF2's CTFWeaponBuilder::IsValidPlacement is the blueprint's UpdatePlacement(), whose result is the
// virtual CBaseObject::IsPlacementPosValid() (computes the build spot from the owner's aim).
// PF2's own PrimaryAttack re-validates and moves the blueprint when the bot presses fire.
bool CTFWeaponBuilder::IsValidPlacement() const
{
	CBaseEntity *obj = GetPlacementObject();
	void *raw = obj ? obj->GetRaw() : NULL;
	return raw && VCall<bool>(raw, vt::Obj_IsPlacementPosValid);
}

void CBaseObject::DetonateObject()
{
	CTFPlayer *b = GetBuilder();
	if (b)
		b->ClientCommand(UTIL_VarArgs("destroy %d %d", GetType(), GetObjectMode()));
}

#define PF2_AUTOLIST(Iface, Proxy, cls) 	const CUtlVector<Iface *> &Iface::AutoList() 	{ 		static CUtlVector<Iface *> list; static int tick = -1; 		if (tick != gpGlobals->framecount) { tick = gpGlobals->framecount; list.RemoveAll(); 			for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, cls); e; e = gEntList.FindEntityByClassname(e, cls)) 				if (Proxy *p = dynamic_cast<Proxy *>(e)) list.AddToTail(p); } 		return list; 	}
bool PointInRespawnRoom(const CBaseEntity *pTarget, const Vector &vecOrigin, bool bTouching_SameTeamOnly)
{
	const IFuncRespawnRoomAutoList *dummy = NULL; (void)dummy;
	for (int i = 0; i < IFuncRespawnRoomAutoList::AutoList().Count(); ++i)
	{
		CFuncRespawnRoom *room = static_cast<CFuncRespawnRoom *>(IFuncRespawnRoomAutoList::AutoList()[i]);
		if (!room->GetActive() || room->GetDisabled())
			continue;
		if (bTouching_SameTeamOnly && pTarget && room->GetTeamNumber() != pTarget->GetTeamNumber())
			continue;
		if (room->PointIsWithin(vecOrigin))
			return true;
	}
	return false;
}

PF2_AUTOLIST(ICaptureZoneAutoList, CCaptureZone, "func_capturezone")
PF2_AUTOLIST(ICaptureFlagAutoList, CCaptureFlag, "item_teamflag")
PF2_AUTOLIST(IFuncRespawnRoomAutoList, CFuncRespawnRoom, "func_respawnroom")
PF2_AUTOLIST(ITFTeamSpawnAutoList, CTFTeamSpawn, "info_player_teamspawn")
bool g_fGameOver = false;
class CPopulationManager;
CPopulationManager *g_pPopulationManager = NULL;

CBaseEntity *CTFPlayer::MedicGetHealTarget()
{
	CWeaponMedigun *m = dynamic_cast<CWeaponMedigun *>(GetActiveTFWeapon());
	return m ? m->GetHealTarget() : NULL;
}

float CTFPlayer::MedicGetChargeLevel(CTFWeaponBase **pRetMedigun)
{
	CWeaponMedigun *m = dynamic_cast<CWeaponMedigun *>(Weapon_OwnsThisID(TF_WEAPON_MEDIGUN));
	if (pRetMedigun) *pRetMedigun = m;
	return m ? m->GetChargeLevel() : 0.0f;
}

bool CTFPlayer::IsThreatAimingTowardMe(CBaseEntity *threat, float cosTolerance) const
{
	CTFPlayer *p = ToTFPlayer(threat);
	if (!p)
		return false;
	Vector to = const_cast<CTFPlayer *>(this)->WorldSpaceCenter() - p->EyePosition();
	to.NormalizeInPlace();
	Vector fwd;
	p->EyeVectors(&fwd);
	return DotProduct(to, fwd) > cosTolerance;
}

CTriggerAreaCapture *CTFPlayer::GetControlPointStandingOn()
{
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "trigger_capture_area"); e; e = gEntList.FindEntityByClassname(e, "trigger_capture_area"))
		if (e->CollisionProp()->IsPointInBounds(WorldSpaceCenter()))
			return static_cast<CTriggerAreaCapture *>(e);
	return NULL;
}

bool CTFPlayer::IsCapturingPoint()
{
	CTriggerAreaCapture *t = GetControlPointStandingOn();
	CTeamControlPoint *p = t ? t->GetControlPoint() : NULL;
	return p && p->GetOwner() != GetTeamNumber();
}

void CCaptureFlag::AddFollower(CTFBot *bot)
{
	CTFPlayer *p = ToTFPlayer((CBaseEntity *)bot);
	if (p && m_followers.Find(p) == m_followers.InvalidIndex())
		m_followers.AddToTail(p);
}

void CCaptureFlag::RemoveFollower(CTFBot *bot)
{
	m_followers.FindAndRemove(ToTFPlayer((CBaseEntity *)bot));
}

static CTFWeaponInfo s_weaponInfo[TF_WEAPON_COUNT + 1];
WEAPON_FILE_INFO_HANDLE LookupWeaponInfoSlot(const char *name) { int id = WeaponIdFromName(name); return id > 0 ? (WEAPON_FILE_INFO_HANDLE)id : GetInvalidWeaponInfoHandle(); }
WEAPON_FILE_INFO_HANDLE GetInvalidWeaponInfoHandle(void) { return (WEAPON_FILE_INFO_HANDLE)0xFFFF; }
FileWeaponInfo_t *GetFileWeaponInfoFromHandle(WEAPON_FILE_INFO_HANDLE h)
{
	if (h == 0 || h > TF_WEAPON_COUNT)
		return NULL;
	CTFWeaponInfo &w = s_weaponInfo[h];
	// weapons that never use ammo: medigun, PDAs, builder, invisibility watch
	bool noAmmo = h == TF_WEAPON_MEDIGUN || h == TF_WEAPON_PDA || h == TF_WEAPON_PDA_ENGINEER_BUILD || h == TF_WEAPON_PDA_ENGINEER_DESTROY ||
		h == TF_WEAPON_PDA_SPY || h == TF_WEAPON_BUILDER || h == TF_WEAPON_INVIS || h == TF_WEAPON_PDA_DEMO;
	w.m_WeaponData[0].m_iProjectile = noAmmo ? TF_PROJECTILE_NONE : TF_PROJECTILE_BULLET;
	return &w;
}
const char *WeaponIdToAlias(int iWeapon)
{
	for (const WeaponName &w : s_weaponNames)
		if (w.id == iWeapon)
			return w.name;
	return NULL;
}

CBaseObject *CTFPlayer::GetObjectOfType(int iType, int iMode) const
{
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "obj_*"); e; e = gEntList.FindEntityByClassname(e, "obj_*"))
	{
		CBaseObject *o = static_cast<CBaseObject *>(e);
		// a blueprint still being placed is not a built object (TFBot's build behaviors would think they are
		// done and walk off, leaving the blueprint at the world origin)
		if (e->IsBaseObject() && o->GetBuilder() == this && o->GetType() == iType && o->GetObjectMode() == iMode && !o->IsPlacing())
			return o;
	}
	return NULL;
}

int CTFPlayer::GetObjectCount() const
{
	int n = 0;
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "obj_*"); e; e = gEntList.FindEntityByClassname(e, "obj_*"))
		if (e->IsBaseObject() && static_cast<CBaseObject *>(e)->GetBuilder() == this)
			++n;
	return n;
}

CBaseObject *CTFPlayer::GetObject(int i) const
{
	int n = 0;
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "obj_*"); e; e = gEntList.FindEntityByClassname(e, "obj_*"))
		if (e->IsBaseObject() && static_cast<CBaseObject *>(e)->GetBuilder() == this)
			if (n++ == i)
				return static_cast<CBaseObject *>(e);
	return NULL;
}

CTFTeam *CTFPlayer::GetTFTeam() const { return GetGlobalTFTeam(GetTeamNumber()); }
CTFTeam *CTFPlayer::GetOpposingTFTeam() const { return GetGlobalTFTeam(GetTeamNumber() == TF_TEAM_RED ? TF_TEAM_BLUE : TF_TEAM_RED); }

void CTFPlayer::HandleCommand_JoinClass(const char *pClassName, bool) { ClientCommand(UTIL_VarArgs("joinclass %s", pClassName)); }
void CTFPlayer::HandleCommand_JoinTeam(const char *pTeamName) { ClientCommand(UTIL_VarArgs("jointeam %s", pTeamName)); }
void CTFPlayer::ForceChangeTeam(int iTeamNum, bool) { ChangeTeam(iTeamNum); }

void CTFPlayer::ForceRespawn()
{
	void *raw = GetRaw();
	if (raw)
		VCall<void>(raw, vt::ForceRespawn);
}

// ---------------------------------------------------------------------------------------------
// Buildings / objectives
// ---------------------------------------------------------------------------------------------
const CUtlVector<IBaseObjectAutoList *> &IBaseObjectAutoList::AutoList()
{
	static CUtlVector<IBaseObjectAutoList *> list;
	static int builtTick = -1;
	if (builtTick != gpGlobals->framecount)
	{
		builtTick = gpGlobals->framecount;
		list.RemoveAll();
		for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "obj_*"); e; e = gEntList.FindEntityByClassname(e, "obj_*"))
			if (e->IsBaseObject())
				list.AddToTail(static_cast<CBaseObject *>(e));
	}
	return list;
}

CObjectTeleporter *CObjectTeleporter::GetMatchingTeleporter()
{
	CTFPlayer *builder = GetBuilder();
	if (!builder)
		return NULL;
	return static_cast<CObjectTeleporter *>(builder->GetObjectOfType(OBJ_TELEPORTER, IsEntrance() ? MODE_TELEPORTER_EXIT : MODE_TELEPORTER_ENTRANCE));
}

CBaseEntity *CCaptureFlag::GetCarrier() const
{
	if (!IsStolen())
		return NULL;
	CBaseEntity *self = const_cast<CCaptureFlag *>(this);
	CBaseEntity *p = self->GetMoveParent();
	if (p && p->IsPlayer())
		return p;
	CBaseEntity *o = GetOwnerEntity();
	return (o && o->IsPlayer()) ? o : NULL;
}

bool CFuncRespawnRoom::PointIsWithin(const Vector &vecPoint)
{
	// brush entity: test the point against the brush model with an engine trace
	edict_t *ed = edict();
	if (!ed || !GetRaw())
		return false;
	Ray_t ray;
	ray.Init(vecPoint, vecPoint);
	trace_t tr;
	enginetrace->ClipRayToEntity(ray, MASK_ALL, (IHandleEntity *)GetRaw(), &tr);
	return tr.startsolid;
}

CTeamControlPoint *CTriggerAreaCapture::GetControlPoint() const
{
	CBaseEntity *p = HandleField("m_hPoint");
	if (p)
		return static_cast<CTeamControlPoint *>(p);
	string_t *name = Field<string_t>("m_iszCapPointName");
	if (name && STRING(*name))
		return static_cast<CTeamControlPoint *>(gEntList.FindEntityByName(NULL, STRING(*name)));
	return NULL;
}

bool CTeamControlPoint::IsLocked()
{
	bool *p = Field<bool>("m_bLocked");
	return p && *p;
}

float CTeamControlPoint::GetTeamCapPercentage(int iTeam)
{
	// objective resource: lazy cap percentage is the capping progress (0..1) of the capping team
	CTFObjectiveResource *res = TFObjectiveResource();
	int idx = GetPointIndex();
	if (res->GetCappingTeam(idx) != iTeam)
		return GetOwner() == iTeam ? 1.0f : 0.0f;
	return res->GetCPCapPercentage(idx);
}

CUtlVector<CHandle<CTeamControlPointMaster> > g_hControlPointMasters;

// The game fills g_hControlPointMasters as masters spawn; here it is rebuilt from the entity list.
void PF2_RefreshControlPointMasters()
{
	g_hControlPointMasters.RemoveAll();
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "team_control_point_master"); e; e = gEntList.FindEntityByClassname(e, "team_control_point_master"))
	{
		bool *disabled = e->Field<bool>("m_bDisabled");
		if (!disabled || !*disabled)
			g_hControlPointMasters.AddToTail(static_cast<CTeamControlPointMaster *>(e));
	}
}

int CTeamControlPointMaster::GetNumPoints() { return TFObjectiveResource()->GetNumControlPoints(); }

CTeamControlPoint *CTeamControlPointMaster::GetControlPoint(int point)
{
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "team_control_point"); e; e = gEntList.FindEntityByClassname(e, "team_control_point"))
	{
		CTeamControlPoint *cp = static_cast<CTeamControlPoint *>(e);
		if (cp->GetPointIndex() == point)
			return cp;
	}
	return NULL;
}

CBaseEntity *CTeamTrainWatcher::GetTrainEntity() const
{
	CBaseEntity *t = HandleField("m_hTrain");
	if (t)
		return t;
	string_t *name = Field<string_t>("m_iszTrain");
	return (name && STRING(*name)) ? gEntList.FindEntityByName(NULL, STRING(*name)) : NULL;
}

Vector CTeamTrainWatcher::GetNextCheckpointPosition() const
{
	CBaseEntity *t = GetTrainEntity();
	return t ? t->GetAbsOrigin() : vec3_origin;
}

void CTeamTrainWatcher::ProjectPointOntoPath(const Vector &pos, Vector *pathPos, float *distanceAlongPath) const
{
	// PF2NB: the path_track chain is not reachable from an addon; approximate with the train itself
	CBaseEntity *t = GetTrainEntity();
	if (pathPos) *pathPos = t ? t->GetAbsOrigin() : pos;
	if (distanceAlongPath) *distanceAlongPath = 0.0f;
}

bool CTeamTrainWatcher::IsAheadOfTrain(const Vector &) const { return false; }

// ---------------------------------------------------------------------------------------------
// Teams
// ---------------------------------------------------------------------------------------------
const char *CTeam::GetName() const
{
	const char *p = Field<const char>("m_szTeamname");
	return p ? p : "";
}

int CTeam::GetNumPlayers() const
{
	int n = 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBasePlayer *p = UTIL_PlayerByIndex(i);
		if (p && p->GetTeamNumber() == GetTeamNumber())
			++n;
	}
	return n;
}

CBasePlayer *CTeam::GetPlayer(int iIndex) const
{
	int n = 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBasePlayer *p = UTIL_PlayerByIndex(i);
		if (p && p->GetTeamNumber() == GetTeamNumber() && n++ == iIndex)
			return p;
	}
	return NULL;
}

CTeam *GetGlobalTeam(int iIndex)
{
	// NBPERF: scan the entity list for the team entities once per frame, not on every call
	static CTeam *s_teams[TF_TEAM_COUNT];
	static int s_frame = -1;
	if (s_frame != gpGlobals->framecount)   // framecount never resets across map changes
	{
		s_frame = gpGlobals->framecount;
		memset(s_teams, 0, sizeof(s_teams));
		for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "tf_team"); e; e = gEntList.FindEntityByClassname(e, "tf_team"))
		{
			int t = static_cast<CTeam *>(e)->GetTeamNumber();
			if (t >= 0 && t < TF_TEAM_COUNT)
				s_teams[t] = static_cast<CTeam *>(e);
		}
	}
	return (iIndex >= 0 && iIndex < TF_TEAM_COUNT) ? s_teams[iIndex] : NULL;
}

CTFTeam *GetGlobalTFTeam(int iIndex) { return static_cast<CTFTeam *>(GetGlobalTeam(iIndex)); }
int GetNumberOfTeams() { return TF_TEAM_COUNT; }
CTeam *CBaseEntity::GetTeam() const { return GetGlobalTeam(GetTeamNumber()); }

// ---------------------------------------------------------------------------------------------
// Game rules
// ---------------------------------------------------------------------------------------------
static CTFGameRules s_tfGameRules;
CGameRules *g_pGameRules = &s_tfGameRules;
CTFGameRules *TFGameRules() { return &s_tfGameRules; }

static CViewVectors s_viewVectors(
	Vector(0, 0, 72), Vector(-24, -24, 0), Vector(24, 24, 82), Vector(-24, -24, 0), Vector(24, 24, 62),
	Vector(0, 0, 45), Vector(-10, -10, -10), Vector(10, 10, 10), Vector(0, 0, 14));
const CViewVectors *CGameRules::GetViewVectors() const { return &s_viewVectors; }

// the gamerules object lives behind the proxy entity's datatable proxy function (as SourceMod does it)
static void *GameRulesObject(int *outBaseOffsetOfTable = NULL)
{
	// NBPERF: path costs ask the game rules for every nav area they expand; find the rules object
	// once per frame instead of scanning the entity list each call
	static void *s_obj = NULL;
	static int s_frame = -1;
	if (s_frame == gpGlobals->framecount)
		return s_obj;
	s_frame = gpGlobals->framecount;
	s_obj = NULL;
	CBaseEntity *proxy = gEntList.FindEntityByClassname(NULL, "tf_gamerules");
	void *raw = proxy ? proxy->GetRaw() : NULL;
	if (!raw)
		return NULL;
	ServerClass *sc = VCall<ServerClass *>(raw, vt::GetServerClass);
	int off = 0;
	SendProp *p = FindSendPropDeep(sc->m_pTable, "teamplayroundbased_gamerules_data", 0, &off);
	if (!p || !p->GetDataTableProxyFn())
		return NULL;
	CSendProxyRecipients recip;
	s_obj = (void *)p->GetDataTableProxyFn()(p, raw, (char *)raw + off, &recip, 0);
	return s_obj;
}

template <typename T>
static T GameRulesGet(const char *name, T def)
{
	void *gr = GameRulesObject();
	if (!gr)
		return def;
	static ServerClass *sc = NULL;
	if (!sc)
		for (ServerClass *c = gamedll->GetAllServerClasses(); c; c = c->m_pNext)
			if (!V_strcmp(c->GetName(), "CTFGameRulesProxy"))
				sc = c;
	if (!sc)
		return def;
	// NBPERF: offsets by name, computed once (-1 = not present)
	static const int CACHE = 64;
	static struct { const char *name; int off; } s_off[CACHE];
	static int s_n = 0;
	for (int c = 0; c < s_n; ++c)
		if (s_off[c].name == name || !V_strcmp(s_off[c].name, name))
			return s_off[c].off < 0 ? def : *(T *)((char *)gr + s_off[c].off);
	int found = -1;
	// offsets inside the gamerules data tables are relative to the gamerules object
	SendTable *t = sc->m_pTable;
	for (int i = 0; i < t->GetNumProps(); ++i)
	{
		SendProp *p = t->GetProp(i);
		if (p->GetType() != DPT_DataTable || !p->GetDataTable())
			continue;
		const char *pn = p->GetName();
		if (pn && (V_strstr(pn, "gamerules_data") || !V_strcmp(pn, "baseclass")))
		{
			int off = 0;
			if (FindSendPropDeep(p->GetDataTable(), name, 0, &off))
			{
				found = off;
				break;
			}
		}
	}
	if (s_n < CACHE)
	{
		s_off[s_n].name = name;   // callers pass string literals
		s_off[s_n].off = found;
		++s_n;
	}
	return found < 0 ? def : *(T *)((char *)gr + found);
}

CBaseEntity *CTFGameRules::Ent() const { return gEntList.FindEntityByClassname(NULL, "tf_gamerules"); }
gamerules_roundstate_t CTFGameRules::State_Get() const { return (gamerules_roundstate_t)GameRulesGet<int>("m_iRoundState", GR_STATE_RND_RUNNING); }
bool CTFGameRules::InSetup() const { return GameRulesGet<bool>("m_bInSetup", false); }
bool CTFGameRules::IsInWaitingForPlayers() const { return GameRulesGet<bool>("m_bInWaitingForPlayers", false); }
bool CTFGameRules::InOvertime() const { return GameRulesGet<bool>("m_bInOvertime", false); }
int CTFGameRules::GetWinningTeam() const { return GameRulesGet<int>("m_iWinningTeam", 0); }
bool CTFGameRules::IsInKothMode() const { return GameRulesGet<bool>("m_bPlayingKoth", false); }
bool CTFGameRules::IsCTFCPHybrid() const { return GameRulesGet<bool>("m_bCTFCPHybrid", false); }
bool CTFGameRules::IsInArenaMode() const { return gEntList.FindEntityByClassname(NULL, "tf_logic_arena") != NULL; }

int CTFGameRules::GetGameType() const
{
	if (gEntList.FindEntityByClassname(NULL, "team_train_watcher"))
		return TF_GAMETYPE_ESCORT;
	if (IsInArenaMode())
		return TF_GAMETYPE_ARENA;
	int t = GameRulesGet<int>("m_nGameType", TF_GAMETYPE_UNDEFINED);
	if ((t == TF_GAMETYPE_CTF && PF2_FlagsAreObjective()) || t == TF_GAMETYPE_CP)
		return t;
	if (PF2_FlagsAreObjective())
		return TF_GAMETYPE_CTF;
	if (gEntList.FindEntityByClassname(NULL, "team_control_point"))
		return TF_GAMETYPE_CP;
	return TF_GAMETYPE_UNDEFINED;
}

// PF2NB: a flag only counts as an objective when the map has somewhere to capture it
// (turkeyburgers_tfc's "flag" is a soccer ball with no capture zone)
bool PF2_FlagsAreObjective()
{
	return gEntList.FindEntityByClassname(NULL, "item_teamflag") && gEntList.FindEntityByClassname(NULL, "func_capturezone");
}

// PF2NB: no flag, control point or payload on the map (dm_/tdm_/mulch_dm/MARIO_KART/turkeyburgers/
// wacky_races ...): bots roam instead of looking for an objective. Cached per map for a few seconds.
bool PF2_IsObjectivelessMap()
{
	static float s_until = -1.0f;
	static bool s_result = false;
	if (gpGlobals->curtime < s_until && gpGlobals->curtime > s_until - 10.0f)
		return s_result;
	s_until = gpGlobals->curtime + 5.0f;
	s_result = !gEntList.FindEntityByClassname(NULL, "team_train_watcher") &&
		!PF2_FlagsAreObjective() &&
		!gEntList.FindEntityByClassname(NULL, "team_control_point");
	return s_result;
}

bool CTFGameRules::IsAttackDefenseMode()
{
	// TF2: CP map where one team owns every point at round start. PF2 flag A/D uses flags with
	// m_nGameType == TF_FLAGTYPE_ATTACK_DEFEND, handled by the CTF behaviours.
	CTFObjectiveResource *res = TFObjectiveResource();
	int n = res->GetNumControlPoints();
	if (n <= 0)
		return false;
	int owner = res->GetOwningTeam(0);
	if (owner != TF_TEAM_RED && owner != TF_TEAM_BLUE)
		return false;
	for (int i = 1; i < n; ++i)
		if (res->GetOwningTeam(i) != owner)
			return false;
	return true;
}

bool CTFGameRules::IsHuntedOrVIPMap() const
{
	const char *map = STRING(gpGlobals->mapname);
	if (map && (V_stristr(map, "hunted") || V_stristr(map, "vip") || StartsWith(map, "hunted_")))
		return true;
	// runtime evidence: a class filter for the civilian, or a civilian-only spawn
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "filter_tf_class"); e; e = gEntList.FindEntityByClassname(e, "filter_tf_class"))
	{
		int *cls = e->Field<int>("m_iAllowedClass");
		if (cls && *cls == TF_CLASS_CIVILIAN)
			return true;
	}
	return false;
}

bool CTFGameRules::CanBotChooseClass(CBasePlayer *pBot, int iDesiredClassIndex)
{
	extern bool PF2NB_ClassAllowedForTeam(int team, int tfclass);
	return PF2NB_ClassAllowedForTeam(pBot ? pBot->GetTeamNumber() : 0, iDesiredClassIndex);
}

// CTeamplayRules::WouldChangeUnbalanceTeams semantics: joining iNewTeam (from iCurrentTeam) is
// unbalancing if it puts iNewTeam more than mp_teams_unbalance_limit players ahead of the other team.
bool CTFGameRules::WouldChangeUnbalanceTeams(int iNewTeam, int iCurrentTeam)
{
	if (iNewTeam != TF_TEAM_RED && iNewTeam != TF_TEAM_BLUE)
		return false;
	if (iNewTeam == iCurrentTeam)
		return false;
	static ConVarRef limitRef("mp_teams_unbalance_limit", true);
	int limit = limitRef.IsValid() ? limitRef.GetInt() : 1;
	if (limit <= 0)
		return false;
	int other = iNewTeam == TF_TEAM_RED ? TF_TEAM_BLUE : TF_TEAM_RED;
	CTeam *newTeam = GetGlobalTeam(iNewTeam), *otherTeam = GetGlobalTeam(other);
	int newCount = newTeam ? newTeam->GetNumPlayers() : 0;
	int otherCount = otherTeam ? otherTeam->GetNumPlayers() : 0;
	if (iCurrentTeam == other)
		--otherCount;
	return (newCount + 1) - otherCount > limit;
}

CTeamRoundTimer *CTFGameRules::GetActiveRoundTimer()
{
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "team_round_timer"); e; e = gEntList.FindEntityByClassname(e, "team_round_timer"))
	{
		CTeamRoundTimer *t = static_cast<CTeamRoundTimer *>(e);
		if (t->ShowInHud() && !t->IsTimerPaused())
			return t;
	}
	return NULL;
}

float CTeamRoundTimer::GetTimeRemaining()
{
	float *end = Field<float>("m_flTimerEndTime");
	bool *paused = Field<bool>("m_bTimerPaused");
	float *rem = Field<float>("m_flTimeRemaining");
	if (paused && *paused && rem)
		return *rem;
	return end ? MAX(0.0f, *end - gpGlobals->curtime) : 0.0f;
}

void CTFGameRules::CollectCapturePoints(CBasePlayer *player, CUtlVector<CTeamControlPoint *> *captureVector) const
{
	if (!captureVector)
		return;
	captureVector->RemoveAll();
	CTFObjectiveResource *res = TFObjectiveResource();
	CTeamControlPointMaster master;
	int n = res->GetNumControlPoints();
	if (IsInKothMode() && n == 1)
	{
		if (CTeamControlPoint *p = master.GetControlPoint(0))
			captureVector->AddToTail(p);
		return;
	}
	for (int i = 0; i < n; ++i)
	{
		CTeamControlPoint *point = master.GetControlPoint(i);
		if (!point || !res->IsCPVisible(i))
			continue;
		if (res->GetOwningTeam(i) == player->GetTeamNumber())
			continue;
		if (res->TeamCanCapPoint(i, player->GetTeamNumber()) && !point->IsLocked())
			captureVector->AddToTail(point);
	}
}

void CTFGameRules::CollectDefendPoints(CBasePlayer *player, CUtlVector<CTeamControlPoint *> *defendVector) const
{
	if (!defendVector)
		return;
	defendVector->RemoveAll();
	CTFObjectiveResource *res = TFObjectiveResource();
	CTeamControlPointMaster master;
	int enemy = player->GetTeamNumber() == TF_TEAM_RED ? TF_TEAM_BLUE : TF_TEAM_RED;
	for (int i = 0; i < res->GetNumControlPoints(); ++i)
	{
		CTeamControlPoint *point = master.GetControlPoint(i);
		if (!point || !res->IsCPVisible(i))
			continue;
		if (res->GetOwningTeam(i) != player->GetTeamNumber())
			continue;
		if (res->TeamCanCapPoint(i, enemy) && !point->IsLocked())
			defendVector->AddToTail(point);
	}
}

static CTeamTrainWatcher *FindWatcher(int team, bool exactTeam)
{
	CTeamTrainWatcher *any = NULL;
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "team_train_watcher"); e; e = gEntList.FindEntityByClassname(e, "team_train_watcher"))
	{
		CTeamTrainWatcher *w = static_cast<CTeamTrainWatcher *>(e);
		if (w->IsDisabled())
			continue;
		if (w->GetTeamNumber() == team)
			return w;
		if (!any)
			any = w;
	}
	return exactTeam ? NULL : any;
}

bool CTFGameRules::HasMultipleTrains() const
{
	// PF2NB: PF2 has no m_bIsPayloadRace; team_train_watcher sets m_bMultipleTrains for race maps.
	// Fall back to "an enabled watcher for each team".
	if (GameRulesGet<bool>("m_bMultipleTrains", false))
		return true;
	return FindWatcher(TF_TEAM_RED, true) && FindWatcher(TF_TEAM_BLUE, true);
}

CTeamTrainWatcher *CTFGameRules::GetPayloadToPush(int pushingTeam) const
{
	if (GetGameType() != TF_GAMETYPE_ESCORT)
		return NULL;
	bool race = HasMultipleTrains();
	if (race)
		return FindWatcher(pushingTeam, true);
	return pushingTeam == TF_TEAM_BLUE ? FindWatcher(TF_TEAM_BLUE, false) : NULL;
}

CTeamTrainWatcher *CTFGameRules::GetPayloadToBlock(int blockingTeam) const
{
	if (GetGameType() != TF_GAMETYPE_ESCORT)
		return NULL;
	bool race = HasMultipleTrains();
	if (race)
		return FindWatcher(blockingTeam == TF_TEAM_RED ? TF_TEAM_BLUE : TF_TEAM_RED, true);
	return blockingTeam == TF_TEAM_RED ? FindWatcher(TF_TEAM_BLUE, false) : NULL;
}

static void RefreshItemVectors(CTFGameRules *gr)
{
	if (gr->m_itemVectorTime >= 0 && gpGlobals->curtime - gr->m_itemVectorTime < 1.0f && gpGlobals->curtime >= gr->m_itemVectorTime)
		return;
	gr->m_itemVectorTime = gpGlobals->curtime;
	gr->m_healthVector.RemoveAll();
	gr->m_ammoVector.RemoveAll();
	gr->m_armorVector.RemoveAll();
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "item_*"); e; e = gEntList.FindEntityByClassname(e, "item_*"))
	{
		if (e->ClassMatches("item_healthkit*") || e->ClassMatches("item_healthammokit*"))
			gr->m_healthVector.AddToTail(e);
		if (e->ClassMatches("item_ammopack*") || e->ClassMatches("item_healthammokit*") || e->ClassMatches("item_backpack*"))
			gr->m_ammoVector.AddToTail(e);
		if (e->ClassMatches("item_armor*"))		// PF2 armor pickup (CArmor): +200 up to the class max
			gr->m_armorVector.AddToTail(e);
	}
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "func_regenerate"); e; e = gEntList.FindEntityByClassname(e, "func_regenerate"))
	{
		gr->m_healthVector.AddToTail(e);
		gr->m_ammoVector.AddToTail(e);
		gr->m_armorVector.AddToTail(e);	// resupply also refills armor
	}
}

CUtlVector<CHandle<CBaseEntity> > &CTFGameRules::GetHealthEntityVector() { RefreshItemVectors(this); return m_healthVector; }
CUtlVector<CHandle<CBaseEntity> > &CTFGameRules::GetAmmoEntityVector() { RefreshItemVectors(this); return m_ammoVector; }
CUtlVector<CHandle<CBaseEntity> > &CTFGameRules::GetArmorEntityVector() { RefreshItemVectors(this); return m_armorVector; }
void CTFGameRules::OnLevelInit() { m_itemVectorTime = -1.0f; m_healthVector.RemoveAll(); m_ammoVector.RemoveAll(); m_armorVector.RemoveAll(); }

// ---------------------------------------------------------------------------------------------
// Objective resource
// ---------------------------------------------------------------------------------------------
static CTFObjectiveResource s_objRes;
CTFObjectiveResource *TFObjectiveResource() { return &s_objRes; }
CBaseEntity *CTFObjectiveResource::Ent() { return gEntList.FindEntityByClassname(NULL, "tf_objective_resource"); }
int CTFObjectiveResource::GetNumControlPoints() { CBaseEntity *e = Ent(); int *p = e ? e->Field<int>("m_iNumControlPoints") : NULL; return p ? *p : 0; }
int CTFObjectiveResource::GetOwningTeam(int i) { return SendArrayGet<int>(Ent(), "m_iOwner", i, 0); }
bool CTFObjectiveResource::IsCPVisible(int i) { return SendArrayGet<int>(Ent(), "m_bCPIsVisible", i, 1) != 0; }
int CTFObjectiveResource::GetCappingTeam(int i) { return SendArrayGet<int>(Ent(), "m_iCappingTeam", i, 0); }
float CTFObjectiveResource::GetCPCapPercentage(int i) { return SendArrayGet<float>(Ent(), "m_flLazyCapPerc", i, 0.0f); }

bool CTFObjectiveResource::TeamCanCapPoint(int i, int team)
{
	// m_bTeamCanCap is laid out [point + team * MAX_CONTROL_POINTS] (bool elements)
	return SendArrayGet<bool>(Ent(), "m_bTeamCanCap", i + team * MAX_CONTROL_POINTS, true);
}

int CTFObjectiveResource::GetPreviousPointForPoint(int index, int team, int iPrevIndex)
{
	return SendArrayGet<int>(Ent(), "m_iPreviousPoints", (index * MAX_CONTROL_POINT_TEAMS + team) * MAX_PREVIOUS_POINTS + iPrevIndex, -1);
}
