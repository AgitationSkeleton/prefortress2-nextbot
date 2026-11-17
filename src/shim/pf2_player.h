// pf2-nextbot game layer: combat character, weapon and player proxies.
#pragma once
#include "pf2_entity.h"

class CNavArea;
class INextBot;
class CBaseCombatWeapon;


// --------------------------------------------------------------------------------------------
class CBaseCombatWeapon : public CBaseEntity
{
public:
	DECLARE_CLASS(CBaseCombatWeapon, CBaseEntity);
	bool IsBaseCombatWeapon() const override { return true; }
	CBaseCombatWeapon *MyCombatWeaponPointer() override { return this; }
	CBaseCombatCharacter *GetOwner() const;
	int Clip1() const;
	int Clip2() const;
	int GetPrimaryAmmoType() const;
	int GetSecondaryAmmoType() const;
	bool HasAmmo();
	bool HasPrimaryAmmo();
	bool UsesClipsForAmmo1() const { return Clip1() >= 0; }
	float m_flNextPrimaryAttackValue() const;
	const char *GetName() const { return GetClassname(); }
	int GetSlot() const;
	bool IsMeleeWeapon() const;
};

// --------------------------------------------------------------------------------------------
class CBaseCombatCharacter : public CBaseEntity
{
public:
	DECLARE_CLASS(CBaseCombatCharacter, CBaseEntity);
	enum FieldOfViewCheckType { USE_FOV, DISREGARD_FOV };
	enum LineOfSightCheckType { IGNORE_NOTHING, IGNORE_ACTORS };
	void OnPursuedBy(INextBot *pPursuer) {}
	void OnNavAreaRemoved(CNavArea *removedArea) override { if (m_lastKnownArea == removedArea) ClearLastKnownArea(); }
	CBaseCombatCharacter *MyCombatCharacterPointer() override { return this; }

	CBaseCombatWeapon *GetActiveWeapon() const;
	CBaseCombatWeapon *GetWeapon(int i) const;
	int WeaponCount() const { return MAX_WEAPONS; }
	CBaseCombatWeapon *Weapon_GetSlot(int slot) const;
	bool Weapon_Switch(CBaseCombatWeapon *pWeapon, int viewmodelindex = 0);
	Vector Weapon_ShootPosition();
	int GetAmmoCount(int iAmmoIndex) const;

	// nav
	virtual CNavArea *GetLastKnownArea() const { return m_lastKnownArea; }
	virtual bool IsAreaTraversable(const CNavArea *area) const;
	virtual void ClearLastKnownArea();
	virtual void UpdateLastKnownArea();
	virtual void OnNavAreaChanged(CNavArea *enteredArea, CNavArea *leftArea) {}

	// visibility (CBaseCombatCharacter::IsAbleToSee etc. from the SDK, simplified: no fog)
	bool IsHiddenByFog(const Vector &target) const { return false; }
	bool IsHiddenByFog(CBaseEntity *target) const { return false; }
	bool IsHiddenByFog(float range) const { return false; }
	float GetFogObscuredRatio(const Vector &target) const { return 0.0f; }
	float GetFogObscuredRatio(CBaseEntity *target) const { return 0.0f; }
	float GetFogObscuredRatio(float range) const { return 0.0f; }
	bool IsLookingTowards(const CBaseEntity *target, float cosTolerance = 0.9f) const;
	bool IsLookingTowards(const Vector &target, float cosTolerance = 0.9f) const;
	bool IsInFieldOfView(CBaseEntity *entity) const;
	bool IsInFieldOfView(const Vector &pos) const;
	bool IsLineOfSightClear(CBaseEntity *entity, int checkType = 0) const;
	bool IsLineOfSightClear(const Vector &pos, int checkType = 0, CBaseEntity *entityToIgnore = NULL) const;
	bool IsAbleToSee(const CBaseEntity *entity, FieldOfViewCheckType checkFOV);
	bool IsAbleToSee(CBaseCombatCharacter *pBCC, FieldOfViewCheckType checkFOV);
	virtual bool FVisible(CBaseEntity *pEntity, int traceMask = MASK_BLOCKLOS, CBaseEntity **ppBlocker = NULL);
	bool IsInAVehicle() const { return false; }
	IServerVehicle *GetVehicle() { return NULL; }
	float GetTimeSinceLastInjury(int team = -1) const;
	void SetTimeOfLastInjury(float t) { m_lastInjuryTime = t; }
protected:
	float m_lastInjuryTime = -1.0f;
};

