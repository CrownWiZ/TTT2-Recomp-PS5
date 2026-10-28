#pragma once
// Boot splash: an attribution screen the runtime shows before the game's own
// video output reaches the screen. The game keeps booting underneath; while the
// splash is active the present paths show these pixels instead of the game's
// frontbuffer, so the credits land before the publisher logo sequence.
// Duration: TTT2_SPLASH_MS (default 3500; 0 disables).
#include <cstdint>
namespace splash {
// True while the boot splash should be on screen.
bool Active();
// 1280x720 BGRX pixels (valid while Active(); rendered once on first use).
const uint32_t* Pixels();
}
