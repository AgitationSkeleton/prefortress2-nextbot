// pf2-nextbot game layer: TF/PF2 proxies (players, weapons, buildings, objectives, teams, gamerules).
// Enum values come from PF2's own tf_shareddefs.h. TF2-only names that PF2 lacks are defined in
// pf2_tf2compat.h with values that never match a PF2 entity, so TF2-only branches are inert.
#pragma once
#include "tf_shareddefs.h"
#include "pf2_tf2compat.h"
#include "pf2_player.h"
// PF2NB: the property accessors below need the __declspec keyword (MSVC; Clang with -fdeclspec).
// tier0/platform.h defines __declspec away on POSIX, so lift that macro for this header only.
#if !defined(_MSC_VER)
#pragma push_macro("__declspec")
#undef __declspec
#endif

class CTFPlayer;
class CTFWeaponBase;
class CBaseObject;
class CCaptureFlag;
class CCaptureZone;
class CTeamControlPoint;
class CTeamControlPointMaster;
class CTeamTrainWatcher;
class CTFTeam;
class CTFNavArea;
class INextBot;
class CTFBot;
class CTriggerAreaCapture;

// --------------------------------------------------------------------------------------------
// Per-class data parsed from PF2's scripts/playerclasses/<class>.txt
// --------------------------------------------------------------------------------------------
struct PF2ClassData
{
	char  name[32];
	int   maxHealth;
	int   maxArmor;
	float maxSpeed;
	int   maxAmmo[TF_AMMO_COUNT];
	int   grenade[2];          // TF_WEAPON_GRENADE_* ids for slot 1/2 (0 = none)
	char  grenadeName[2][48];
	int   weapon[6];           // TF_WEAPON_* ids listed by the class script
};
const PF2ClassData *PF2_GetClassData(int tfclass);
struct TFPlayerClassData_t
{
	char m_szClassName[32];
	float m_flMaxSpeed;
	int m_nMaxHealth;
	int m_nMaxArmor;
};
TFPlayerClassData_t *GetPlayerClassData(int iClass);
int GetClassIndexFromString(const char *pClassName, int nLastClassIndex = TF_LAST_NORMAL_CLASS);
struct PF2NullLocalize
{
	wchar_t *Find(const char *) { return NULL; }
	int ConvertUnicodeToANSI(const wchar_t *, char *buf, int) { if (buf) buf[0] = 0; return 0; }
};
extern PF2NullLocalize *g_pVGuiLocalize;
void PF2_LoadClassData();
int PF2_ClassFromName(const char *name);   // "soldier" -> TF_CLASS_SOLDIER, 0 if unknown
const char *PF2_ClassName(int tfclass);    // TF_CLASS_SOLDIER -> "soldier"

// --------------------------------------------------------------------------------------------
class FileWeaponInfo_t {};
class CTFWeaponInfo : public FileWeaponInfo_t
{
public:
	struct WeaponData_t
	{
		int m_nDamage = 0;
		int m_nBulletsPerShot = 1;
		float m_flRange = 8192.0f;
		float m_flSpread = 0.0f;
		float m_flTimeFireDelay = 0.5f;
		float m_flProjectileSpeed = 0.0f;
		bool m_bUseRapidFireCrits = false;
		int m_iProjectile = 1;
	};
	WeaponData_t m_WeaponData[2];
	const WeaponData_t &GetWeaponData(int iWeapon) const { return m_WeaponData[iWeapon]; }
	char szClassName[64] = "";
};

typedef unsigned short WEAPON_FILE_INFO_HANDLE;
WEAPON_FILE_INFO_HANDLE LookupWeaponInfoSlot(const char *name);
WEAPON_FILE_INFO_HANDLE GetInvalidWeaponInfoHandle(void);
class FileWeaponInfo_t;
FileWeaponInfo_t *GetFileWeaponInfoFromHandle(WEAPON_FILE_INFO_HANDLE handle);

