// pf2-nextbot game layer: combat character, weapon and player proxies.
#include "cbase.h"
#include "nav_mesh.h"
#include "game/vfunc.h"
#include "game/pf2_offsets.h"

using namespace pf2;

// ---------------------------------------------------------------------------------------------
// CBaseCombatWeapon
// ---------------------------------------------------------------------------------------------
CBaseCombatCharacter *CBaseCombatWeapon::GetOwner() const
{
	CBaseEntity *o = HandleField("m_hOwner");
	return o ? o->MyCombatCharacterPointer() : NULL;
}
int CBaseCombatWeapon::Clip1() const { int *p = Field<int>("m_iClip1"); return p ? *p : -1; }
int CBaseCombatWeapon::Clip2() const { int *p = Field<int>("m_iClip2"); return p ? *p : -1; }
int CBaseCombatWeapon::GetPrimaryAmmoType() const { int *p = Field<int>("m_iPrimaryAmmoType"); return p ? *p : -1; }
int CBaseCombatWeapon::GetSecondaryAmmoType() const { int *p = Field<int>("m_iSecondaryAmmoType"); return p ? *p : -1; }

bool CBaseCombatWeapon::HasPrimaryAmmo()
{
	if (Clip1() > 0)
		return true;
	int type = GetPrimaryAmmoType();
	if (type < 0)
		return Clip1() != 0;   // melee etc.
	CBaseCombatCharacter *owner = GetOwner();
	return owner ? owner->GetAmmoCount(type) > 0 : false;
}

bool CBaseCombatWeapon::HasAmmo() { return HasPrimaryAmmo(); }

float CBaseCombatWeapon::m_flNextPrimaryAttackValue() const { float *p = Field<float>("m_flNextPrimaryAttack"); return p ? *p : 0.0f; }

// ---------------------------------------------------------------------------------------------
// CBaseCombatCharacter
// ---------------------------------------------------------------------------------------------
CBaseCombatWeapon *CBaseCombatCharacter::GetActiveWeapon() const
{
	CBaseEntity *w = HandleField("m_hActiveWeapon");
	return w ? w->MyCombatWeaponPointer() : NULL;
}

CBaseCombatWeapon *CBaseCombatCharacter::GetWeapon(int i) const
{
	if (i < 0 || i >= MAX_WEAPONS)
		return NULL;
	CBaseHandle *arr = Field<CBaseHandle>("m_hMyWeapons");
	if (!arr)
		return NULL;
	CBaseEntity *w = PF2_EntityFromHandle(arr[i]);
	return w ? w->MyCombatWeaponPointer() : NULL;
}

CBaseCombatWeapon *CBaseCombatCharacter::Weapon_GetSlot(int slot) const
{
	void *raw = GetRaw();
	if (!raw)
		return NULL;
	void *w = VCall<void *>(raw, vt::Weapon_GetSlot, slot);
	CBaseEntity *e = PF2_EntityFromRaw(w);
	return e ? e->MyCombatWeaponPointer() : NULL;
}

bool CBaseCombatCharacter::Weapon_Switch(CBaseCombatWeapon *pWeapon, int viewmodelindex)
{
	void *raw = GetRaw();
	void *wraw = pWeapon ? pWeapon->GetRaw() : NULL;
	if (!raw || !wraw)
		return false;
	return VCall<bool>(raw, vt::Weapon_Switch, wraw, viewmodelindex);
}

Vector CBaseCombatCharacter::Weapon_ShootPosition()
{
	void *raw = GetRaw();
	return raw ? VCall<Vector>(raw, vt::Weapon_ShootPosition) : vec3_origin;
}

int CBaseCombatCharacter::GetAmmoCount(int iAmmoIndex) const
{
	if (iAmmoIndex < 0 || iAmmoIndex >= MAX_AMMO_SLOTS)
		return 0;
	int *arr = Field<int>("m_iAmmo");
	return arr ? arr[iAmmoIndex] : 0;
}

bool CBaseCombatCharacter::IsAreaTraversable(const CNavArea *area) const
{
	return area && !area->IsBlocked(GetTeamNumber());
}

void CBaseCombatCharacter::ClearLastKnownArea()
{
	OnNavAreaChanged(NULL, m_lastKnownArea);
	m_lastKnownArea = NULL;
}

