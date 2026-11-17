#!/usr/bin/env python3
"""Derive pf2-nextbot rules for PF2 hunted/VIP maps from BSP evidence.

From each map's entity lump (and the models lump for brush bounds):
  * allowed classes per team: info_player_teamspawn spawnflags are a class bitmask (bit = class-1,
    0 = any). Spawns sitting in a damaging trigger_hurt (the "cage" for wrong classes) are excluded.
  * the VIP escape zone: the trigger that fires the BLU game_round_win (via its outputs), as the
    centre/extent of its brush model.

  python -I hunted_rules.py <maps-dir> <map> [map ...]          -> analysis to stdout
  python -I hunted_rules.py <maps-dir> --write <out.cfg> <map...> -> KeyValues config for the plugin
"""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bspents import entities, lump

CLASSES = ["scout", "sniper", "soldier", "demoman", "medic", "heavyweapons", "pyro", "spy", "engineer", "civilian"]
TEAM = {"2": "red", "3": "blue"}


def models(path):
    """lump 14 (dmodel_t: mins, maxs, origin, headnode, firstface, numfaces) -> list of (mins, maxs)"""
    d = lump(path, 14)
    out = []
    for i in range(len(d) // 48):
        v = struct.unpack_from("<9f", d, i * 48)
        out.append((v[0:3], v[3:6]))
    return out


def vec(s):
    try:
        return tuple(float(x) for x in s.split()[:3])
    except (ValueError, AttributeError):
        return None


def bounds(e, mdl):
    m = e.get("model", "")
    if m.startswith("*"):
        mn, mx = mdl[int(m[1:])]
        o = vec(e.get("origin", "0 0 0")) or (0, 0, 0)
        return tuple(a + b for a, b in zip(mn, o)), tuple(a + b for a, b in zip(mx, o))
    o = vec(e.get("origin"))
    return (o, o) if o else None


def inside(p, b, pad=16):
    return b and all(b[0][i] - pad <= p[i] <= b[1][i] + pad for i in range(3))


def mask_classes(mask):
    return [CLASSES[i] for i in range(10) if mask & (1 << i)] if mask else list(CLASSES)


def outputs(e):
    return [v for k, v in e.items() if k.split("#")[0].startswith("On")]


def analyse(path):
    ents, mdl = entities(path), models(path)
    hurts = [bounds(e, mdl) for e in ents if e.get("classname", "").startswith("trigger_hurt")
             and float(e.get("damage", "0") or 0) > 0 and e.get("StartDisabled", "0") == "0"]
    allowed = {"red": set(), "blue": set()}
    cage = {"red": 0, "blue": 0}
    for e in ents:
        if e.get("classname") != "info_player_teamspawn" or e.get("StartDisabled", "0") == "1":
            continue
        team = TEAM.get(e.get("TeamNum"))
        p = vec(e.get("origin"))
        if not team or not p:
            continue
        mask = int(e.get("spawnflags", "0") or 0) & 0x3FF
        if any(inside(p, b) for b in hurts if b):
            cage[team] += 1
            continue
        allowed[team].update(mask_classes(mask))
    # escape: triggers whose outputs fire a game_round_win for BLU
    wins = {e.get("targetname") for e in ents if e.get("classname") == "game_round_win" and e.get("TeamNum") == "3" and e.get("targetname")}
    escapes = []
    for e in ents:
        if not e.get("classname", "").startswith("trigger_"):
            continue
        if any(o.split(",")[0] in wins for o in outputs(e)):
            b = bounds(e, mdl)
            if b:
                escapes.append((e.get("classname"), e.get("filtername", ""), b))
    # VIP-only triggers: filtered by a filter that lets the Civilian through
    civ_filters = {e.get("targetname") for e in ents if e.get("classname") in ("filter_tf_class",) and e.get("tfclass") == "10"}
    civ_filters |= {e.get("targetname") for e in ents if e.get("classname") == "filter_multi"
                    and any(v in civ_filters for k, v in e.items() if k.startswith("Filter"))}
    byname = {}
    for e in ents:
        if e.get("targetname"):
            byname.setdefault(e["targetname"], []).append(e)
    # lift (Stonehenge): a VIP-only trigger that opens a func_door moving up -> stand on it and wait
    lifts = []
    for e in ents:
        if e.get("classname") != "trigger_multiple" or e.get("filtername") not in civ_filters:
            continue
        for o in outputs(e):
            parts = o.split(",")
            if len(parts) > 1 and parts[1] == "Open":
                for d in byname.get(parts[0], []):
                    if d.get("classname") == "func_door" and d.get("movedir", "").startswith("-90"):
                        b = bounds(e, mdl)
                        if b:
                            lifts.append(b)
    # gate: the door nearest the escape that buttons open, and those buttons
    door, buttons = None, []
    if escapes:
        ec = tuple((a + b) / 2 for a, b in zip(*escapes[0][2]))
        best = None
        for e in ents:
            if e.get("classname") != "func_button":
                continue
            for o in outputs(e):
                parts = o.split(",")
                if len(parts) > 1 and parts[1] in ("Open", "Unlock", "Toggle"):
                    for d in byname.get(parts[0], []):
                        b = bounds(d, mdl)
                        if not b or not d.get("classname", "").startswith("func_door"):
                            continue
                        dc = tuple((x + y) / 2 for x, y in zip(*b))
                        dist = sum((x - y) ** 2 for x, y in zip(dc, ec)) ** 0.5
                        if best is None or dist < best[0]:
                            best = (dist, parts[0])
        if best and best[0] < 1200:
            door = best[1]
            for e in ents:
                if e.get("classname") == "func_button" and any(o.split(",")[0] == door for o in outputs(e)):
                    bb = bounds(e, mdl)
                    if bb:
                        c = tuple((x + y) / 2 for x, y in zip(*bb))
                        buttons.append((e.get("targetname", ""), c, int(e.get("spawnflags", "0") or 0)))
            # prefer touch-activated buttons (the tower buttons); a use-only button next to the gate is
            # usually on the inside (the way back out), unreachable before the gate opens
            touch = [b for b in buttons if b[2] & 256]
            if touch:
                buttons = touch
    return allowed, cage, wins, escapes, lifts, door, buttons


def main():
    mapsdir = sys.argv[1]
    args = sys.argv[2:]
    out = None
    if args and args[0] == "--write":
        out, args = args[1], args[2:]
    lines = ['// pf2-nextbot: rules for PF2 hunted/VIP maps, generated by tools/hunted_rules.py from BSP evidence.',
             '// "red"/"blue": classes bots may play (space separated; edit to taste).',
             '// "vip_goal": where the Civilian VIP must go (mins maxs); "vip_wait" 1 = stand still there (lifts).',
             '// "gate_door": door that must be open before the VIP heads out; "buttonN": buttons that open it (bots touch/use them).',
             '"PF2HuntedRules"', "{"]
    for m in args:
        allowed, cage, wins, escapes, lifts, door, buttons = analyse(os.path.join(mapsdir, m + ".bsp"))
        print("== %s" % m)
        for t in ("red", "blue"):
            print("   %-4s allowed: %s  (cage spawns %d)" % (t, " ".join(c for c in CLASSES if c in allowed[t]) or "-", cage[t]))
        print("   BLU win entities: %s" % (", ".join(sorted(w for w in wins if w)) or "-"))
        for cls, flt, b in escapes:
            print("   escape: %s filter=%s mins=(%.0f %.0f %.0f) maxs=(%.0f %.0f %.0f)" % ((cls, flt) + b[0] + b[1]))
        for b in lifts:
            print("   vip lift: mins=(%.0f %.0f %.0f) maxs=(%.0f %.0f %.0f)" % (b[0] + b[1]))
        if door:
            print("   gate door: %s; buttons: %s" % (door, "; ".join(
                "%s (%.0f %.0f %.0f)%s" % (n, c[0], c[1], c[2], " touch" if f & 256 else "") for n, c, f in buttons)))
        lines.append('\t"%s"' % m)
        lines.append("\t{")
        for t in ("red", "blue"):
            if allowed[t]:
                lines.append('\t\t"%s"\t"%s"' % (t, " ".join(c for c in CLASSES if c in allowed[t])))
        if lifts:
            # the VIP stands on the lift and waits; the map moves/teleports him from there
            lines.append('\t\t"vip_goal"\t"%.0f %.0f %.0f %.0f %.0f %.0f"' % (lifts[0][0] + lifts[0][1]))
            lines.append('\t\t"vip_wait"\t"1"')
        elif escapes:
            lines.append('\t\t"vip_goal"\t"%.0f %.0f %.0f %.0f %.0f %.0f"' % (escapes[0][2][0] + escapes[0][2][1]))
        if door:
            lines.append('\t\t"gate_door"\t"%s"' % door)
            for i, (n, c, f) in enumerate(buttons):
                lines.append('\t\t"button%d"\t"%.0f %.0f %.0f"' % (i, c[0], c[1], c[2]))
        lines.append("\t}")
    lines.append("}")
    if out:
        open(out, "w", newline="\n").write("\n".join(lines) + "\n")
        print("wrote", out)


if __name__ == "__main__":
    main()
