# prefortress2-nextbot

Valve's **NextBot** framework and **TFBot** AI (from the Source SDK 2013 Team Fortress 2 code) running
in **Pre-Fortress 2** (PF2) as a server-side [Metamod:Source](https://www.metamodsource.net/) plugin.

PF2's server binary has no navigation mesh system and no NextBot. This plugin brings both, without
modifying any game file: the bots are PF2's own puppet bots, driven every tick by the TFBot behavior
tree, so to the engine and to other plugins they are ordinary players.

## Features

| Area | What the bots do |
|---|---|
| Quota | `tf_bot_quota` / `tf_bot_quota_mode normal\|fill\|match`, difficulty, join-after-player, auto-vacate for humans |
| Combat | TFBot target selection, aiming and weapon choice with PF2's weapons (projectile leads for the nailgun, tranquilizer dart, syringe gun, rockets, pipes) |
| CTF | fetch / carry / deliver the intelligence, escort carriers, attack flag defenders; defenders guard the capture zone, chase the carrier and deny dropped flags |
| Control points, A/D, KOTH | native `CapturePoint` / `DefendPoint` behaviors |
| Payload | push / guard / block; payload race (`plr_`): both teams push their own cart, blockers scale with the enemy cart's progress |
| Arena and objectiveless maps (`dm_`, `tdm_`, ...) | roam: enemy spawn exits, a random reachable enemy, or a spot 500-2000 u away |
| Hunted / VIP maps | per-map rules (allowed classes, VIP escape, gate buttons); exactly one BLU VIP (a bot steps down for a human Civilian); bodyguards climb ladders, press the gate buttons and escort |
| PF2 mechanics | armor pickups, medic syringe heal/cure/infect, engineer wrench armor repair, PF2 buildings (single teleporter object with entrance/exit mode, no-build zones), demoman remote pipe traps, pyro incendiary rocket, spy disguise/cloak, PF2 hand grenades (prime / hold / throw), PF2 conditions (concussion, caltrops/leg shots, gas, infection) |
| Random Civilian | `tf_bot_civilian_chance`, capped per team, never on hunted/VIP maps |
| Ladders | ladder volumes from the BSPs (`data/ladders.cfg`) become native nav ladders at map load |
| Class restrictions | from `data/hunted.cfg` first, then learned per map (a class that spawns inside a damaging trigger or dies to the world right after spawning) |
| Robustness | stuck recovery (jump, repath), one-way door avoidance, hot reload (`meta unload`/`load` with live bots), runtime check that the `PlayerRunCommand` hook fires (bot driving is disabled if it does not) |
| Hibernation (Windows) | `pf2nb_prevent_hibernation 1` keeps an empty server simulating so bots can fill it |

See [docs/STATUS.md](docs/STATUS.md) for what was verified and how, and the known gaps.

## Supported game build

The plugin calls virtual functions on PF2's real entities by vtable slot and reads all fields by
sendprop/datamap name at runtime. The slots are in [`src/game/pf2_offsets.h`](src/game/pf2_offsets.h),
one column per platform, derived from PF2's unstripped Linux `server.so` of the **2026-02-16** PF2 build
(the build PF2's Windows `server.dll` of the same date matches; Windows slot = Linux slot - 1 for every
entry, because GCC emits two destructor slots where MSVC emits one):

    python3 tools/vtdump.py <pf2>/bin/server.so CTFPlayer            # whole vtable, Linux and Windows index
    python3 tools/vtdump.py <pf2>/bin/server.so CTFPlayer runcommand # filtered

`vtdump.py` needs GNU binutils (`readelf`, `c++filt`) on `PATH` (or `BINUTILS=<dir>`). PF2's weapon ids,
condition numbers and object types are newer than PF2's public source and were re-derived from the
shipped binaries too (`src/shim/tf_shareddefs.h`).

After a PF2 update, `pf2nb_status` shows `hook=BROKEN` if the `PlayerRunCommand` slot moved; the plugin
then refuses to drive bots. Re-run `vtdump.py` on the new `server.so`, update `pf2_offsets.h` and rebuild.

## Install

Requirements: a PF2 dedicated server with **Metamod:Source 1.11 or 1.12** (plugin API 16) installed in
`pf2/addons/metamod`.

1. Download `prefortress2-nextbot-<version>-windows.zip` or `-linux.zip` from the
   [releases](../../releases) and extract it into the `pf2/` game directory. It contains:

       addons/metamod/pf2nextbot.vdf                    tells Metamod to load the plugin
       addons/pf2nextbot/bin/pf2nextbot.dll             (Windows)
       addons/pf2nextbot/bin/pf2nextbot_i486.so         (Linux)
       addons/pf2nextbot/data/hunted.cfg                hunted/VIP map rules (editable)
       addons/pf2nextbot/data/ladders.cfg               ladder volumes per map
       addons/sourcemod/plugins/pf2_hunted_classrestrict.smx      optional, see below
       addons/sourcemod/configs/hunted_classrestrict.cfg          optional, see below
       cfg/pf2nextbot.example.cfg                       example settings

2. Download `prefortress2-nextbot-navmeshes-<version>.zip` (attached to the first release) and extract
   it into `pf2/` as well: it adds `addons/pf2nextbot/nav/<map>.nav` for PF2's maps. Bots need a nav
   mesh for the current map; see [Nav meshes](#nav-meshes) for other maps.
3. Add the settings you want to `cfg/server.cfg` (see `cfg/pf2nextbot.example.cfg` and the table below)
   and start the server. `meta list` should show `PF2 NextBot`, and `pf2nb_status` the bots.

The VDF is:

    "Metamod Plugin"
    {
        "alias"  "pf2nextbot"
        "file"   "addons/pf2nextbot/bin/pf2nextbot"
    }

Metamod adds the extension itself (`.dll` on Windows, `_i486.so` then `.so` on Linux).

To uninstall, delete `addons/metamod/pf2nextbot.vdf` (and `addons/pf2nextbot`). The plugin never
writes game files; the Windows hibernation change lives in memory only.

### Optional: hunted class-restriction SourceMod plugin

`pf2_hunted_classrestrict.smx` (source in [`sourcemod/`](sourcemod/)) is a small, independent SourceMod
plugin that enforces the class rules of PF2's hunted/VIP maps for **human** players, derived at map start
from the map's caged spawns (spawn points inside a damaging `trigger_hurt`), the same evidence the bot
rules in `hunted.cfg` come from. It needs SourceMod 1.11+ and is not required by the bots. Install it only
if you want those rules enforced for humans; delete the two `addons/sourcemod/...` files otherwise. See
[sourcemod/README.md](sourcemod/README.md).

## Configuration

Valve's TFBot cvars and commands work as in TF2: `tf_bot_quota`, `tf_bot_quota_mode`,
`tf_bot_difficulty`, `tf_bot_join_after_player`, `tf_bot_auto_vacate`, `tf_bot_keep_class_after_death`,
`tf_bot_add [count] [red|blue] [class] [easy|normal|hard|expert]`, `tf_bot_kick <name|all>`,
`tf_bot_kill`, `nb_debug`, ... A typical `server.cfg` block (also in `cfg/pf2nextbot.example.cfg`):

    tf_bot_difficulty 3
    tf_bot_quota 12
    tf_bot_quota_mode fill
    tf_bot_join_after_player 0
    tf_bot_auto_vacate 1
    tf_bot_keep_class_after_death 0
    tf_bot_civilian_chance 10
    pf2nb_prevent_hibernation 1

PF2-specific cvars:

| Cvar | Default | Meaning |
|---|---|---|
| `pf2nb_enable` | 1 | master switch |
| `pf2nb_prevent_hibernation` | 1 | keep an empty server awake so bots can fill it (Windows only; see Known gaps) |
| `tf_bot_civilian_chance` | 10 | % chance a bot picks the Civilian (never on hunted/VIP maps) |
| `tf_bot_civilian_max_per_team` | 1 | Civilian cap per team |
| `tf_bot_pf2_hunted_rules` | 1 | use `data/hunted.cfg` on hunted/VIP maps |
| `tf_bot_pf2_hunted_bot_vip` | 1 | one BLU bot plays the VIP when no human does |
| `tf_bot_pf2_ladders` | 1 | add nav ladders from `data/ladders.cfg` |
| `tf_bot_pf2_armor_ratio` | 0.5 | look for armor between fights when below this fraction |
| `tf_bot_pf2_armor_search_range` | 1500 | how far to look for armor |
| `tf_bot_pf2_team_support` | 1 | medic syringe heals / engineer wrench armor on teammates |
| `tf_bot_pf2_team_support_range` | 160 | range for the above |
| `tf_bot_pf2_flag_guard_radius` | 500 | how far from the objective attack/defend defenders take up guard positions |
| `tf_bot_pf2_avoid_hurt_triggers` | 1 | keep bots out of active `trigger_hurt` volumes |
| `tf_bot_pf2_learn_class_restrictions` | 1 | learn per-map class cages |
| `tf_bot_grenades` | 1 | use PF2 hand grenades |
| `tf_bot_grenade_fuse` / `_min_range` / `_max_range` / `_throw_speed` | 3.5 / 250 / 1200 / 700 | grenade planning |
| `pf2nb_unstick_jump` | 1 | bots stuck on the ground for 2+ s jump |
| `pf2nb_bad_connection_time` | 60 | seconds a mesh connection bots got stuck on is avoided (doubles per repeat, max 600; 0 = off) |
| `pf2nb_door_block_time` | 30 | how long a one-way door that never opens is blocked for pathing |
| `pf2nb_debug` | 0 | extra logging |

Diagnostics: `pf2nb_status` (bots, behavior stacks, hook state), `pf2nb_navinfo` (nav mesh and TF
decoration), `pf2nb_navat x y z [bot]`, `pf2nb_cpinfo`, `pf2nb_ents <pattern>`, `pf2nb_trace <bot>`.

`addons/pf2nextbot/data/hunted.cfg` and `ladders.cfg` are generated from the map BSPs by
`tools/hunted_rules.py` and `tools/ladders.py` and can be edited by hand.

## Nav meshes

Meshes are loaded from `addons/pf2nextbot/nav/<map>.nav` (not `maps/`), and saved there too. To make
or fix one on a **test** server (generation kicks the bots and takes a while):

    sv_cheats 1
    map <mapname>
    pf2nb_nav_generate            // full generate, seeded from every team spawn (recommended)
    // or: nav_generate           // Valve's stock generate (seeds from one spawn only)
    // then reload the map:  changelevel <mapname>

    pf2nb_nav_seed_ladders        // list ladder tops that have no nav area
    pf2nb_nav_seed_ladders go     // incremental generate from them

Valve's nav editing commands (`nav_edit 1`, `nav_mark`, `nav_delete`, `nav_analyze`, `nav_save`, ...)
work as in TF2. `pf2nb_nav_generate` also fixes a stock generator bug where spawn rooms behind doors
were silently skipped.

## Building

x86 only (PF2's server is 32-bit). Dependencies:

* [alliedmodders/hl2sdk](https://github.com/alliedmodders/hl2sdk), branch `sdk2013`
* [alliedmodders/metamod-source](https://github.com/alliedmodders/metamod-source), a 1.12 release tag
  (plugin API 16)

`tools/build.py` finds them from the `HL2SDK` / `MMSOURCE` environment variables, else from
`tools/local.json` (`{"hl2sdk": "...", "mmsource": "..."}`, git-ignored), else from `deps/hl2sdk` and
`deps/metamod-source`:

    git clone -b sdk2013 https://github.com/alliedmodders/hl2sdk deps/hl2sdk
    git clone -b 1.12.0.1228 https://github.com/alliedmodders/metamod-source deps/metamod-source

**Windows** (Visual Studio 2022 or its Build Tools with the C++ x86 tools; found with `vswhere`):

    python tools/build.py              # incremental, parallel -> build/pf2nextbot.dll

**Linux** (Clang, because `src/shim` uses `__declspec(property)` accessors, enabled with `-fdeclspec`,
plus 32-bit glibc/libstdc++ development files):

    sudo apt-get install clang g++-multilib python3
    python3 tools/build.py             # -> build/pf2nextbot_i486.so

`CXX` selects another Clang, `LINUX_SYSROOT` passes a `--sysroot`. Options: `clean`, `-j N`,
`--only <substring>`. The flags follow the hl2sdk-manifests sdk2013 settings (`POSIX LINUX _LINUX
COMPILER_GCC`, `-m32`, link `mathlib.a`, `tier1.a`, `libtier0_srv.so`, `libvstdlib_srv.so`);
the Linux binary exports only `CreateInterface` and links libstdc++/libgcc statically.

To package a release layout locally (after building the plugin and the `.smx`):
`python3 tools/package.py plugin <version> windows|linux --smx <path-to-smx>`.

The GitHub Actions workflow (`.github/workflows/build.yml`) builds both platforms on every push and
pull request and publishes a release with the zips when a `v*` tag is pushed (or on a manual run with a
tag name).

## Known gaps

| Item | State |
|---|---|
| Linux runtime | the Linux build compiles and links against the same SDK libraries Metamod uses, and its vtable slots were read from PF2's `server.so`, but it **has not been run on a Linux PF2 server yet**. Please report results. |
| Hibernation on Linux | `pf2nb_prevent_hibernation` is a no-op on Linux (it logs `hibernation control unsupported on Linux in this build`). The Windows implementation patches one instruction of `engine.dll` in memory after verifying a byte pattern; no Linux `engine.so` pattern has been verified. On Linux an empty server may hibernate and punt the bots until a human joins. |
| `hunted_stonehenge_test` | partial: the VIP reaches and rides the lift, but no full bot-won round yet |
| Payload path progress | `CTeamTrainWatcher` path projection is approximated by the cart position |
| Maps without a mesh | bots idle; generate one (see above) |

## License and credits

* `third_party/valve/` is Valve's NextBot, navigation mesh and TFBot code from the Source SDK 2013,
  used under the **Source 1 SDK License** ([`LICENSE`](LICENSE) and
  [`thirdpartylegalnotices.txt`](thirdpartylegalnotices.txt), also in `third_party/valve/`; both are
  included in the release zips as the license requires).
  Local edits are marked `// PF2NB:`.
* A few headers in `src/shim/` (`shareddefs.h`, `tf_shareddefs.h`, `mp_shareddefs.h`, `in_buttons.h`,
  `ai_activity.h`) come from the Pre-Fortress 2 source, which is itself derived from the Source SDK.
* The rest of this project (`src/`, `tools/`, `sourcemod/`) is distributed under the same Source 1 SDK
  License terms, so the whole plugin can be redistributed (free of charge) as one Source SDK
  modification.

Credits: **Valve** (Source SDK 2013, NextBot, TFBot); **AlliedModders** (Metamod:Source, SourceHook,
hl2sdk, SourceMod); the **Pre-Fortress 2** team; **TF2 Classic** (TF2C), whose bot changes for payload
race, arena/objectiveless roaming and SeekAndDestroy on maps without control points were the design
reference for the `// PF2NB (TF2C):` blocks (re-implemented here, not copied).

Pre-Fortress 2, Team Fortress and Source are trademarks of their respective owners; this project is not
affiliated with or endorsed by Valve or the Pre-Fortress 2 team.
