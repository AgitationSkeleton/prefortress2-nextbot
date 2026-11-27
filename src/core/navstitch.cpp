// pf2-nextbot: stitch small gaps the nav generator leaves between floors.
//
// The generator samples the floor every 25 units; on some stairs (bam4's spiral stairs out of each spawn)
// a sample lands on a riser and is dropped, which leaves a one- or two-cell hole between two walkable
// areas and no connection across it. On bam4 that single hole cut both spawns off from the rest of the
// map, so every bot that wanted the enemy flag stood in its spawn with no path.
// At map load, facing areas up to two cells apart are linked when nothing else covers the gap, the floor
// across it is continuous and walkable (steps, stairs, ramps: pl_jungle's ship hold ramp was cut into
// pieces the same way and its spawn could not be left) and a player hull fits through.
#include "cbase.h"
#include "plugin.h"
#include "nav_mesh.h"
#include "tf_nav_mesh.h"

ConVar pf2nb_stitch_gaps("pf2nb_stitch_gaps", "1", FCVAR_NONE,
	"At map load, link walkable floors the nav generator left a small hole between (stairs)");

namespace
{
const float kMaxGap = 56.0f;      // a little over two generation cells
const float kMinOverlap = 20.0f;  // shared width the hull walks through

struct Stitch { unsigned int from, to; };
CUtlVector<Stitch> s_stitches;

struct Collect
{
	CUtlVector<CNavArea *> *out;
	bool operator()(CNavArea *a) { out->AddToTail(a); return true; }
};

// walkable floor under every point of the gap: no hole to fall through, no rise or drop between
// neighbouring samples taller than a step (a ramp or stairs, not a ledge), starting and ending at the
// two areas' edge heights
bool FloorAcross(const Vector &a, const Vector &b, float lowZ, float highZ)
{
	CTraceFilterWorldOnly world;
	Vector d = b - a;
	d.z = 0;
	float len = d.Length();
	int n = MAX(2, (int)(len / 8.0f));
	float prevZ = a.z;
	for (int i = 0; i <= n; ++i)
	{
		Vector p = a + d * ((float)i / n);
		trace_t tr;
		UTIL_TraceHull(Vector(p.x, p.y, highZ + StepHeight), Vector(p.x, p.y, lowZ - StepHeight - 2.0f),
			Vector(-4, -4, 0), Vector(4, 4, 4), MASK_PLAYERSOLID_BRUSHONLY, &world, &tr);
		if (tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal.z < 0.7f)
			return false;
		if (fabs(tr.endpos.z - prevZ) > StepHeight)
			return false;
		prevZ = tr.endpos.z;
	}
	return fabs(b.z - prevZ) <= StepHeight;
}

bool HullFits(const Vector &a, const Vector &b)
{
	CTraceFilterWorldOnly world;
	trace_t tr;
	float z = MAX(a.z, b.z) + StepHeight;
	UTIL_TraceHull(Vector(a.x, a.y, z), Vector(b.x, b.y, z), Vector(-12, -12, 0), Vector(12, 12, HumanCrouchHeight - StepHeight),
		MASK_PLAYERSOLID_BRUSHONLY, &world, &tr);
	return !tr.startsolid && tr.fraction >= 1.0f;
}
}

void PF2NB_StitchReset() { s_stitches.RemoveAll(); }