class CTFWeaponBase : public CBaseCombatWeapon
{
public:
	DECLARE_CLASS(CTFWeaponBase, CBaseCombatWeapon);
	int GetWeaponID() const;
	bool IsWeapon(int iWeapon) const { return GetWeaponID() == iWeapon; }
	bool IsMeleeWeapon() const;
	bool IsGrenade() const;
	int GetSlot() const;
	float GetProjectileSpeed();
	const CTFWeaponInfo &GetTFWpnData() const;
	int GetMaxClip1() const;
	bool IsReloading() const { return false; }
	bool IsFiring() const { return false; }
	CTFPlayer *GetTFPlayerOwner() const;
	bool IsEnergyWeapon() const { return false; }
	bool IsBlastImpactWeapon() const { return false; }
	bool CanAttack() const { return true; }
	int GetPrimaryAmmoType() const { return CBaseCombatWeapon::GetPrimaryAmmoType(); }
	float GetChargeBeginTime() { float *p = Field<float>("m_flChargeBeginTime"); return p ? *p : 0.0f; }
	float GetCurrentCharge() { return 0.0f; }
	float GetChargeMaxTime() { return 4.0f; }
	bool IsZoomed() const;
	float GetNextPrimaryAttack() const { float *p = Field<float>("m_flNextPrimaryAttack"); return p ? *p : 0.0f; }
	__declspec(property(get = GetNextPrimaryAttack)) float m_flNextPrimaryAttack;
	float GetTimeSinceLastFired() const;
	bool IsPrimed() const { int *p = Field<int>("m_bPrimed"); return p && (*p & 0xff); }
	int GetPipeBombCount() const { int *p = Field<int>("m_iPipebombCount"); return p ? *p : 0; }
private:
	mutable CTFWeaponInfo m_info;
};
inline CTFWeaponBase *ToTFWeaponBase(CBaseEntity *e) { return (e && e->IsBaseCombatWeapon()) ? static_cast<CTFWeaponBase *>(e) : NULL; }

// TF2 weapon subclasses referenced by TFBot. PF2 has only some of these; all map onto CTFWeaponBase.
class CTFWeaponBaseGun : public CTFWeaponBase {};
class CTFWeaponBaseMelee : public CTFWeaponBase {};
class CTFSniperRifle : public CTFWeaponBaseGun
{
public:
	float GetChargedDamage() { return 50.0f; }
	bool IsZoomed() { return CTFWeaponBase::IsZoomed(); }
};
class CTFGrenadePipebombProjectile : public CBaseEntity
{
public:
	bool GetTouched() const { bool *p = Field<bool>("m_bTouched"); return p && *p; }
	__declspec(property(get = GetTouched)) bool m_bTouched;
};

class CTFPipebombLauncher : public CTFWeaponBaseGun
{
public:
	int GetPipeBombCount() { return CTFWeaponBase::GetPipeBombCount(); }
	float GetChargeMaxTime() { return 4.0f; }
	CUtlVector<CHandle<CTFGrenadePipebombProjectile> > &GetPipeBombVector();
};
class CWeaponMedigun : public CTFWeaponBaseGun
{
public:
	CBaseEntity *GetHealTarget() const { return HandleField("m_hHealingTarget"); }
	float GetChargeLevel() const { float *p = Field<float>("m_flChargeLevel"); return p ? *p : 0.0f; }
	bool IsReleasingCharge() const { bool *p = Field<bool>("m_bChargeRelease"); return p && *p; }
	int GetMedigunType() const { return 0; }
	float GetTargetRange() { return 450.0f; }
	void CycleResistType() {}
	float GetMinChargeAmount() const { return 1.0f; }
	bool IsAttachedToBuilding() const { return false; }
	medigun_charge_types GetResistType() const { return (medigun_charge_types)0; }
};
class CTFFlameThrower : public CTFWeaponBaseGun
{
public:
	bool CanAirBlast() const { return false; }   // PF2's flamethrower has no compression blast
	bool CanAirBlastDeflectProjectile() const { return false; }
};
class CTFCompoundBow : public CTFWeaponBaseGun   // TF2 Huntsman; PF2 has no bow
{
public:
	float GetCurrentCharge() { return 0.0f; }
	float GetChargeMaxTime() { return 1.0f; }
	float GetProjectileSpeed() { return 1800.0f; }
	float GetProjectileGravity() { return 0.5f; }
};
inline bool WeaponID_IsSniperRifle(int id) { return id == TF_WEAPON_SNIPERRIFLE; }

class CTFBuffItem : public CTFWeaponBase { public: bool IsFull() { return false; } };     // TF2-only items
class CTFLunchBox : public CTFWeaponBase { public: bool HasAmmo() { return false; } };
bool PointInRespawnRoom(const CBaseEntity *pTarget, const Vector &vecOrigin, bool bTouching_SameTeamOnly = false);