void CBaseCombatCharacter::UpdateLastKnownArea()
{
	if (!TheNavMesh || !TheNavMesh->IsLoaded())
		return;
	CNavArea *area = TheNavMesh->GetNearestNavArea(GetAbsOrigin(), true, 50.0f, false, true, GetTeamNumber());
	if (area && area != m_lastKnownArea)
	{
		if (!IsAreaTraversable(area))
			return;
		CNavArea *old = m_lastKnownArea;
		m_lastKnownArea = area;
		OnNavAreaChanged(area, old);
	}
}

bool CBaseCombatCharacter::IsLookingTowards(const CBaseEntity *target, float cosTolerance) const
{
	return target && IsLookingTowards(const_cast<CBaseEntity *>(target)->WorldSpaceCenter(), cosTolerance);
}

bool CBaseCombatCharacter::IsLookingTowards(const Vector &target, float cosTolerance) const
{
	CBaseCombatCharacter *self = const_cast<CBaseCombatCharacter *>(this);
	Vector toTarget = target - self->EyePosition();
	toTarget.NormalizeInPlace();
	Vector forward;
	AngleVectors(self->EyeAngles(), &forward);
	return DotProduct(forward, toTarget) >= cosTolerance;
}

bool CBaseCombatCharacter::IsInFieldOfView(CBaseEntity *entity) const
{
	return entity && IsInFieldOfView(entity->WorldSpaceCenter());
}

bool CBaseCombatCharacter::IsInFieldOfView(const Vector &pos) const
{
	return IsLookingTowards(pos, 0.0f);   // 180 degree horizontal FOV as in CBaseCombatCharacter
}

bool CBaseCombatCharacter::IsLineOfSightClear(CBaseEntity *entity, int) const
{
	if (!entity)
		return false;
	CBaseCombatCharacter *self = const_cast<CBaseCombatCharacter *>(this);
	trace_t tr;
	CTraceFilterSkipTwoEntities filter(PF2_HandleEntity(self), PF2_HandleEntity(entity), COLLISION_GROUP_NONE);
	UTIL_TraceLine(self->EyePosition(), entity->WorldSpaceCenter(), MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE, &filter, &tr);
	if (tr.fraction >= 1.0f)
		return true;
	UTIL_TraceLine(self->EyePosition(), entity->EyePosition(), MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE, &filter, &tr);
	return tr.fraction >= 1.0f;
}

bool CBaseCombatCharacter::IsLineOfSightClear(const Vector &pos, int, CBaseEntity *entityToIgnore) const
{
	CBaseCombatCharacter *self = const_cast<CBaseCombatCharacter *>(this);
	trace_t tr;
	CTraceFilterSkipTwoEntities filter(PF2_HandleEntity(self), PF2_HandleEntity(entityToIgnore), COLLISION_GROUP_NONE);
	UTIL_TraceLine(self->EyePosition(), pos, MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE, &filter, &tr);
	return tr.fraction >= 1.0f;
}

bool CBaseCombatCharacter::IsAbleToSee(const CBaseEntity *entity, FieldOfViewCheckType checkFOV)
{
	CBaseEntity *e = const_cast<CBaseEntity *>(entity);
	if (!e)
		return false;
	if (checkFOV == USE_FOV && !IsInFieldOfView(e))
		return false;
	return IsLineOfSightClear(e);
}

bool CBaseCombatCharacter::IsAbleToSee(CBaseCombatCharacter *pBCC, FieldOfViewCheckType checkFOV)
{
	return IsAbleToSee((const CBaseEntity *)pBCC, checkFOV);
}

bool CBaseCombatCharacter::FVisible(CBaseEntity *pEntity, int traceMask, CBaseEntity **ppBlocker)
{
	if (!pEntity)
		return false;
	trace_t tr;
	UTIL_TraceLine(EyePosition(), pEntity->EyePosition(), traceMask, this, COLLISION_GROUP_NONE, &tr);
	if (tr.fraction != 1.0f && tr.m_pEnt != pEntity)
	{
		if (ppBlocker) *ppBlocker = tr.m_pEnt;
		return false;
	}
	return true;
}

float CBaseCombatCharacter::GetTimeSinceLastInjury(int) const
{
	return m_lastInjuryTime < 0 ? 99999.9f : gpGlobals->curtime - m_lastInjuryTime;
}

// ---------------------------------------------------------------------------------------------
// CBasePlayer
// ---------------------------------------------------------------------------------------------
IPlayerInfo *CBasePlayer::GetPlayerInfo() const
{
	edict_t *ed = edict();
	return ed && GetRaw() ? playerinfomanager->GetPlayerInfo(ed) : NULL;
}

