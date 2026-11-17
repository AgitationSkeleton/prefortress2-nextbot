// pf2-nextbot game layer: CTakeDamageInfo, built from PF2's player_hurt / player_death events.
#pragma once
#include "pf2_entity.h"

class CTakeDamageInfo
{
public:
	CTakeDamageInfo() {}
	CTakeDamageInfo(CBaseEntity *inflictor, CBaseEntity *attacker, float damage, int type, int custom = 0)
		: m_hInflictor(inflictor), m_hAttacker(attacker), m_flDamage(damage), m_bitsDamageType(type), m_iDamageCustom(custom) {}
	CBaseEntity *GetInflictor() const { return m_hInflictor.Get(); }
	CBaseEntity *GetAttacker() const { return m_hAttacker.Get(); }
	CBaseEntity *GetWeapon() const { return m_hWeapon.Get(); }
	float GetDamage() const { return m_flDamage; }
	int GetDamageType() const { return m_bitsDamageType; }
	int GetDamageCustom() const { return m_iDamageCustom; }
	const Vector &GetDamagePosition() const { return m_vecDamagePosition; }
	const Vector &GetReportedPosition() const { return m_vecDamagePosition; }
	const Vector &GetDamageForce() const { return m_vecDamageForce; }
	void SetWeapon(CBaseEntity *w) { m_hWeapon = w; }
	void SetDamagePosition(const Vector &p) { m_vecDamagePosition = p; }
	void SetDamageCustom(int c) { m_iDamageCustom = c; }
	EHANDLE m_hInflictor, m_hAttacker, m_hWeapon;
	float m_flDamage = 0.0f;
	int m_bitsDamageType = 0;
	int m_iDamageCustom = 0;
	Vector m_vecDamagePosition = vec3_origin;
	Vector m_vecDamageForce = vec3_origin;
};