class CTFWeaponBuilder : public CTFWeaponBase
{
public:
	int GetType() const { int *p = Field<int>("m_iObjectType"); return p ? *p : -1; }
	bool IsValidPlacement() const;
	CBaseEntity *GetPlacementObject() const;   // the owner's blueprint (object still placing)
	__declspec(property(get = GetPlacementObject)) CBaseEntity *m_hObjectBeingBuilt;
};
class CTFWeaponBaseGrenade : public CTFWeaponBase {};

// --------------------------------------------------------------------------------------------
class CTFPlayerClass
{
public:
	int GetClassIndex() const;
	bool IsClass(int iClass) const { return GetClassIndex() == iClass; }
	const char *GetName() const { return PF2_ClassName(GetClassIndex()); }
	float GetMaxSpeed() const { const PF2ClassData *d = PF2_GetClassData(GetClassIndex()); return d ? d->maxSpeed : 300.0f; }
	int GetMaxHealth() const { const PF2ClassData *d = PF2_GetClassData(GetClassIndex()); return d ? d->maxHealth : 100; }
	bool HasCustomModel() const { return false; }
	CTFPlayer *m_pOuter = NULL;
};

class CTFPlayerShared
{
public:
	bool InCond(int nCond) const;
	void AddCond(int nCond, float flDuration = -1.0f, CBaseEntity *pProvider = NULL) {}
	void RemoveCond(int nCond, bool ignore_Healing = false) {}
	bool IsInvulnerable() const { return InCond(TF_COND_INVULNERABLE); }
	bool IsStealthed() const { return InCond(TF_COND_STEALTHED); }
	bool IsDisguised() const { return InCond(TF_COND_DISGUISED); }
	bool IsCarryingObject() const;
	bool IsControlStunned() const { return false; }
	bool IsLoserStateStunned() const { return false; }
	bool IsParachuteEquipped() const { return false; }
	bool IsShieldEquipped() const { return false; }
	bool IsRageDraining() const { return false; }
	bool IsAlly(CBaseEntity *) const { return false; }
	int GetDisguiseTeam() const;
	int GetDisguiseClass() const;
	float GetPercentInvisible() const;
	int GetNumHealers() const;
	CBaseEntity *GetHealerByIndex(int index) const { return NULL; }
	bool HealerIsDispenser(int index) const { return false; }
	int GetMaxBuffedHealth() const;
	float GetRageMeter() const { return 0.0f; }
	float GetScoutEnergyDrinkMeter() const { return 0.0f; }
	int GetTauntIndex() const { return 0; }
	void ResetRageBuffs() {}
	void SetSeparation(bool) {}
	void SetSeparationVelocity(const Vector &) {}
	void AddToSpyCloakMeter(float, bool = false) {}
	void Disguise(int nTeam, int nClass, CTFPlayer *pDesiredTarget = NULL, bool bOnKill = false);
	CTFWeaponBase *GetActiveTFWeapon() const;
	float GetSpyCloakMeter() const;
	CTFPlayer *m_pOuter = NULL;
};