// Mirrors PF2's CUserCmd layout (pf2-src game/shared/usercmd.h) up to hasbeenpredicted.
class CUserCmd
{
public:
	void   *__vtable;
	int     command_number;
	int     tick_count;
	QAngle  viewangles;
	float   forwardmove;
	float   sidemove;
	float   upmove;
	int     buttons;
	byte    impulse;
	int     weaponselect;
	int     weaponsubtype;
	int     random_seed;
	int     server_random_seed;
	short   mousedx;
	short   mousedy;
	bool    hasbeenpredicted;
};

// --------------------------------------------------------------------------------------------
class CPlayerLocalDataProxy
{
public:
	float m_flStepSize = 18.0f;
	bool m_bDucked = false;
	bool m_bDucking = false;
	bool m_bInDuckJump = false;
	float m_flFallVelocity = 0.0f;
	QAngle m_vecPunchAngle;
};

class CBasePlayer : public CBaseCombatCharacter
{
public:
	DECLARE_CLASS(CBasePlayer, CBaseCombatCharacter);
	bool IsPlayer() const override { return true; }
	bool IsNetClient() const override { return !IsFakeClient(); }

	const char *GetPlayerName() const;
	void SetPlayerName(const char *name) {}
	int GetUserID() const;
	virtual bool IsBot() const { return IsFakeClient(); }
	bool IsFakeClient() const;
	bool IsConnected() const { return GetRaw() != NULL; }
	bool IsDead() { return !IsAlive(); }
	bool IsHLTV() const;
	bool IsReplay() const { return false; }
	IPlayerInfo *GetPlayerInfo() const;

	void EyeVectors(Vector *pForward, Vector *pRight = NULL, Vector *pUp = NULL);
	void EyePositionAndVectors(Vector *pPosition, Vector *pForward, Vector *pRight, Vector *pUp);
	virtual void SnapEyeAngles(const QAngle &viewAngles);   // bots: becomes this frame's cmd.viewangles
	const QAngle &GetPunchAngle();
	float MaxSpeed() const;
	int GetObserverMode() const;
	CBaseEntity *GetObserverTarget() const;
	bool IsObserver() const { return GetObserverMode() != 0; }
	bool IsOnLadder() const { return GetMoveType() == MOVETYPE_LADDER; }
	// CTFPlayer::PlayerSolidMask: the enemy team's contents bit makes enemy spawn barriers solid
	unsigned int PlayerSolidMask(bool brushOnly = false) const
	{
		unsigned int team = GetTeamNumber() == 2 ? CONTENTS_TEAM2 : (GetTeamNumber() == 3 ? CONTENTS_TEAM1 : 0);
		return team | (brushOnly ? MASK_PLAYERSOLID_BRUSHONLY : MASK_PLAYERSOLID);
	}
	const Vector &GetPlayerMins() const;
	const Vector &GetPlayerMaxs() const;
	bool IsDucked() const;
	bool IsDucking() const;
	float GetDeathTime() const;
	void AddFlag(int flags) {}
	void ClearFlags() {}
	bool IsSuitEquipped() const { return false; }
	void ShowViewPortPanel(const char *, bool = true, KeyValues * = NULL) {}
	Vector GetGroundNormal() const { return Vector(0, 0, 1); }
	CPlayerLocalDataProxy &LocalData();
	CPlayerLocalDataProxy m_Local;   // refreshed by LocalData()
	void ChangeTeam(int iTeamNum);   // via the game's own jointeam command
	virtual void ChangeTeam(int iTeamNum, bool bAutoTeam, bool bSilent, bool bAutoBalance = false) { ChangeTeam(iTeamNum); }
	int GetFOV() const { int *p = Field<int>("m_iFOV"); return (p && *p > 0) ? *p : 90; }
	const char *GetNetworkIDString() { edict_t *ed = edict(); return ed ? engine->GetPlayerNetworkIDString(ed) : ""; }
	int GiveAmmo(int iCount, int iAmmoIndex, bool bSuppressSound = false) { return 0; }   // the bots get no free ammo
	void SetAutoReload(bool b) { edict_t *ed = edict(); if (ed) engine->SetFakeClientConVarValue(ed, "cl_autoreload", b ? "1" : "0"); }
	static edict_t *s_PlayerEdict;
	void CommitSuicide(bool bExplode = false, bool bForce = false);
	void ClientCommand(const char *cmd);   // runs the game's ClientCommand for this player
	// Entity virtuals that NextBotPlayer<> overrides in a game DLL. Here they are plain hooks:
	// the bot glue calls Spawn/Event_Killed/OnTakeDamage_Alive from PF2's game events and
	// PhysicsSimulate from the PlayerRunCommand hook; ProcessUsercmds captures the built command.
	virtual void Spawn() {}
	virtual int DrawDebugTextOverlays() { return 0; }
	virtual void PhysicsSimulate() {}
	virtual int OnTakeDamage_Alive(const CTakeDamageInfo &) { return 0; }
	virtual int OnTakeDamage_Dying(const CTakeDamageInfo &) { return 0; }
	virtual void Event_Killed(const CTakeDamageInfo &) {}
	virtual void Touch(CBaseEntity *) {}
	virtual void Weapon_Equip(CBaseCombatWeapon *) {}
	virtual void Weapon_Drop(CBaseCombatWeapon *, const Vector *, const Vector *) {}
	virtual void HandleAnimEvent(struct animevent_t *) {}
	virtual CBaseEntity *EntSelectSpawnPoint() { return NULL; }
	const trace_t &GetTouchTrace() { static trace_t t; return t; }
	void ProcessUsercmds(CUserCmd *cmds, int numcmds, int totalcmds, int dropped_packets, bool paused);
	bool TakePendingCmd(CUserCmd *out);
	int m_nSimulationTick = -1;

