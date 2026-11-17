// pf2-nextbot: MvM population manager. PF2 has no MvM, so there is never one.
#pragma once
#include "cbase.h"
class CPopulationManager
{
public:
	bool CanBotsAttackWhileInSpawnRoom() const { return true; }
	bool IsInEndlessWaves() const { return false; }
	bool IsSpawningPaused() const { return false; }
	bool GetSentryBusterDamageAndKillThreshold(int &, int &) const { return false; }
};
extern CPopulationManager *g_pPopulationManager;