const char *CBasePlayer::GetPlayerName() const
{
	IPlayerInfo *info = GetPlayerInfo();
	return info ? info->GetName() : "";
}

int CBasePlayer::GetUserID() const
{
	edict_t *ed = edict();
	return ed ? engine->GetPlayerUserId(ed) : -1;
}

bool CBasePlayer::IsFakeClient() const { return (GetFlags() & FL_FAKECLIENT) != 0; }
bool CBasePlayer::IsHLTV() const { IPlayerInfo *i = GetPlayerInfo(); return i && i->IsHLTV(); }

void CBasePlayer::EyeVectors(Vector *pForward, Vector *pRight, Vector *pUp)
{
	AngleVectors(EyeAngles(), pForward, pRight, pUp);
}

void CBasePlayer::EyePositionAndVectors(Vector *pPosition, Vector *pForward, Vector *pRight, Vector *pUp)
{
	*pPosition = EyePosition();
	EyeVectors(pForward, pRight, pUp);
}

void CBasePlayer::SnapEyeAngles(const QAngle &viewAngles) { SetDesiredView(viewAngles); }

const QAngle &CBasePlayer::GetPunchAngle()
{
	QAngle *p = Field<QAngle>("m_vecPunchAngle");
	m_punchCache = p ? *p : QAngle(0, 0, 0);
	return m_punchCache;
}

float CBasePlayer::MaxSpeed() const { float *p = Field<float>("m_flMaxspeed"); return p ? *p : 0.0f; }
int CBasePlayer::GetObserverMode() const { int *p = Field<int>("m_iObserverMode"); return p ? *p : 0; }
CBaseEntity *CBasePlayer::GetObserverTarget() const { return HandleField("m_hObserverTarget"); }
float CBasePlayer::GetDeathTime() const { float *p = Field<float>("m_flDeathTime"); return p ? *p : 0.0f; }
bool CBasePlayer::IsDucked() const { bool *p = Field<bool>("m_bDucked"); return p && *p; }
bool CBasePlayer::IsDucking() const { bool *p = Field<bool>("m_bDucking"); return p && *p; }

const Vector &CBasePlayer::GetPlayerMins() const
{
	void *raw = GetRaw();
	static Vector zero(0, 0, 0);
	return raw ? VCall<const Vector &>(raw, vt::GetPlayerMins) : zero;
}

const Vector &CBasePlayer::GetPlayerMaxs() const
{
	void *raw = GetRaw();
	static Vector zero(0, 0, 0);
	return raw ? VCall<const Vector &>(raw, vt::GetPlayerMaxs) : zero;
}

CPlayerLocalDataProxy &CBasePlayer::LocalData()
{
	if (float *p = Field<float>("m_flStepSize")) m_Local.m_flStepSize = *p;
	m_Local.m_bDucked = IsDucked();
	m_Local.m_bDucking = IsDucking();
	if (float *p = Field<float>("m_flFallVelocity")) m_Local.m_flFallVelocity = *p;
	m_Local.m_vecPunchAngle = GetPunchAngle();
	return m_Local;
}

void CBasePlayer::ClientCommand(const char *cmd)
{
	edict_t *ed = edict();
	if (!ed || !GetRaw())
		return;
	CCommand args;
	args.Tokenize(cmd);
	gameclients->ClientCommand(ed, args);
}

void CBasePlayer::ChangeTeam(int iTeamNum)
{
	const char *team = iTeamNum == TF_TEAM_RED ? "red" : iTeamNum == TF_TEAM_BLUE ? "blue" : "spectate";
	ClientCommand(UTIL_VarArgs("jointeam %s", team));
}

void CBasePlayer::CommitSuicide(bool bExplode, bool)
{
	ClientCommand(bExplode ? "explode" : "kill");
}

void CBasePlayer::ProcessUsercmds(CUserCmd *cmds, int numcmds, int, int, bool)
{
	if (numcmds > 0 && cmds)
	{
		m_pendingCmd = cmds[0];
		m_hasPendingCmd = true;
	}
	m_nSimulationTick = gpGlobals->tickcount;
}

bool CBasePlayer::TakePendingCmd(CUserCmd *out)
{
	if (!m_hasPendingCmd)
		return false;
	*out = m_pendingCmd;
	m_hasPendingCmd = false;
	return true;
}

edict_t *CBasePlayer::s_PlayerEdict = NULL;

bool CBaseMultiplayerPlayer::SpeakConceptIfAllowed(int, const char *, char *, size_t, IRecipientFilter *)
{
	return false;   // voice commands are not issued by the bots
}
