// pf2-nextbot / ff-nextbot: frame profiler (see perf.h)
#include <stdio.h>
#include <string.h>
#include "perf.h"
#include <convar.h>
#include <tier0/dbg.h>

namespace perf {

static const char *s_names[SECTION_COUNT] = { "frame interval", "plugin GameFrame", "bot drive (all)", "bot think (all)", "slowest bot think", "path compute", "vision update" };
static double s_cur[SECTION_COUNT];
static double s_sum[SECTION_COUNT], s_max[SECTION_COUNT];
static int s_over[SECTION_COUNT][3];   // frames over 10 / 20 / 50 ms
static int s_frames = 0;
static double s_lastFrame = 0.0;
static double s_since = 0.0;

void Add(Section s, double seconds) { s_cur[s] += seconds; }
void MaxOf(Section s, double seconds) { if (seconds > s_cur[s]) s_cur[s] = seconds; }

void Reset()
{
	memset(s_cur, 0, sizeof(s_cur));
	memset(s_sum, 0, sizeof(s_sum));
	memset(s_max, 0, sizeof(s_max));
	memset(s_over, 0, sizeof(s_over));
	s_frames = 0;
	s_since = Plat_FloatTime();
}

void EndFrame()
{
	double now = Plat_FloatTime();
	if (s_lastFrame > 0.0)
		s_cur[FRAME_INTERVAL] = now - s_lastFrame;
	s_lastFrame = now;
	if (s_since == 0.0)
		s_since = now;
	for (int i = 0; i < SECTION_COUNT; ++i)
	{
		double v = s_cur[i];
		s_sum[i] += v;
		if (v > s_max[i])
			s_max[i] = v;
		if (v > 0.010) s_over[i][0]++;
		if (v > 0.020) s_over[i][1]++;
		if (v > 0.050) s_over[i][2]++;
		s_cur[i] = 0.0;
	}
	++s_frames;
}

static void Report()
{
	double span = Plat_FloatTime() - s_since;
	Msg("[NB perf] %d frames over %.1f s\n", s_frames, span);
	for (int i = 0; i < SECTION_COUNT; ++i)
		Msg("  %-20s avg %7.3f ms  max %7.2f ms  frames >10ms %5d  >20ms %5d  >50ms %4d\n", s_names[i],
			s_frames ? s_sum[i] / s_frames * 1000.0 : 0.0, s_max[i] * 1000.0, s_over[i][0], s_over[i][1], s_over[i][2]);
}

} // namespace perf

CON_COMMAND(pf2nb_perf, "pf2nb_perf [reset]: bot plugin frame profile since the last reset")
{
	if (args.ArgC() > 1 && !V_stricmp(args[1], "reset"))
	{
		perf::Reset();
		Msg("[NB perf] reset\n");
		return;
	}
	perf::Report();
}
