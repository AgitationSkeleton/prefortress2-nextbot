// pf2-nextbot / ff-nextbot: cheap frame profiler for the bot plugin (pf2nb_perf).
// Sections accumulate time per server frame; the console command reports averages, maxima and how
// often a frame went over budget, so stutter can be traced to plugin work vs. the game itself.
#pragma once
#include <tier0/platform.h>

namespace perf {

enum Section
{
	FRAME_INTERVAL,    // wall time between our GameFrame hooks (the server's real frame time)
	GAMEFRAME,         // our GameFrame hook: quota, door/hazard scans, NextBot scheduling
	DRIVE,             // Puppet_Drive: RunPlayerMove for every bot (game movement + our think)
	THINK,             // TFBot think (NextBotPlayer::PhysicsSimulate) for all bots this frame
	THINK_MAX_BOT,     // the slowest single bot think this frame
	PATH,              // Path::Compute (A* searches) this frame
	VISION,            // IVision::Update this frame
	SECTION_COUNT
};

void Add(Section s, double seconds);   // accumulate into the current frame
void MaxOf(Section s, double seconds); // keep the frame's maximum
void EndFrame();                       // close the frame (called once per GameFrame)
void Reset();

struct Scope
{
	Section s;
	double t0;
	explicit Scope(Section sec) : s(sec), t0(Plat_FloatTime()) {}
	~Scope() { Add(s, Plat_FloatTime() - t0); }
};

} // namespace perf