	void SetDesiredView(const QAngle &a) { m_desiredView = a; m_hasDesiredView = true; }
	bool GetDesiredView(QAngle *a) const { if (m_hasDesiredView) *a = m_desiredView; return m_hasDesiredView; }
protected:
	CUserCmd m_pendingCmd;
	bool m_hasPendingCmd = false;
	QAngle m_desiredView;
	bool m_hasDesiredView = false;
	mutable QAngle m_punchCache;
};

class CBaseMultiplayerPlayer : public CBasePlayer
{
public:
	DECLARE_CLASS(CBaseMultiplayerPlayer, CBasePlayer);
	bool SpeakConceptIfAllowed(int iConcept, const char *modifiers = NULL, char *pszOutResponseChosen = NULL, size_t bufsize = 0, IRecipientFilter *filter = NULL);
};

inline CBaseCombatCharacter *ToBaseCombatCharacter(CBaseEntity *pEntity)
{
	return pEntity ? pEntity->MyCombatCharacterPointer() : NULL;
}
typedef CBaseCombatCharacter::FieldOfViewCheckType FieldOfViewCheckType;

inline CBasePlayer *ToBasePlayer(CBaseEntity *pEntity)
{
	return (pEntity && pEntity->IsPlayer()) ? static_cast<CBasePlayer *>(pEntity) : NULL;
}

// ForEachPlayer / CollectPlayers (game/server/util.h)
template <typename Functor>
bool ForEachPlayer(Functor &func)
{
	extern CBasePlayer *UTIL_PlayerByIndex(int);
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBasePlayer *player = UTIL_PlayerByIndex(i);
		if (player == NULL || !player->IsConnected())
			continue;
		if (func(player) == false)
			return false;
	}
	return true;
}

#define COLLECT_ONLY_LIVING_PLAYERS true
#define APPEND_PLAYERS true
template <typename T>
int CollectPlayers(CUtlVector<T *> *playerVector, int team = TEAM_ANY, bool isAlive = false, bool shouldAppend = false)
{
	extern CBasePlayer *UTIL_PlayerByIndex(int);
	if (!shouldAppend)
		playerVector->RemoveAll();
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBasePlayer *player = UTIL_PlayerByIndex(i);
		if (player == NULL || !player->IsConnected())
			continue;
		if (team != TEAM_ANY && player->GetTeamNumber() != team)
			continue;
		if (isAlive && !player->IsAlive())
			continue;
		playerVector->AddToTail(assert_cast<T *>(player));
	}
	return playerVector->Count();
}