class CTFPlayer : public CBaseMultiplayerPlayer
{
public:
	DECLARE_CLASS(CTFPlayer, CBaseMultiplayerPlayer);
	CTFPlayer();
	CTFPlayerClass *GetPlayerClass() { return &m_PlayerClass; }
	const CTFPlayerClass *GetPlayerClass() const { return &m_PlayerClass; }
	bool IsPlayerClass(int iClass) const { return m_PlayerClass.IsClass(iClass); }
	int GetDesiredPlayerClassIndex() const;
	CTFWeaponBase *GetActiveTFWeapon() const { return ToTFWeaponBase(GetActiveWeapon()); }
	CTFWeaponBase *Weapon_GetWeaponByType(int iType);
	CTFWeaponBase *Weapon_OwnsThisID(int iWeaponID) const;
	CTFWeaponBase *GetWeaponOfID(int iWeaponID) const { return Weapon_OwnsThisID(iWeaponID); }
	bool HasTheFlag() const;
	CBaseEntity *GetItem() const { return HandleField("m_hItem"); }
	bool HasItem() const { return GetItem() != NULL; }
	int GetMaxAmmo(int iAmmoIndex, int iClassNumber = -1) const;
	int GetAvailableMetal() const { return GetAmmoCount(TF_AMMO_METAL); }
	int GetArmorValue() const { int *p = Field<int>("m_ArmorValue"); return p ? *p : 0; }
	int GetMaxArmor() const { const PF2ClassData *d = PF2_GetClassData(m_PlayerClass.GetClassIndex()); return d ? d->maxArmor : 0; }
	int GetGrenadeCount(int slot) const { return GetAmmoCount(slot == 0 ? TF_AMMO_GRENADES1 : TF_AMMO_GRENADES2); }
	float GetNextThrowTime() const;
	bool IsGrenadePrimed() const;
	Vector GetClassEyeHeight();
	Vector EstimateProjectileImpactPosition(CTFWeaponBaseGun *weapon);
	Vector EstimateProjectileImpactPosition(float pitch, float yaw, float initVel);
	Vector EstimateStickybombProjectileImpactPosition(float pitch, float yaw, float charge);
	bool IsInCombat() const { return GetTimeSinceLastInjury() < 2.0f; }
	void EndLongTaunt() {}
	void HandleTauntCommand(int iTauntSlot = 0) {}
	bool IsReadyToSpawn() { return false; }
	float GetSpawnTime() const { return m_spawnTime; }
	void SetSpawnTime(float t) { m_spawnTime = t; }
	float GetTimeSinceCalledForMedic() const { return 99999.9f; }
	bool IsCallingForMedic() const { return false; }
	CBaseEntity *MedicGetHealTarget();
	float MedicGetChargeLevel(CTFWeaponBase **pRetMedigun = NULL);
	bool IsAnyEnemySentryAbleToAttackMe() const { return false; }
	bool IsThreatAimingTowardMe(CBaseEntity *threat, float cosTolerance = 0.8f) const;
	bool IsThreatFiringAtMe(CBaseEntity *threat) const { return false; }
	bool IsFiringWeapon() const { return false; }
	float GetTimeSinceWeaponFired() const { return 99999.9f; }
	bool IsPlacingSapper() const { return false; }
	float GetDamagePerSecond() const { return 0.0f; }
	void StartBuildingObjectOfType(int iType, int iMode = 0);   // PF2 "build <type>" command
	bool IsCapturingPoint();
	CTriggerAreaCapture *GetControlPointStandingOn();
	CTeamControlPoint *SelectClosestControlPointByTravelDistance(CUtlVector<CTeamControlPoint *> *pointVector) const;
	CBaseObject *GetObjectOfType(int iObjectType, int iObjectMode = 0) const;
	int GetObjectCount() const;
	CBaseObject *GetObject(int i) const;
	bool CanBuild(int iObjectType, int iObjectMode = 0) { return true; }
	bool CanDisguise() { return true; }
	bool IsMiniBoss() const { return false; }
	bool IsAllowedToPickUpFlag() const { return true; }
	void InitClass() {}
	void UpdateOnRemove() {}
	void TeamFortress_SetSpeed() {}
	bool IsBotOfType(int) const { return IsFakeClient(); }
	int GetAllowedTauntPartnerTeam() const { return GetTeamNumber(); }
	CTFTeam *GetTFTeam() const;
	CTFTeam *GetOpposingTFTeam() const;
	void HandleCommand_JoinClass(const char *pClassName, bool bAllowSpawn = true);
	void HandleCommand_JoinTeam(const char *pTeamName);
	void ForceChangeTeam(int iTeamNum, bool bFullTeamSwitch = false);
	void ForceRespawn();
	void UpdateModelToClass() {}
	void Taunt() {}
	bool IsTaunting() const { return m_Shared.InCond(TF_COND_TAUNTING); }
	bool IsReadyToTauntWithPartner() const { return false; }
	CTFPlayer *FindPartnerTauntInitiator() { return NULL; }
	bool IsAllowedToTaunt() { return false; }
	float GetAliveDuration() const { return gpGlobals->curtime - m_spawnTime; }
	float GetDesiredHeadScale() const { return 1.0f; }
	float GetModelScale() const { return 1.0f; }
	bool IsInspecting() const { return false; }
	bool IsUsingActionSlot() const { return false; }
	bool CanAirDash() const { return false; }
	CBaseEntity *GetGrapplingHookTarget() const { return NULL; }
	bool IsInPurgatory() const { return false; }
	float GetHealthBuffPercent() const { return 0.0f; }
	bool IsBotCustomName() const { return false; }

	CTFPlayerShared m_Shared;
	CTFPlayerClass m_PlayerClass;
protected:
	float m_spawnTime = 0.0f;
};

