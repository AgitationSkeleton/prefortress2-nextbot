// pf2-nextbot game layer: TF2 bot map entities (bot_proxy, bot_generator, bot_action_point,
// func_tfbot_hint, bot_hint_*). PF2 maps cannot contain them (the entities do not exist in PF2's
// server.dll), so these are inert stand-ins with the members TFBot references.
#pragma once
#include "cbase.h"
class CTFBot;

class CTFBotProxy : public CBaseEntity
{
public:
	void OnInjured() {}
	void OnKilled() {}
	void OnAttackingEnemy() {}
	void OnKilledEnemy() {}
	void OnSpawned() {}
};

class CTFBotActionPoint : public CBaseEntity
{
public:
	bool IsWithinRange(CBaseEntity *entity) { return false; }
	void ReachedActionPoint(CTFBot *bot) {}
	CHandle<CBaseEntity> m_moveGoal;
	float m_stayTime = 0.0f;
	float m_desiredDistance = 0.0f;
	int m_stayFlags = 0;
	string_t m_command = NULL_STRING;
};

class CTFBotGenerator : public CBaseEntity
{
public:
	void OnBotKilled(CTFBot *pBot) {}
};

class CBaseTFBotHintEntity : public CBaseEntity
{
public:
	enum HintType { HINT_TELEPORTER_EXIT, HINT_SENTRYGUN, HINT_ENGINEER_NEST, HINT_SNIPER_SPOT };
	bool IsEnabled() const { return false; }
	bool OwnerObjectHasNoOwner() const { return true; }
	bool OwnerObjectFinishBuilding() const { return false; }
	virtual HintType GetHintType() const { return HINT_SNIPER_SPOT; }
	bool IsA(HintType type) const { return GetHintType() == type; }
};

class CTFBotHint : public CBaseTFBotHintEntity
{
public:
	bool IsEnabled() const { return false; }
	bool IsFor(CTFBot *) const { return false; }
	void SetOwnerEntity(CBaseEntity *) {}
};

class CTFBotHintSentrygun : public CBaseTFBotHintEntity
{
public:
	bool IsAvailableForSelection(CTFPlayer *pRequestingPlayer) const { return false; }
	CTFPlayer *GetPlayerOwner() const { return NULL; }
	void SetPlayerOwner(CTFPlayer *) {}
	bool IsSticky() const { return false; }
	void SetOwnerEntity(CBaseEntity *) {}
	void InputDisable(struct inputdata_t &) {}
	void IncrementUseCount() {}
	void DecrementUseCount() {}
	HintType GetHintType() const override { return HINT_SENTRYGUN; }
};

class CTFBotHintTeleporterExit : public CBaseTFBotHintEntity
{
public:
	HintType GetHintType() const override { return HINT_TELEPORTER_EXIT; }
	void SetOwnerEntity(CBaseEntity *) {}
};

class CTFBotHintEngineerNest : public CBaseTFBotHintEntity
{
public:
	HintType GetHintType() const override { return HINT_ENGINEER_NEST; }
	bool IsStaleNest() const { return true; }
	void DetonateStaleNest() {}
	CTFBotHintSentrygun *GetSentryHint() const { return NULL; }
	CTFBotHintTeleporterExit *GetTeleporterHint() const { return NULL; }
};
