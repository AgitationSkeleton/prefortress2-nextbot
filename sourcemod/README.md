# pf2_hunted_classrestrict

Optional SourceMod plugin for a PF2 dedicated server: enforces the class rules of hunted/VIP maps for
humans (and, harmlessly, bots). It is independent of the NextBot plugin; the bots already follow the same
rules from `addons/pf2nextbot/data/hunted.cfg`.

| File | Install to |
|---|---|
| `pf2_hunted_classrestrict.smx` (built by CI, in the release zips) | `pf2/addons/sourcemod/plugins/` |
| `configs/hunted_classrestrict.cfg` (optional overrides) | `pf2/addons/sourcemod/configs/` |
| `scripting/pf2_hunted_classrestrict.sp` | source |

Needs SourceMod core + sdktools (1.11+). For an instant respawn it uses `CTFPlayer::ForceRespawn` from a
`gamedata/pf2.txt` file if one is installed (PF2 server tool plugins commonly ship it; vtable 326 on
Windows, the same index the NextBot plugin uses); without it, it falls back to a suicide and the player
respawns as the new class.

To compile it yourself: `spcomp scripting/pf2_hunted_classrestrict.sp` with SourceMod 1.12's compiler.

## How the rules are derived

At map start, from the live entities, the same way as `tools/hunted_rules.py` does from the BSP:

1. Every enabled `trigger_hurt` with `m_flDamage > 0` gives a box: `m_vecAbsOrigin + m_vecMins/Maxs`.
2. Every enabled `info_player_teamspawn` on RED/BLU: if its origin is inside a box (16 u padding),
   it is a cage spawn; otherwise its `m_spawnflags & 0x3FF` class mask (bit = class-1, 0 = any) is
   added to the team's allowed set.
3. The map is restricted only if it has at least one cage spawn. Otherwise nothing is enforced.
4. `configs/hunted_classrestrict.cfg` may replace a team's list or switch a map off.
5. On a restricted map the Civilian is removed from RED, and BLU may have one human Civilian (the VIP).
   PF2 itself does not cap the Civilian (`pf_allow_special_class` only enables it outside escort
   modes), so this does not fight the game. Bots are not capped here: the NextBot plugin already makes
   exactly one BLU bot the VIP and makes it step down for a human Civilian.

Derived rules for PF2's stock hunted maps (identical to `hunted.cfg`):

| Map | RED | BLU | cage spawns R/B |
|---|---|---|---|
| hunted_accurate | sniper | soldier medic heavyweapons civilian | 5 / 5 |
| hunted_actually_accurate_a1 | sniper | soldier medic heavyweapons civilian | 5 / 5 |
| hunted_actually_accurate_a2 | sniper | soldier medic heavyweapons civilian | 5 / 5 |
| hunted_biblically_accurate_a3 | sniper spy | all | 5 / 0 |
| hunted_stonehenge_test | not restricted (no cages) | | 0 / 0 |
| ad_hunted | not restricted (no cages) | | 0 / 0 |

## Enforcement

* `joinclass` / `join_class` from a human for a disallowed class is refused with a chat message that
  lists the allowed classes, and the class menu is reopened. `joinclass random|auto` becomes a random
  allowed class.
* On `player_spawn` (checked one frame later) anyone playing a disallowed class gets
  `m_iDesiredPlayerClass` set to a random allowed class (never the Civilian) and is respawned at once.
  A player forced more than 3 times in 10 s is left alone and logged (another plugin fighting back).

## Commands and cvars

| Name | |
|---|---|
| `sm_huntedclass_enabled 1` | master switch |
| `sm_huntedclass_status` (admin, generic) | derived rules for this map + every player's class and whether it is allowed |
| `sm_huntedclass_rederive` (admin, config) | derive again (after editing the override file) |
| `sm_huntedclass_test <player> <class>` (admin, cheats) | test aid: says what `joinclass` would do, then respawns the player as that class so the spawn check can be watched |

The rule line for each map is written to the SourceMod log at map start.

## Together with classrestrict.smx

Many PF2 servers run the generic `classrestrict.smx` (configured per map by `cfg/<map>.cfg` via
`sm_classrestrict_*` cvars). Its cvars are not reset between maps, so a map without its own cfg
inherits the previous map's limits.

The two plugins cannot loop: a class one refuses is replaced by a class both accept whenever their
allowed sets overlap (classrestrict picks a random class it allows; this plugin picks one it allows and
gives up after 3 forced respawns). One second after the map configs run, if `classrestrict.smx` is
running and enabled, this plugin compares its cvars to its own rules and logs a `note:` when they
differ, and `NO CLASS SATISFIES BOTH` when the sets are disjoint. Tested: overlapping limits converge
in one extra respawn; disjoint limits stop after 3 forced respawns with `giving up on ... another class
plugin?` and the warning. Recommended: let this plugin own the hunted maps and put
`sm_classrestrict_enabled 0` in those map cfgs (or keep the classrestrict limits identical).