inline CTFPlayer *ToTFPlayer(CBaseEntity *pEntity)
{
	return (pEntity && pEntity->IsPlayer()) ? static_cast<CTFPlayer *>(pEntity) : NULL;
}
inline const CTFPlayer *ToTFPlayer(const CBaseEntity *pEntity) { return ToTFPlayer(const_cast<CBaseEntity *>(pEntity)); }

// --------------------------------------------------------------------------------------------
// Buildings
// --------------------------------------------------------------------------------------------
// TF2 keeps an autolist of buildings; here it is rebuilt from the entity list at most once per tick
class IBaseObjectAutoList
{
public:
	virtual ~IBaseObjectAutoList() {}
	static const CUtlVector<IBaseObjectAutoList *> &AutoList();
};

class CBaseObject : public CBaseCombatCharacter, public IBaseObjectAutoList
{
public:
	DECLARE_CLASS(CBaseObject, CBaseCombatCharacter);
	int ObjectType() const { return GetType(); }
	void StartPlacement(CTFPlayer *) {}
	void StartBuilding(CBaseEntity *) {}
	void Spawn() {}
	void SetBuilder(CTFPlayer *) {}
	void SetAbsAngles(const QAngle &) {}
	void SetObjectMode(int) {}
	void DetonateObject();
	void DestroyObject() { DetonateObject(); }
	float GetTimeSinceLastFired() const { return 99999.9f; }
	Vector BodyDirection2D() { Vector f; AngleVectors(GetAbsAngles(), &f); f.z = 0; f.NormalizeInPlace(); return f; }
	bool IsBaseObject() const override { return true; }
	int GetPF2Type() const { int *p = Field<int>("m_iObjectType"); return p ? *p : -1; }
	int GetType() const { return GetPF2Type(); }
	int GetObjectMode() const { int *p = Field<int>("m_iObjectMode"); return p ? *p : 0; }
	CTFPlayer *GetBuilder() const { return ToTFPlayer(HandleField("m_hBuilder")); }
	CTFPlayer *GetOwner() const { return GetBuilder(); }
	bool IsBuilding() const { bool *p = Field<bool>("m_bBuilding"); return p && *p; }
	bool IsPlacing() const { bool *p = Field<bool>("m_bPlacing"); return p && *p; }
	bool IsCarried() const { bool *p = Field<bool>("m_bCarried"); return p && *p; }
	bool IsUpgrading() const { return false; }
	bool HasSapper() const { bool *p = Field<bool>("m_bHasSapper"); return p && *p; }
	bool IsPlasmaDisabled() const { return false; }
	bool IsDisabled() const { bool *p = Field<bool>("m_bDisabled"); return p && *p; }
	bool IsMiniBuilding() const { return false; }
	bool IsDying() const { return false; }
	int GetUpgradeLevel() const { int *p = Field<int>("m_iUpgradeLevel"); return p ? *p : 1; }
	int GetMaxUpgradeLevel() const { return 3; }
	int GetUpgradeMetal() const { int *p = Field<int>("m_iUpgradeMetal"); return p ? *p : 0; }
	int GetUpgradeMetalRequired() const { return 200; }
	int GetMaxHealth() const { int *p = Field<int>("m_iMaxHealth"); return p ? *p : 150; }
	bool ShouldBlockNav() const { return false; }
	bool IsBeingCarried() const { return IsCarried(); }
};
class CObjectSentrygun : public CBaseObject
{
public:
	CBaseEntity *GetEnemy() const { return HandleField("m_hEnemy"); }
	QAngle GetTurretAngles() const { return GetAbsAngles(); }
	int GetAmmoShells() const { int *p = Field<int>("m_iAmmoShells"); return p ? *p : 0; }
	int GetMaxAmmoShells() const { return 150; }
};
class CObjectDispenser : public CBaseObject
{
public:
	int GetMetalAmmoCount() const { int *p = Field<int>("m_iAmmoMetal"); return p ? *p : 0; }
	int GetAvailableMetal() const { return GetMetalAmmoCount(); }
};
class CObjectTeleporter : public CBaseObject
{
public:
	bool IsEntrance() const { return GetObjectMode() == MODE_TELEPORTER_ENTRANCE; }
	bool IsExit() const { return GetObjectMode() == MODE_TELEPORTER_EXIT; }
	CObjectTeleporter *GetMatchingTeleporter();
	bool IsSendingPlayer(CTFPlayer *) { return false; }
	int GetState() const { int *p = Field<int>("m_iState"); return p ? *p : 0; }
	bool IsReady() const { return GetState() > 1; }
};
class CObjectSapper : public CBaseObject {};

