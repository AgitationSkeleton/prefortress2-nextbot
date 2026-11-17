# Status

What has been verified, and how. Unless marked otherwise, everything below was tested with the
Windows build on a private PF2 dedicated test server (2026-02-16 PF2 build, Metamod:Source 1.11,
SourceMod with a typical set of community plugins loaded alongside), usually under a debugger to catch
faults.

## Working (verified on Windows)

| Area | Evidence |
|---|---|
| Plugin loads under Metamod (API 16) | `meta list`; clean boot under a debugger, no faults |
| Bots are PF2 puppet bots driven by Valve TFBot | `pf2nb_status` shows native action stacks, e.g. `MainAction( TacticalMonitor( ScenarioMonitor( FetchFlag ) ) )`, `DeliverFlag`, `AttackFlagDefenders`, `EscortFlagCarrier`, `Heal`, `EngineerBuild` |
| Nav meshes load from `addons/pf2nextbot/nav` | TF decoration computed (spawn rooms, incursion distances) |
| Quota (`tf_bot_quota`, `fill`, difficulty, join-after-player) | an empty server fills from `server.cfg` at boot |
| Combat | kills with the minigun, rocket launcher etc. in the game log |
| CTF loop | bots pick up the enemy intelligence and carry it (`DeliverFlag`) |
| Flag attack/defend | defenders run `PF2DefendFlag` (guard the capture zone, chase the carrier, deny dropped flags) |
| CP, CP A/D, KOTH, payload | native `CapturePoint` / `DefendPoint` / `PayloadPush` / `PayloadGuard` / `PayloadBlock` |
| Objectiveless maps (`dm_`, `tdm_`, `mulch_dm`, ...) | SeekAndDestroy roams (enemy spawn exits / a random enemy / somewhere 500-2000 u away) and no longer quits every tick on maps with a round timer but no points. A flag without a `func_capturezone` (e.g. a soccer ball) is not an objective |
| Payload race (`plr_`) | PF2's `m_bMultipleTrains`; both teams push their own cart, the share of blockers grows with the enemy cart's progress (min ~20%), roles switch as the race changes, spare pushers roam |
| Ladders | `data/ladders.cfg` (PF2 `info_ladder` volumes from the BSPs) adds native `CNavLadder`s at map load; bots climb them |
| Hibernation | `pf2nb_prevent_hibernation 1` keeps an empty server simulating (Windows) |
| Hot reload | `meta unload` / `meta load` with live bots; existing puppets are adopted |
| One-way doors | bots stuck at a door that never opens for their team block it for pathing and repath |
| Class restrictions | hunted rules first, then learned per map (spawned inside a damaging trigger / died to the world right after spawning); a bot that spawns as a disallowed class re-picks and respawns at once (also after a team swap) |
| Other plugins | a set of ~28 common SourceMod plugins (anti-cheat, class restrictions, PF2 tools, ...) loaded and ran alongside the bots with no detections or kicks |

### Hunted/VIP maps

Rules live in `addons/pf2nextbot/data/hunted.cfg`, generated from BSP evidence by `tools/hunted_rules.py`
and editable: allowed classes per team (from `info_player_teamspawn` class masks, ignoring cage spawns),
the VIP goal (the trigger that fires the BLU `game_round_win`, or a VIP-only lift for Stonehenge), and
the gate door plus the touch buttons that open it.

* Exactly one VIP: one BLU bot plays the Civilian only if BLU has no Civilian; a bot VIP steps down if a
  human (or a lower-index bot) is the Civilian.
* BLU bodyguards climb the tower ladders, touch the gate buttons, then escort the VIP; medics heal.
  This takes priority over BLU class behaviors (a BLU sniper/spy does bodyguard duty).
* The VIP waits for the gate, then paths to the escape (or stands still on the lift and waits).

