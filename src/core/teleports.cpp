// pf2-nextbot: map teleporters (trigger_teleport) as nav links.
//
// Valve's nav mesh has no notion of teleport triggers, so a region whose only exit is a teleporter is a
// dead end for bots: plr_highertower's civilian spawn room (walk into a teleporter to reach the map)
// kept its Civilian there all round, and maps that use teleporters as routes (balloon_race,
// cp_chaoscanyon, rock2, ...) could not be crossed. At map load every trigger_teleport that players can
// use gets one-way links from the nav areas inside it to the area at its destination. The path cost
// (CTFBotPathCost) charges a fixed cost for such a link and skips its height checks; the path follower
// walks to the trigger (Path::ComputePathDetails, PF2NB edit) and is carried across; the bot repaths
// from where it lands.
#include "cbase.h"
#include "plugin.h"
#include "nav_mesh.h"
#include "tf_nav_mesh.h"
#include "bot/tf_bot.h"

extern ConVar pf2nb_debug;
ConVar tf_bot_pf2_teleports("tf_bot_pf2_teleports", "1", FCVAR_NONE,
	"Bots use the map's trigger_teleport volumes as nav links (applied at map load)");

namespace
{
struct TeleLink
{
	unsigned int from, to;
	Vector trigger;         // the trigger's box centre: round restarts recreate map entities, so it is found again by place
	Vector enter;           // where to walk to on the source area's floor
	bool allowed[2];        // red, blue: refreshed once a second (filters and enabled state change in a round)
};
CUtlVector<TeleLink> s_links;
CUtlVector<unsigned int> s_fromIDs;   // sorted source area ids: the path cost asks about every edge it explores
float s_nextRefresh = 0.0f;

bool TriggerAllows(CBaseEntity *e, int team)
{
	if (!e)
		return false;
	bool *disabled = e->Field<bool>("m_bDisabled");
	if (disabled && *disabled)
		return false;
	int own = e->GetTeamNumber();
	if ((own == TF_TEAM_RED || own == TF_TEAM_BLUE) && own != team)
		return false;
	CBaseEntity *filter = e->HandleField("m_hFilter");
	if (filter && filter->ClassMatches("filter_activator_tfteam"))
	{
		bool *neg = filter->Field<bool>("m_bNegated");
		int t = filter->GetTeamNumber();
		bool match = (t == team);
		if (neg && *neg)
			match = !match;
		return match;
	}
	return true;   // no filter, or a class/name filter we can't judge: allowed
}

// the live trigger_teleport at that place, or NULL
CBaseEntity *Live(const Vector &centre)
{
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "trigger_teleport"); e; e = gEntList.FindEntityByClassname(e, "trigger_teleport"))
	{
		Vector lo, hi;
		e->CollisionProp()->WorldSpaceAABB(&lo, &hi);
		if (((lo + hi) * 0.5f - centre).IsLengthLessThan(2.0f))
			return e;
	}
	return NULL;
}

void Refresh()
{
	if (gpGlobals->curtime < s_nextRefresh && gpGlobals->curtime > s_nextRefresh - 2.0f)
		return;
	s_nextRefresh = gpGlobals->curtime + 1.0f;
	Vector last(1e9f, 1e9f, 1e9f);
	CBaseEntity *live = NULL;
	for (TeleLink &l : s_links)
	{
		if (l.trigger != last)
		{
			last = l.trigger;
			live = Live(l.trigger);
		}
		l.allowed[0] = live && TriggerAllows(live, TF_TEAM_RED);
		l.allowed[1] = live && TriggerAllows(live, TF_TEAM_BLUE);
	}
}

const TeleLink *Find(const CNavArea *from, const CNavArea *to)
{
	if (!from || !to)
		return NULL;
	unsigned int f = from->GetID(), t = to->GetID();
	int lo = 0, hi = s_fromIDs.Count() - 1;
	bool any = false;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (s_fromIDs[mid] == f) { any = true; break; }
		if (s_fromIDs[mid] < f) lo = mid + 1; else hi = mid - 1;
	}
	if (!any)
		return NULL;
	for (const TeleLink &l : s_links)
		if (l.from == f && l.to == t)
			return &l;
	return NULL;
}
}

void PF2NB_TeleportsReset() { s_links.RemoveAll(); s_fromIDs.RemoveAll(); s_nextRefresh = 0.0f; }

