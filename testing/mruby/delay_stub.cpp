/**
 * Minimal nanosleep-based implementation of love::sleep for the mruby-port
 * standalone harness, so the timer slice can be exercised without linking SDL.
 * The real build uses src/common/delay.cpp (SDL-backed).
 */

#include "common/delay.h"

#include <ctime>

namespace love
{

// Note: the deprecation subsystem (initDeprecation/deinitDeprecation) is now
// provided by the real common/deprecation.cpp, linked for the graphics backend.

void sleep(double ms)
{
	struct timespec ts;
	ts.tv_sec = (time_t)(ms / 1000.0);
	ts.tv_nsec = (long)((ms - ts.tv_sec * 1000.0) * 1000000.0);
	nanosleep(&ts, nullptr);
}

} // love