// --------------------------------------------------------------------------------------------
// Objectives
// --------------------------------------------------------------------------------------------
class ICaptureZoneAutoList { public: virtual ~ICaptureZoneAutoList() {} static const CUtlVector<ICaptureZoneAutoList *> &AutoList(); };
class IFuncRespawnRoomAutoList { public: virtual ~IFuncRespawnRoomAutoList() {} static const CUtlVector<IFuncRespawnRoomAutoList *> &AutoList(); };
class ITFTeamSpawnAutoList { public: virtual ~ITFTeamSpawnAutoList() {} static const CUtlVector<ITFTeamSpawnAutoList *> &AutoList(); };

bool PF2_IsObjectivelessMap();   // PF2NB: no flag / control point / payload on this map
bool PF2_FlagsAreObjective();    // PF2NB: item_teamflag AND a func_capturezone to take it to
class ICaptureFlagAutoList { public: virtual ~ICaptureFlagAutoList() {} static const CUtlVector<ICaptureFlagAutoList *> &AutoList(); };

class CTFItem : public CBaseEntity
{
public:
	CTFPlayer *GetPrevOwner() const { return ToTFPlayer(HandleField("m_hPrevOwner")); }
};

class CCaptureFlag : public CTFItem, public ICaptureFlagAutoList
{
public:
	DECLARE_CLASS(CCaptureFlag, CTFItem);
	int GetType() const { return GetGameType(); }   // TF2: flag type (TF_FLAGTYPE_*)
	// bot bookkeeping kept on the proxy, as TF2 keeps it on the flag entity
	void AddFollower(CTFBot *bot);
	void RemoveFollower(CTFBot *bot);
	int GetNumFollowers() const { return m_followers.Count(); }
	CUtlVector<CHandle<CTFPlayer> > m_followers;
	int GetGameType() const { int *p = Field<int>("m_nGameType"); return p ? *p : TF_FLAGTYPE_CTF; }
	int GetFlagStatus() const { int *p = Field<int>("m_nFlagStatus"); return p ? *p : TF_FLAGINFO_NONE; }
	bool IsHome() const { return GetFlagStatus() == TF_FLAGINFO_NONE; }
	bool IsStolen() const { return GetFlagStatus() == TF_FLAGINFO_STOLEN; }
	bool IsDropped() const { return GetFlagStatus() == TF_FLAGINFO_DROPPED; }
	bool IsDisabled() const { bool *p = Field<bool>("m_bDisabled"); return p && *p; }
	bool IsPoisonous() const { return false; }
	float GetMaxReturnTime() const { return 60.0f; }
	float GetReturnProgress() const { return 0.0f; }
	CBaseEntity *GetOwnerEntity() const { return HandleField("m_hOwnerEntity"); }
	CBaseEntity *GetCarrier() const;   // player carrying it, or NULL
	const Vector &GetResetPos() const { return m_resetPos; }
	Vector m_resetPos;
	bool m_resetPosKnown = false;
	void PickUp(CTFPlayer *pPlayer, bool bInvisible) {}
};

class CCaptureZone : public CBaseEntity, public ICaptureZoneAutoList
{
public:
	bool IsDisabled() const { bool *p = Field<bool>("m_bDisabled"); return p && *p; }
};


class CTFTeamSpawn : public CBaseEntity, public ITFTeamSpawnAutoList
{
public:
	bool IsDisabled() const { bool *p = Field<bool>("m_bDisabled"); return p && *p; }
	bool IsTriggered(CBaseEntity *) const { return !IsDisabled(); }
	int GetMatchSummaryType() const { return 0; }
};

class CFuncRespawnRoom : public CBaseEntity, public IFuncRespawnRoomAutoList
{
public:
	bool GetDisabled() const { bool *p = Field<bool>("m_bDisabled"); return p && *p; }
	__declspec(property(get = GetDisabled)) bool m_bDisabled;
	bool PointIsWithin(const Vector &vecPoint);
	bool GetActive() const { bool *p = Field<bool>("m_bActive"); return !p || *p; }
};