void PF2NB_AddTeleportLinks()
{
	s_links.RemoveAll();
	s_fromIDs.RemoveAll();
	if (!tf_bot_pf2_teleports.GetBool() || !TheNavMesh)
		return;
	PF2_InvalidateEntityCache();   // the entity list cache can still hold the previous map's entities here
	int triggers = 0;
	for (CBaseEntity *e = gEntList.FindEntityByClassname(NULL, "trigger_teleport"); e; e = gEntList.FindEntityByClassname(e, "trigger_teleport"))
	{
		int *flags = e->Field<int>("m_spawnflags");
		if (flags && !(*flags & 1))
			continue;   // not for clients
		string_t *target = e->Field<string_t>("m_target");
		if (!target || !STRING(*target) || !STRING(*target)[0])
		{
			if (pf2nb_debug.GetBool()) PF2NB_Log("teleport #%d: no target", e->entindex());
			continue;
		}
		CBaseEntity *dest = gEntList.FindEntityByName(NULL, STRING(*target));
		if (!dest)
		{
			if (pf2nb_debug.GetBool()) PF2NB_Log("teleport #%d: no destination '%s'", e->entindex(), STRING(*target));
			continue;
		}
		Vector landing = dest->GetAbsOrigin();
		CNavArea *to = TheNavMesh->GetNearestNavArea(landing, false, 300.0f, false, false);
		if (!to)
		{	// destinations in the air (plr_highertower drops its Civilians onto the map from the sky): the floor below
			CTraceFilterWorldOnly world;
			trace_t tr;
			UTIL_TraceHull(landing, landing - Vector(0, 0, 32768.0f), Vector(-16, -16, 0), Vector(16, 16, 72), MASK_PLAYERSOLID_BRUSHONLY, &world, &tr);
			if (!tr.startsolid && tr.fraction < 1.0f)
				to = TheNavMesh->GetNearestNavArea(tr.endpos, false, 300.0f, false, false);
			// still nothing (a slide with no mesh on it): the mesh beneath the landing spot; the bot repaths
			// from wherever it really ends up
			if (!to)
				to = TheNavMesh->GetNavArea(landing, 32768.0f);
		}
		if (!to)
		{
			if (pf2nb_debug.GetBool()) PF2NB_Log("teleport #%d: no nav area at its destination (%.0f %.0f %.0f)", e->entindex(), landing.x, landing.y, landing.z);
			continue;
		}
		Vector lo, hi;
		e->CollisionProp()->WorldSpaceAABB(&lo, &hi);
		if ((hi - lo).AsVector2D().IsLengthGreaterThan(4000.0f))
			continue;   // map-wide volumes (round-end returns) are not routes
		Vector mid = (lo + hi) * 0.5f;
		int added = 0;
		FOR_EACH_VEC(TheNavAreas, it)
		{
			CNavArea *a = TheNavAreas[it];
			const Vector &c = a->GetCenter();
			if (a == to || c.x < lo.x || c.x > hi.x || c.y < lo.y || c.y > hi.y || c.z < lo.z - 40.0f || c.z > hi.z)
				continue;
			Vector enter(c.x, c.y, a->GetZ(c));
			Vector d = mid - c;
			NavDirType dir = fabs(d.x) > fabs(d.y) ? (d.x > 0 ? EAST : WEST) : (d.y > 0 ? SOUTH : NORTH);
			a->ConnectTo(to, dir);
			TeleLink l = { a->GetID(), to->GetID(), mid, enter, { true, true } };
			s_links.AddToTail(l);
			++added;
		}
		if (!added)
		{	// a trigger smaller than any area: link the area under its middle
			CNavArea *a = TheNavMesh->GetNearestNavArea(mid, false, 128.0f, false, false);
			if (a && a != to)
			{
				Vector d = mid - a->GetCenter();
				NavDirType dir = fabs(d.x) > fabs(d.y) ? (d.x > 0 ? EAST : WEST) : (d.y > 0 ? SOUTH : NORTH);
				a->ConnectTo(to, dir);
				Vector enter(mid.x, mid.y, a->GetZ(mid));
				TeleLink l = { a->GetID(), to->GetID(), mid, enter, { true, true } };
				s_links.AddToTail(l);
				++added;
			}
		}
		if (added)
			++triggers;
	}
	s_fromIDs.RemoveAll();
	for (const TeleLink &l : s_links)
		if (s_fromIDs.Find(l.from) < 0)
			s_fromIDs.AddToTail(l.from);
	s_fromIDs.Sort([](const unsigned int *l, const unsigned int *r) { return *l < *r ? -1 : (*l > *r ? 1 : 0); });
	if (s_links.Count())
		PF2NB_Log("nav: %d teleporters linked (%d area links)", triggers, s_links.Count());
}

// < 0: not a teleport link; -2: a teleport link this team can't use; else the link's cost
float PF2NB_TeleportLinkCost(const CNavArea *from, const CNavArea *to, int team)
{
	if (s_links.Count() == 0)
		return -1.0f;
	const TeleLink *l = Find(from, to);
	if (!l)
		return -1.0f;
	Refresh();
	int ti = team == TF_TEAM_BLUE ? 1 : 0;
	if ((team == TF_TEAM_RED || team == TF_TEAM_BLUE) && !l->allowed[ti])
		return -2.0f;
	return 100.0f;
}

bool PF2NB_TeleportEntry(const CNavArea *from, const CNavArea *to, Vector *enter)
{
	const TeleLink *l = s_links.Count() ? Find(from, to) : NULL;
	if (!l)
		return false;
	*enter = l->enter;
	return true;
}

CON_COMMAND(pf2nb_teleports, "pf2nb_teleports [rescan]: list the teleporter nav links made at map load (rescan: make them again)")
{
	if (args.ArgC() > 1 && !V_stricmp(args[1], "rescan"))
		PF2NB_AddTeleportLinks();
	s_nextRefresh = 0.0f;
	Refresh();
	for (const TeleLink &l : s_links)
		PF2NB_Log("  trigger '%s': area %u -> %u, enter (%.0f %.0f %.0f)%s%s", Live(l.trigger) ? STRING(Live(l.trigger)->GetEntityName()) : "(gone)",
			l.from, l.to, l.enter.x, l.enter.y, l.enter.z, l.allowed[0] ? "" : " (not red)", l.allowed[1] ? "" : " (not blue)");
	if (args.ArgC() > 1 && !V_stricmp(args[1], "raw"))
	{
		for (const TeleLink &l : s_links)
			PF2NB_Log("  link trigger at (%.0f %.0f %.0f)", l.trigger.x, l.trigger.y, l.trigger.z);
	}
	PF2NB_Log("%d teleporter links", s_links.Count());
}
