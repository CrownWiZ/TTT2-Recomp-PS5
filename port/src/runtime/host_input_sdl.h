#pragma once

// Snapshot-only contract: this header is included by kernel modules on every
// host platform, so it carries no SDL types. The SDL event-thread glue
// (Event/LegacyKey/Apply*) moved to host_input_sdl_events.h, included only by
// host_window_sdl.cpp.
#include <array>
#include <cstdint>

namespace host_input {

// Xbox-style values at the host boundary. The guest ABI is written separately.
struct Pad {
    bool connected = false;
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    int16_t leftX = 0;
    int16_t leftY = 0;
    int16_t rightX = 0;
    int16_t rightY = 0;
};

struct Snapshot {
    bool focused = false;
    std::array<bool, 256> keys{}; // legacy settings.ini virtual-key values
    std::array<Pad, 4> pads{};
};

// Call from the SDL event thread. Event only changes a small locked snapshot;
// the guest reads it without calling into SDL or the OS driver.
bool Start();
void Stop();
void Tick();
void SetFocused(bool focused);
Snapshot Read();
bool KeyDown(int legacyKey);
bool AnyKeyDown();
bool Focused();

} // namespace host_input
