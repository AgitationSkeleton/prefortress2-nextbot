# SourceMod plugins

Two optional, independent SourceMod plugins for a PF2 dedicated server. Neither is needed by the
NextBot plugin; both ship in the release zips (delete the ones you do not want).

* [pf2_hunted_classrestrict](#pf2_hunted_classrestrict): enforces the class rules of hunted/VIP maps.
* [pf2_spawnfix](#pf2_spawnfix): keeps players (in practice Civilians) out of the enemy spawn room.

## pf2_hunted_classrestrict

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

### How the rules are derived

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

### Enforcement

* `joinclass` / `join_class` from a human for a disallowed class is refused with a chat message that
  lists the allowed classes, and the class menu is reopened. `joinclass random|auto` becomes a random
  allowed class.
* On `player_spawn` (checked one frame later) anyone playing a disallowed class gets
  `m_iDesiredPlayerClass` set to a random allowed class (never the Civilian) and is respawned at once.
  A player forced more than 3 times in 10 s is left alone and logged (another plugin fighting back).

### Commands and cvars

| Name | |
|---|---|
| `sm_huntedclass_enabled 1` | master switch |
| `sm_huntedclass_status` (admin, generic) | derived rules for this map + every player's class and whether it is allowed |
| `sm_huntedclass_rederive` (admin, config) | derive again (after editing the override file) |
| `sm_huntedclass_test <player> <class>` (admin, cheats) | test aid: says what `joinclass` would do, then respawns the player as that class so the spawn check can be watched |

The rule line for each map is written to the SourceMod log at map start.

### Together with classrestrict.smx

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

## pf2_spawnfix

Works around a PF2 engine bug that spawns players (in practice, Civilians) in the other team's spawn room.

| File | Install to |
|---|---|
| `pf2_spawnfix.smx` (built by CI, in the release zips) | `pf2/addons/sourcemod/plugins/` |
| `scripting/pf2_spawnfix.sp` | source |

Needs SourceMod core, sdktools and sdkhooks (1.11+; `gamedata/sdkhooks.games/game.pf2.txt` ships with
SourceMod). It needs no gamedata of its own.

To compile it yourself: `spcomp scripting/pf2_spawnfix.sp` with SourceMod 1.12's compiler.
`spcomp pf2_spawnfix.sp SPAWNFIX_LOGONLY=1` builds a variant that defaults to log only (`enabled 2`,
`civmask 0`).

### Root cause

Checked against the disassembly of PF2's shipped `server` binary (`CTFPlayer::SelectSpawnSpot`,
`CTFPlayer::EntSelectSpawnPoint`, `CTFGameRules::IsSpawnPointValid`). The public Source SDK code is older
and has no class check, so it is misleading here.

1. `EntSelectSpawnPoint` starts at `g_pLastSpawnPoints[team]`, the last spot this team spawned on
   (NULL until someone on the team has spawned this map), and calls `SelectSpawnSpot`.
2. `SelectSpawnSpot` takes the next `info_player_teamspawn` of **any** team as `pFirstSpot`, then walks
   every teamspawn and accepts the first one that passes all of these checks:
   - `IsSpawnPointValid`: same team, triggered, not disabled, hull clear;
   - origin is not (0,0,0);
   - class check (added in PF2's binary): `spawnflags == 0 || spawnflags & (1 << (class-1))`.
3. If nothing passes, it returns false and leaves `pSpot` on `pFirstSpot`. `EntSelectSpawnPoint` does not
   advance `g_pLastSpawnPoints`, but still returns `pSpot`, and the player spawns there. **Nothing checks
   that spot's team, enabled state or class mask.**
4. PF2's Civilian is class 10, so its spawnflags bit is 512. Many maps mark "all classes" as
   `spawnflags 511` (classes 1-9); on those maps no spawn allows the Civilian and **every** Civilian spawn
   takes the fallback. Among the maps commonly run on PF2 servers this includes ctf_2fort, ctf_granary,
   ctf_mach_pf2, ctf_turbine_pf2_v3, koth_viaduct_pf2pass, koth_karnak_rc3, cp_labor_pf2,
   cp_chaoscanyon_a7, ad_avanti_b1, pl_upward_betafied and tdm_painhouse_pf2m_rc2.
5. Usually `pFirstSpot` is the team's own next spot, so the Civilian still lands at home (on an
   unvalidated spot, and every fallback Civilian stacks on the same one). It lands in the **enemy** spawn
   when the team's last used spot is the last of its block in entity order, so the next entity belongs to
   the other team, or when no teammate has spawned yet this map. A simulation of the disassembled loop
   over the real entity order gives per-Civilian-spawn odds of roughly 1/17 to 3/16 on ctf_2fort,
   ctf_granary and koth_viaduct, and around 1/2 on tdm_painhouse; on maps that give the Civilian its own
   spawns it is 0. That is why the bug looks rare and random.
6. Other classes and maps with deliberate class masks can hit the same fallback, for example the hunted
   BLU VIP, which has a single `512` spot. When the walk wraps around on a class-mismatched spot, the
   shipped loop returns false straight away and never runs its "ignore players" second pass, so a briefly
   blocked VIP spot can send the VIP to the spot after the team's last one, which can be a RED spot.

Team switches, autobalance and class changes are not the cause; they only matter because any Civilian
spawn on an affected map goes through this path.

### What it does

1. **`sm_spawnfix_civmask 1`** (prevention). At map start, at round start and on each teamspawn as it
   spawns: if a team has *no* spawn the Civilian may use, the Civilian bit (512) is added to that team's
   spawns that already allow all nine normal classes. Civilians then pass the engine's own selection like
   everyone else, with no teleport. Maps that give the Civilian its own spawns (hunted maps,
   plr_highertower, ...) are left untouched, and maps whose name contains `hunted` or starts with `vip_`
   are never patched.
2. **`sm_spawnfix_enabled 1`** (safety net). One frame after `player_spawn`, the plugin checks whether the
   player stands on an `info_player_teamspawn` of another team (within 24 u in x/y, or up to 1024 u below
   it after the engine's drop to ground), which the engine only does in the fallback. It acts only when
   the player is not also on an own-team spot, no enabled own-team spawn is within
   `sm_spawnfix_ownradius` (leaves mixed DM layouts alone), and the player is not inside an own-team
   `func_respawnroom`. The player is then teleported to an enabled own-team spawn (dropped to the ground,
   with the spot's angles and zero velocity), chosen randomly within the first non-empty tier of:
   1. allows the class and is clear;
   2. allows the class and is clear of world geometry only;
   3. allows any class and is clear;
   4. allows any class and is clear of world geometry only.

   Tiers 3 and 4 never pick a hunted "cage" spawn (one inside a damaging `trigger_hurt`, the same rule
   `pf2_hunted_classrestrict` uses), so on a hunted map the BLU Civilian goes back to the VIP spawn.
   Every correction or skip is written to the SourceMod log with the player, team, class and both spots.

### Commands and cvars

| Name | Default | |
|---|---|---|
| `sm_spawnfix_enabled` | 1 | 0 off, 1 move wrong-team spawns, 2 log only (moves nobody) |
| `sm_spawnfix_civmask` | 1 | let the Civilian use all-class spawns on maps that give it none |
| `sm_spawnfix_ownradius` | 384 | do not act if an enabled own-team spawn is this close (units) |
| `sm_spawnfix_status` (admin, generic) | | per-team spawn counts, Civilian-usable counts, whether the map was patched, and this map's counters |

Recommended rollout: start with `sm_spawnfix_enabled 2` and `sm_spawnfix_civmask 0` to see in the
SourceMod log how often wrong-team spawns happen on your maps, then switch to `1`/`1`.

Works with `pf2_hunted_classrestrict` (its forced respawn is just another `player_spawn` that this plugin
checks again) and with the NextBot plugin (bots replan after a teleport).