| Map | Result |
|---|---|
| `hunted_actually_accurate_a1` | 7 BLU VIP escapes in 7 rounds over two sessions (~122-138 s), across team swaps |
| `hunted_biblically_accurate_a3` | 6 BLU VIP escapes in a row (~119-153 s) |
| `hunted_actually_accurate_a2`, `hunted_accurate` | same layout and rules |
| `hunted_stonehenge_test` | partial: the VIP reaches the pillar and rides it up when the lift fires; no full round won yet (snags leaving BLU's base, deaths during the 15 s ascent) |

Before these changes every hunted round ended in a RED timeout win.

### PF2 game rules the bots follow

The shipped PF2 server binary is newer than PF2's public source; weapon ids, conditions and object
types were re-derived from the binaries (see `src/shim/tf_shareddefs.h`).

| Mechanic | Bot handling |
|---|---|
| Armor (all classes but Civilian; absorbs 30/60/80 % of damage) | `GetArmor` fetches `item_armor` / resupply when armor < `tf_bot_pf2_armor_ratio` between fights |
| Medic syringe (melee): heals + cures burning/infection/tranq/leg damage on teammates, infects enemies | heals/cures nearby teammates when no enemy is in view; infects enemies at melee range; medigun beam + Uber as in TF2 |
| Infection (damage over time, ignores armor) | counts as "hurt": the bot fetches health / a healer cures it |
| Engineer wrench on teammates: armor for metal | repairs nearby teammates' armor between fights (`tf_bot_pf2_team_support`) |
| Engineer buildings: one teleporter object with entrance/exit mode, no building in spawn rooms / `func_nobuild` | `build <type> <mode>`; build spots skip spawn rooms and no-build volumes; blueprints are not counted as built |
| Demoman remote pipes (roll instead of sticking) | trap laying / detonation |
| Scout nailgun, spy tranquilizer dart, syringe gun | projectile lead |
| Pyro: no airblast; alt-fire is an incendiary rocket | uses the rocket beyond flame range (up to 750 u) |
| Spy disguise (`disguise <class> <team>`) and cloak | PF2's argument order |
| PF2 hand grenades | prime/hold/throw module (`tf_bot_grenade*`) |
| Concussion, caltrops/leg shots, gas | recognised conditions; syringe cures |

## Nav tooling

* `pf2nb_nav_generate`: full generate seeded from every player spawn (stock seeds from one spawn only).
* Generator fix: stock `GetNextWalkableSeedNode` returned NULL for a seed on an already-sampled node,
  which ended sampling and silently dropped every later seed, so spawn rooms behind doors were missed
  (e.g. `plr_chariots`: 1368 areas without spawn rooms, bots frozen off the mesh -> 3251 areas).
* `pf2nb_nav_seed_ladders [go]`: list ladder tops without nav areas; `go` runs an incremental generate
  plus analysis from them (test servers only).
* Saved meshes always go to `addons/pf2nextbot/nav` (`CNavMesh::GetFilename` is patched), never `maps/`.
* `pf2nb_navat x y z [bot]`: nearest area to a point and whether a bot can reach it.
* The released nav pack covers 163 of PF2's 164 maps (all but the `background01` menu backdrop).

## Linux

The Linux build (`pf2nextbot_i486.so`) compiles and links with Clang against the hl2sdk sdk2013 Linux
libraries (`--no-undefined`, so every engine symbol it uses resolves against `libtier0_srv.so`,
`libvstdlib_srv.so` and the static `tier1`/`mathlib`). Every vtable slot it uses was read from PF2's
Linux `server.so`. It has **not** been run on a Linux PF2 server yet; differences from Windows:

* `pf2nb_prevent_hibernation` is a logged no-op (no verified Linux `engine.so` pattern).
* Virtual calls use the Itanium (GCC) calling convention: `this` first on the stack, hidden
  struct-return pointer before it.
* Only `CreateInterface` is exported, and symbols bind locally (`-Bsymbolic`), so the plugin's proxy
  classes, whose names match classes in `server.so`, cannot interpose or be interposed.
* The SDK's static libraries are not position independent, so the `.so` has text relocations, as is
  usual for sdk2013 plugins.

## Known gaps

| Item | State |
|---|---|
| Maps without nav | only `background01` (menu backdrop) |
| Stonehenge (`hunted_stonehenge_test`) | see the hunted table |
| Payload path progress | `CTeamTrainWatcher` path projection is approximated by the cart position |
| Linux | built, not yet run on a server (see above) |

## Soak results (Windows)

* 180 s per map with the community plugin set loaded, under a debugger: ctf_2fort, storm,
  hunted_actually_accurate_a1, plr_pipeline_pf2, pl_goldrush, ad_dustbowl, koth_viaduct_pf2pass,
  dm_crossfire, mulch_dm, tdm_2fort - 0 faults, at most 1 bot stuck > 3 s on a map, no anti-cheat
  detections or kicks.
* Two full 11-map soaks (CTF, flag A/D, CP, CP A/D, KOTH, payload, arena, hunted) under a debugger:
  0 faults, 0 stuck.
