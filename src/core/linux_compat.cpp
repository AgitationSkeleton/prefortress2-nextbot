// pf2-nextbot: Linux-only link shims.
//
// The hl2sdk's prebuilt lib/public/linux32/mathlib.a was compiled with -ffast-math against an old
// glibc, so it references the __*_finite math entry points. glibc 2.31+ no longer provides them to
// newly linked binaries. They are the same functions without the errno handling, so forward them.
// The symbols stay local to the plugin (-fvisibility=hidden, --exclude-libs).
#if defined(__linux__)
#include <math.h>

extern "C" {
double __pow_finite(double x, double y) { return pow(x, y); }
double __log_finite(double x) { return log(x); }
double __acos_finite(double x) { return acos(x); }
double __asin_finite(double x) { return asin(x); }
double __atan2_finite(double y, double x) { return atan2(y, x); }
float __acosf_finite(float x) { return acosf(x); }
float __atan2f_finite(float y, float x) { return atan2f(y, x); }
}
#endif
