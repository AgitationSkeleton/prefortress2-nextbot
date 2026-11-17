// pf2-nextbot game layer: CTFPlayer helpers that need the nav mesh.
#include "cbase.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "tf_nav_mesh.h"

// Valve: CTFPlayer::SelectClosestControlPointByTravelDistance (Source SDK 2013, tf_player.cpp),
// with CTFPlayertPathCost replaced by the generic shortest-path cost.
CTeamControlPoint *CTFPlayer::SelectClosestControlPointByTravelDistance(CUtlVector<CTeamControlPoint *> *pointVector) const
{
	if (!pointVector || pointVector->Count() == 0)
		return NULL;
	if (GetLastKnownArea() == NULL)
		return NULL;
	CTeamControlPoint *closestPoint = NULL;
	float closestPointTravelRange = FLT_MAX;
	ShortestPathCost cost;
	for (int i = 0; i < pointVector->Count(); ++i)
	{
		CTeamControlPoint *point = pointVector->Element(i);
		float travelRange = NavAreaTravelDistance(GetLastKnownArea(), TheTFNavMesh()->GetControlPointCenterArea(point->GetPointIndex()), cost);
		if (travelRange >= 0.0 && travelRange < closestPointTravelRange)
		{
			closestPoint = point;
			closestPointTravelRange = travelRange;
		}
	}
	return closestPoint;
}
