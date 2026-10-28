#pragma once
#include <SDL3/SDL.h>
#include "host_input_sdl.h"

// SDL event-thread glue of the input layer: the pieces that need SDL types
// live here, so snapshot consumers (kernel modules) never include SDL.
namespace host_input {

void Event(const SDL_Event& event);

// Existing keyboard settings use Windows virtual-key numbers. Keep them stable
// while SDL takes ownership of event collection on all host platforms.
int LegacyKey(SDL_Keycode key);
void ApplyGamepadButton(Pad& pad, SDL_GamepadButton button, bool down);
void ApplyGamepadAxis(Pad& pad, SDL_GamepadAxis axis, int16_t value);

} // namespace host_input
