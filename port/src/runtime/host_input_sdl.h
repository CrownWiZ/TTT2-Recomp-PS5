#pragma once

#include <SDL3/SDL.h>
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
void Event(const SDL_Event& event);
Snapshot Read();
bool KeyDown(int legacyKey);
bool AnyKeyDown();
bool Focused();

// Existing keyboard settings use Windows virtual-key numbers. Keep them stable
// while SDL takes ownership of event collection on all host platforms.
int LegacyKey(SDL_Keycode key);
void ApplyGamepadButton(Pad& pad, SDL_GamepadButton button, bool down);
void ApplyGamepadAxis(Pad& pad, SDL_GamepadAxis axis, int16_t value);

} // namespace host_input