class CTeamControlPoint : public CBaseEntity
{
public:
	DECLARE_CLASS(CTeamControlPoint, CBaseEntity);
	int GetPointIndex() const { int *p = Field<int>("m_iPointIndex"); return p ? *p : 0; }
	int GetOwner() const { return GetTeamNumber(); }
	bool IsLocked();
	float GetTeamCapPercentage(int iTeam);
	bool HasBeenContested() const { return false; }
	float LastContestedAt() { return -1.0f; }
	int GetPreviousPointForPoint(int iTeam, int iPrevPoint) { return -1; }
	bool IsActive() const { return true; }
	const char *GetName() const { string_t s = GetEntityName(); return STRING(s) ? STRING(s) : ""; }
};

class CTriggerAreaCapture : public CBaseEntity
{
public:
	CTeamControlPoint *GetControlPoint() const;
	bool IsActive() const { return true; }
	float GetCapturerCount() { return 0.0f; }
	bool PassesTriggerFilters(CBaseEntity *) { return true; }
};

class CTeamControlPointMaster : public CBaseEntity
{
public:
	int GetNumPoints();
	CTeamControlPoint *GetControlPoint(int point);
	bool PlayingMiniRounds() const { return false; }
	bool IsInRound(CTeamControlPoint *pPoint) { return true; }
	int GetBaseControlPoint(int iTeam) { return -1; }
};
extern CUtlVector<CHandle<CTeamControlPointMaster> > g_hControlPointMasters;

class CTeamTrainWatcher : public CBaseEntity
{
public:
	CBaseEntity *GetTrainEntity() const;
	bool IsDisabled() const { bool *p = Field<bool>("m_bDisabled"); return p && *p; }
	float GetTrainDistanceAlongTrack() const { return 0.0f; }
	Vector GetNextCheckpointPosition() const;
	bool IsTrainNearCheckpoint() const { return false; }
	bool IsTrainAtStart() const { return false; }
	float GetTrainProgress() const { float *p = Field<float>("m_flTotalProgress"); return p ? *p : 0.0f; }
	void ProjectPointOntoPath(const Vector &pos, Vector *pathPos, float *distanceAlongPath) const;
	bool IsAheadOfTrain(const Vector &pos) const;
	bool IsTrainMovingForward() const { return true; }
	int GetCapturerCount() const { int *p = Field<int>("m_nNumCappers"); return p ? *p : 0; }
};

// --------------------------------------------------------------------------------------------
// Teams
// --------------------------------------------------------------------------------------------
class CTeam : public CBaseEntity
{
public:
	const char *GetName() const;
	// CTeam keeps its own networked m_iTeamNum (DT_Team), separate from CBaseEntity's datamap field
	int GetTeamNumber() const { int *p = SendField<int>("m_iTeamNum"); return p ? *p : 0; }
	int GetNumPlayers() const;
	CBasePlayer *GetPlayer(int iIndex) const;
	int GetScore() const { int *p = Field<int>("m_iScore"); return p ? *p : 0; }
	int GetRoundsWon() const { int *p = Field<int>("m_iRoundsWon"); return p ? *p : 0; }
};
class CTFTeam : public CTeam
{
public:
	int GetNumObjects(int iObjectType = -1) { return 0; }
	int GetFlagCaptures() { return GetScore(); }
	int GetRole() const { int *p = Field<int>("m_iRole"); return p ? *p : 0; }
};
CTeam *GetGlobalTeam(int iIndex);
inline int GetEnemyTeam(int iTeam) { return iTeam == TF_TEAM_RED ? TF_TEAM_BLUE : (iTeam == TF_TEAM_BLUE ? TF_TEAM_RED : iTeam); }
CTFTeam *GetGlobalTFTeam(int iIndex);
int GetNumberOfTeams();

// --------------------------------------------------------------------------------------------
// Game rules (read from the tf_gamerules entity)
// --------------------------------------------------------------------------------------------
class CTeamRoundTimer : public CBaseEntity
{
public:
	float GetTimeRemaining();
	bool IsTimerPaused() const { bool *p = Field<bool>("m_bTimerPaused"); return p && *p; }
	bool ShowInHud() const { bool *p = Field<bool>("m_bShowInHUD"); return !p || *p; }
};

class CGameRules
{
public:
	virtual ~CGameRules() {}
	const CViewVectors *GetViewVectors() const;
	bool IsMultiplayer() const { return true; }
	void OnNavMeshLoad() {}
};
extern CGameRules *g_pGameRules;
inline CGameRules *GameRules() { return g_pGameRules; }

