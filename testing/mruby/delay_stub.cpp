/**
 * Minimal nanosleep-based implementation of love::sleep for the mruby-port
 * standalone harness, so the timer slice can be exercised without linking SDL.
 * The real build uses src/common/delay.cpp (SDL-backed).
 */

#include "common/delay.h"
#include "common/deprecation.h"

#include <ctime>

namespace love
{

// The deprecation subsystem is SDL-thread-backed in the real build; stub it for
// the standalone harness.
void initDeprecation() {}
void deinitDeprecation() {}

void sleep(double ms)
{
	struct timespec ts;
	ts.tv_sec = (time_t)(ms / 1000.0);
	ts.tv_nsec = (long)((ms - ts.tv_sec * 1000.0) * 1000000.0);
	nanosleep(&ts, nullptr);
}

} // love
