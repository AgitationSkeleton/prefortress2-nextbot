// pf2-nextbot game layer: world entity proxies used by the nav code (doors, brushes).
#pragma once
#include "pf2_entity.h"
// PF2NB: the property accessors below need the __declspec keyword (MSVC; Clang with -fdeclspec).
// tier0/platform.h defines __declspec away on POSIX, so lift that macro for this header only.
#if !defined(_MSC_VER)
#pragma push_macro("__declspec")
#undef __declspec
#endif

class IFuncNavPrerequisiteAutoList
{
public:
	static const CUtlVector<IFuncNavPrerequisiteAutoList *> &AutoList() { static CUtlVector<IFuncNavPrerequisiteAutoList *> v; return v; }
};

class CFuncBrush : public CBaseEntity
{
public:
	enum BrushSolidities_e { BRUSHSOLID_TOGGLE = 0, BRUSHSOLID_NEVER = 1, BRUSHSOLID_ALWAYS = 2 };
	int GetSolidity() const { int *p = Field<int>("m_iSolidity"); return p ? *p : BRUSHSOLID_ALWAYS; }
	__declspec(property(get = GetSolidity)) int m_iSolidity;
};

enum TOGGLE_STATE { TS_AT_TOP, TS_AT_BOTTOM, TS_GOING_UP, TS_GOING_DOWN };

class CBaseToggle : public CBaseEntity
{
public:
	TOGGLE_STATE GetToggleState() const { int *p = Field<int>("m_toggle_state"); return p ? (TOGGLE_STATE)*p : TS_AT_BOTTOM; }
	__declspec(property(get = GetToggleState)) TOGGLE_STATE m_toggle_state;
};

class CBaseDoor : public CBaseToggle
{
public:
	bool IsRotatingDoor() const { return ClassMatches("func_door_rotating"); }
};

class CBasePropDoor : public CBaseEntity
{
public:
	bool IsDoorOpen() const { int *p = Field<int>("m_eDoorState"); return p && *p == 2; }
	bool IsDoorClosed() const { int *p = Field<int>("m_eDoorState"); return !p || *p == 0; }
	bool IsDoorOpening() const { int *p = Field<int>("m_eDoorState"); return p && *p == 1; }
	bool IsDoorClosing() const { int *p = Field<int>("m_eDoorState"); return p && *p == 3; }
	bool IsDoorLocked() const { bool *p = Field<bool>("m_bLocked"); return p && *p; }
};

class CBreakableProp : public CBaseEntity {};

class CTriggerMultiple : public CBaseEntity
{
public:
	bool GetDisabled() const { bool *p = Field<bool>("m_bDisabled"); return p && *p; }
	CBaseEntity *GetFilter() const { return HandleField("m_hFilter"); }
	__declspec(property(get = GetDisabled)) bool m_bDisabled;
	__declspec(property(get = GetFilter)) CBaseEntity *m_hFilter;
};
class CPhysicsProp : public CBreakableProp {};

// func_nav_prerequisite: not used by PF2 maps; areas carry no prerequisites
class CFuncNavPrerequisite : public CBaseEntity, public IFuncNavPrerequisiteAutoList
{
public:
	enum TaskType { TASK_NONE = 0, TASK_DESTROY_ENTITY = 1, TASK_MOVE_TO_ENTITY = 2, TASK_WAIT = 3 };
	bool IsTask(TaskType type) const { return false; }
	TaskType GetTask() const { return TASK_NONE; }
	CBaseEntity *GetTaskEntity(void) { return NULL; }
	float GetTaskValue(void) const { return 0.0f; }
	bool IsEnabled(void) const { return false; }
	bool PassesTriggerFilters(CBaseEntity *) { return false; }
};

// func_nav_* entities: PF2's server.dll has no nav system, so maps cannot contain working ones.
class CFuncNavCost : public CBaseEntity
{
public:
	virtual float GetCostMultiplier(CBaseCombatCharacter *who) const { return 1.0f; }
	bool IsApplicableTo(CBaseCombatCharacter *who) const { return false; }
	bool HasTag(const char *groupname) const { return false; }
};
class CFuncNavAvoid : public CFuncNavCost {};
class CFuncNavPrefer : public CFuncNavCost {};
class CFuncNavBlocker : public CBaseEntity
{
public:
	static bool CalculateBlocked(bool *pResultByTeam, const Vector &vecMins, const Vector &vecMaxs)
	{
		for (int i = 0; i < 2; ++i) pResultByTeam[i] = false;   // MAX_NAV_TEAMS
		return false;
	}
};
class CFuncNavObstruction : public CBaseEntity {};


class CWorld : public CBaseEntity
{
public:
	void GetWorldBounds(Vector &vecMins, Vector &vecMaxs)
	{
		Vector *mn = Field<Vector>("m_WorldMins"), *mx = Field<Vector>("m_WorldMaxs");
		vecMins = mn ? *mn : Vector(-16384, -16384, -16384);
		vecMaxs = mx ? *mx : Vector(16384, 16384, 16384);
	}
};

#if !defined(_MSC_VER)
#pragma pop_macro("__declspec")
#endif