class CTFGameRules : public CGameRules
{
public:
	CBaseEntity *Ent() const;   // the tf_gamerules proxy entity (may be NULL)
	int GetGameType() const;
	gamerules_roundstate_t State_Get() const;
	bool InSetup() const;
	bool IsInWaitingForPlayers() const;
	bool InOvertime() const;
	bool InRoundRestart() const { return State_Get() == GR_STATE_PREROUND; }
	bool IsInPreMatch() const { return false; }
	int GetWinningTeam() const;
	bool RoundHasBeenWon() const { return State_Get() == GR_STATE_TEAM_WIN; }
	bool IsInKothMode() const;
	bool IsInArenaMode() const;
	bool IsMannVsMachineMode() const { return false; }
	bool IsInTraining() const { return false; }
	bool IsRaidMode() const { return false; }
	bool IsBossBattleMode() const { return false; }
	bool IsCreepWaveMode() const { return false; }
	bool IsPVEModeActive() const { return false; }
	bool IsInMedievalMode() const { return false; }
	bool IsMatchTypeCompetitive() const { return false; }
	bool IsPasstimeMode() const { return false; }
	bool IsPlayingSpecialDeliveryMode() const { return false; }
	bool IsPlayingRobotDestructionMode() const { return false; }
	bool IsHolidayActive(int) const { return false; }
	bool IsAttackDefenseMode();
	bool IsCTFCPHybrid() const;
	bool IsHuntedOrVIPMap() const;   // pf2: civilian escort maps
	bool CanBotChooseClass(CBasePlayer *pBot, int iDesiredClassIndex);
	bool CanBotChangeClass(CBasePlayer *pBot) { return true; }
	bool WouldChangeUnbalanceTeams(int iNewTeam, int iCurrentTeam);
	int GetAssignedHumanTeam() const { return TEAM_ANY; }
	CTeamRoundTimer *GetActiveRoundTimer();
	CTeamRoundTimer *GetKothTeamTimer(int iTeam) { return NULL; }
	class CRaidLogic *GetRaidLogic() const { return NULL; }
	void CollectCapturePoints(CBasePlayer *player, CUtlVector<CTeamControlPoint *> *captureVector) const;
	void CollectDefendPoints(CBasePlayer *player, CUtlVector<CTeamControlPoint *> *defendVector) const;
	bool HasMultipleTrains() const;   // payload race (PF2: m_bMultipleTrains on the round-based rules)
	CTeamTrainWatcher *GetPayloadToPush(int pushingTeam) const;
	CTeamTrainWatcher *GetPayloadToBlock(int blockingTeam) const;
	CUtlVector<CHandle<CBaseEntity> > &GetHealthEntityVector();
	CUtlVector<CHandle<CBaseEntity> > &GetAmmoEntityVector();
	CUtlVector<CHandle<CBaseEntity> > &GetArmorEntityVector();	// PF2NB: item_armor + resupply
	void HaveAllPlayersSpeakConceptIfAllowed(int, int = TEAM_UNASSIGNED, const char * = NULL) {}
	void BroadcastSound(int, const char *, int = 0) {}
	CBaseEntity *GetActiveBoss() { return NULL; }
	bool ShouldCollide(int c0, int c1) { return true; }
	void OnLevelInit();
	CUtlVector<CHandle<CBaseEntity> > m_healthVector, m_ammoVector, m_armorVector;
	float m_itemVectorTime = -1.0f;
};
CTFGameRules *TFGameRules();

class CTFObjectiveResource
{
public:
	int GetNumControlPoints();
	int GetOwningTeam(int index);
	bool IsCPVisible(int index);
	bool IsCPBlocked(int index) { return false; }
	bool TeamCanCapPoint(int index, int team);
	int GetCappingTeam(int index);
	float GetCPCapPercentage(int index);
	int GetPreviousPointForPoint(int index, int team, int iPrevIndex);
	bool PlayingMiniRounds() { return false; }
	bool IsInMiniRound(int index) { return true; }
	CBaseEntity *Ent();
	void SetBaseMvMBombUpgradeTime(float) {}
	void SetNextMvMBombUpgradeTime(float) {}
	void SetFlagCarrierUpgradeLevel(int) {}
};
CTFObjectiveResource *TFObjectiveResource();
#define ObjectiveResource TFObjectiveResource

#if !defined(_MSC_VER)
#pragma pop_macro("__declspec")
#endif