void PF2NB_StitchGaps()
{
	s_stitches.RemoveAll();
	if (!pf2nb_stitch_gaps.GetBool() || !TheNavMesh)
		return;
	CUtlVector<CNavArea *> near;
	FOR_EACH_VEC(TheNavAreas, it)
	{
		CNavArea *a = TheNavAreas[it];
		const Vector &alo = a->GetCorner(NORTH_WEST), &ahi = a->GetCorner(SOUTH_EAST);
		for (int dd = 0; dd < NUM_DIRECTIONS; ++dd)
		{
			NavDirType d = (NavDirType)dd;
			Extent ext;
			// as high/low as a stitch may climb across the widest gap (see the slope test below)
			const float reach = StepHeight + 0.9f * kMaxGap + 4.0f;
			ext.lo = Vector(alo.x, alo.y, MIN(alo.z, ahi.z) - reach);
			ext.hi = Vector(ahi.x, ahi.y, MAX(alo.z, ahi.z) + reach);
			switch (d)
			{
			case NORTH: ext.lo.y -= kMaxGap; ext.hi.y = alo.y; break;
			case SOUTH: ext.hi.y += kMaxGap; ext.lo.y = ahi.y; break;
			case WEST:  ext.lo.x -= kMaxGap; ext.hi.x = alo.x; break;
			default:    ext.hi.x += kMaxGap; ext.lo.x = ahi.x; break;
			}
			near.RemoveAll();
			Collect c = { &near };
			TheNavMesh->ForAllAreasOverlappingExtent(c, ext);
			FOR_EACH_VEC(near, k)
			{
				CNavArea *b = near[k];
				if (b == a || a->IsConnected(b, NUM_DIRECTIONS))
					continue;
				const Vector &blo = b->GetCorner(NORTH_WEST), &bhi = b->GetCorner(SOUTH_EAST);
				// gap along d, overlap across it
				float gap, olo, ohi;
				bool alongY = (d == NORTH || d == SOUTH);
				if (alongY)
				{
					gap = d == NORTH ? alo.y - bhi.y : blo.y - ahi.y;
					olo = MAX(alo.x, blo.x); ohi = MIN(ahi.x, bhi.x);
				}
				else
				{
					gap = d == WEST ? alo.x - bhi.x : blo.x - ahi.x;
					olo = MAX(alo.y, blo.y); ohi = MIN(ahi.y, bhi.y);
				}
				if (gap < 1.0f || gap > kMaxGap || ohi - olo < kMinOverlap)
					continue;
				float mid = (olo + ohi) * 0.5f;
				Vector pa, pb;
				if (alongY)
				{
					pa = Vector(mid, d == NORTH ? alo.y + 1.0f : ahi.y - 1.0f, 0);
					pb = Vector(mid, d == NORTH ? bhi.y - 1.0f : blo.y + 1.0f, 0);
				}
				else
				{
					pa = Vector(d == WEST ? alo.x + 1.0f : ahi.x - 1.0f, mid, 0);
					pb = Vector(d == WEST ? bhi.x - 1.0f : blo.x + 1.0f, mid, 0);
				}
				pa.z = a->GetZ(pa);
				pb.z = b->GetZ(pb);
				// a step, or a ramp the generator left holes in (a slope up to about 42 degrees across the gap);
				// FloorAcross below makes sure it is a continuous floor and not a ledge
				if (fabs(pb.z - pa.z) > StepHeight + 0.9f * gap)
					continue;
				// something already covers the gap at this height: not a hole
				Extent gapExt;
				gapExt.lo = Vector(MIN(pa.x, pb.x) + (alongY ? -1.0f : 2.0f), MIN(pa.y, pb.y) + (alongY ? 2.0f : -1.0f), MIN(pa.z, pb.z) - StepHeight);
				gapExt.hi = Vector(MAX(pa.x, pb.x) - (alongY ? -1.0f : 2.0f), MAX(pa.y, pb.y) - (alongY ? 2.0f : -1.0f), MAX(pa.z, pb.z) + StepHeight);
				CUtlVector<CNavArea *> cover;
				Collect cc = { &cover };
				TheNavMesh->ForAllAreasOverlappingExtent(cc, gapExt);
				bool covered = false;
				FOR_EACH_VEC(cover, ci)
					if (cover[ci] != a && cover[ci] != b)
						covered = true;
				if (covered)
					continue;
				// back the hull start off the edge (areas reach half a sample past the valid spots)
				Vector sa = pa, sb = pb;
				AddDirectionVector(&sa, d, -12.0f);
				AddDirectionVector(&sb, d, 12.0f);
				sa.z = a->GetZ(sa);
				sb.z = b->GetZ(sb);
				if (!FloorAcross(pa, pb, MIN(pa.z, pb.z), MAX(pa.z, pb.z)) || !HullFits(sa, sb))
					continue;
				a->ConnectTo(b, d);
				Stitch s = { a->GetID(), b->GetID() };
				s_stitches.AddToTail(s);
			}
		}
	}
	if (s_stitches.Count())
		PF2NB_Log("nav: %d small gaps between floors stitched (pf2nb_stitches lists them)", s_stitches.Count());
}

