// pf2-nextbot: vtable slots in PF2's server binary (CTFPlayer and its bases, plus a few weapon/object
// slots), per platform. Derived from the unstripped Linux server.so with tools/vtdump.py. GCC emits
// two destructor slots where MSVC emits one, so for every entry here the Linux slot is the Windows
// slot + 1; each Linux value below was read directly from server.so (not computed).
// Windows values cross-checked against SourceMod's sdk2013 gamedata (GetDataDescMap 11, EyeAngles 131,
// ForceRespawn 326, PlayerRunCommand 419).
// Game build: PF2 server.dll / server.so of 2026-02-16 (server.dll SHA256 716dc12b...0198).
// The plugin validates the PlayerRunCommand hook at runtime and refuses to drive bots if it never fires.
#pragma once

#if defined(_WIN32)
#define PF2_VTSLOT(w, l) (w)
#else
#define PF2_VTSLOT(w, l) (l)
#endif

namespace pf2 { namespace vt {
enum
{
	//                                  Windows  Linux
	GetRefEHandle        = PF2_VTSLOT(    2,      3),
	GetCollideable       = PF2_VTSLOT(    3,      4),
	GetServerClass       = PF2_VTSLOT(    9,     10),
	GetDataDescMap       = PF2_VTSLOT(   11,     12),
	ShouldCollide        = PF2_VTSLOT(   16,     17),   // CBaseEntity::ShouldCollide(int collisionGroup, int contentsMask) const
	Spawn                = PF2_VTSLOT(   22,     23),
	Classify             = PF2_VTSLOT(   53,     54),
	IsAlive              = PF2_VTSLOT(   65,     66),
	MyCombatCharacter    = PF2_VTSLOT(   71,     72),
	IsPlayer             = PF2_VTSLOT(   80,     81),
	IsBaseObject         = PF2_VTSLOT(   83,     84),
	Teleport             = PF2_VTSLOT(  108,    109),
	GetMaxHealth         = PF2_VTSLOT(  117,    118),
	EyePosition          = PF2_VTSLOT(  130,    131),
	EyeAngles            = PF2_VTSLOT(  131,    132),
	WorldSpaceCenter     = PF2_VTSLOT(  149,    150),
	Weapon_Switch        = PF2_VTSLOT(  265,    266),
	Weapon_ShootPosition = PF2_VTSLOT(  266,    267),
	Weapon_CanSwitchTo   = PF2_VTSLOT(  267,    268),
	Weapon_GetSlot       = PF2_VTSLOT(  269,    270),
	GetPlayerMins        = PF2_VTSLOT(  339,    340),
	GetPlayerMaxs        = PF2_VTSLOT(  340,    341),
	ForceRespawn         = PF2_VTSLOT(  326,    327),
	PlayerRunCommand     = PF2_VTSLOT(  419,    420),
	CommitSuicide        = PF2_VTSLOT(  439,    440),   // CTFPlayer::CommitSuicide(bool bExplode, bool bForce)

	// CBaseObject
	Obj_IsPlacementPosValid = PF2_VTSLOT(336,    337),

	// CBaseCombatWeapon / CTFWeaponBase
	Wpn_GetSlot          = PF2_VTSLOT(  320,    321),   // CBaseCombatWeapon::GetSlot() const
	Wpn_GetWeaponID      = PF2_VTSLOT(  368,    369),   // CTFWeaponBase::GetWeaponID() const
};
} }