CON_COMMAND(pf2nb_stitches, "List the nav gaps linked at map load")
{
	for (const Stitch &s : s_stitches)
	{
		CNavArea *a = TheNavMesh ? TheNavMesh->GetNavAreaByID(s.from) : NULL;
		PF2NB_Log("  %u -> %u at (%.0f %.0f %.0f)", s.from, s.to, a ? a->GetCenter().x : 0, a ? a->GetCenter().y : 0, a ? a->GetCenter().z : 0);
	}
	PF2NB_Log("%d stitched gaps", s_stitches.Count());
}

// pf2nb_reach: from each team's spawns, which objectives can the mesh reach (walk links, stitched gaps,
// teleporter links, ladders; team rules are not applied). For checking a map after load-time repairs.
CON_COMMAND(pf2nb_reach, "pf2nb_reach: from each team's spawns, can the nav mesh (with stitches/teleporters/ladders) reach the flags and control points")
{
	if (!TheNavMesh || !TheNavMesh->IsLoaded())
		return;
	PF2_InvalidateEntityCache();
	for (int team = TF_TEAM_RED; team <= TF_TEAM_BLUE; ++team)
	{
		CUtlVector<CNavArea *> queue;
		CNavArea::MakeNewMarker();
		int spawns = 0;
		for (CBaseEntity *sp = gEntList.FindEntityByClassname(NULL, "info_player_teamspawn"); sp; sp = gEntList.FindEntityByClassname(sp, "info_player_teamspawn"))
		{
			bool *disabled = sp->Field<bool>("m_bDisabled");
			if (sp->GetTeamNumber() != team || (disabled && *disabled))
				continue;
			CNavArea *a = TheNavMesh->GetNearestNavArea(sp->GetAbsOrigin(), false, 200.0f, false, false);
			if (a && !a->IsMarked())
			{
				a->Mark();
				queue.AddToTail(a);
			}
			++spawns;
		}
		for (int qi = 0; qi < queue.Count(); ++qi)
		{
			CNavArea *a = queue[qi];
			for (int d = 0; d < NUM_DIRECTIONS; ++d)
			{
				const NavConnectVector *c = a->GetAdjacentAreas((NavDirType)d);
				FOR_EACH_VEC((*c), i)
					if (!c->Element(i).area->IsMarked())
					{
						c->Element(i).area->Mark();
						queue.AddToTail(c->Element(i).area);
					}
			}
			for (int ld = 0; ld < CNavLadder::NUM_LADDER_DIRECTIONS; ++ld)
			{
				const NavLadderConnectVector *lv = a->GetLadders((CNavLadder::LadderDirectionType)ld);
				FOR_EACH_VEC((*lv), i)
				{
					CNavLadder *l = lv->Element(i).ladder;
					CNavArea *ends[] = { l->m_topForwardArea, l->m_topLeftArea, l->m_topRightArea, l->m_topBehindArea, l->m_bottomArea };
					for (CNavArea *e : ends)
						if (e && !e->IsMarked())
						{
							e->Mark();
							queue.AddToTail(e);
						}
				}
			}
		}
		int bad = 0, objs = 0;
		const char *classes[] = { "item_teamflag", "team_control_point" };
		for (const char *cn : classes)
			for (CBaseEntity *o = gEntList.FindEntityByClassname(NULL, cn); o; o = gEntList.FindEntityByClassname(o, cn))
			{
				if (!V_strcmp(cn, "item_teamflag") && o->GetTeamNumber() == team)
					continue;
				++objs;
				CNavArea *a = TheNavMesh->GetNearestNavArea(o->GetAbsOrigin(), false, 400.0f, false, false);
				if (!a || !a->IsMarked())
				{
					++bad;
					Vector p = o->GetAbsOrigin();
					PF2NB_Log("  %s cannot reach %s '%s' at (%.0f %.0f %.0f)%s", team == TF_TEAM_RED ? "red" : "blue", cn, STRING(o->GetEntityName()), p.x, p.y, p.z, a ? "" : " (no area near it)");
				}
			}
		PF2NB_Log("%s: %d spawns reach %d of %d areas; %d of %d objectives unreachable", team == TF_TEAM_RED ? "red" : "blue", spawns,
			queue.Count(), TheNavAreas.Count(), bad, objs);
	}
}
