// PC display settings chosen in Options -> Display Settings (DISPLAY MODE, RESOLUTION): the
// state, the resolution list and their persistence in %LOCALAPPDATA%\TTT2Recomp\settings.ini.
// The menu natives (menu_ext.cpp) read and write it; the presenter applies it.
#pragma once
#include <cstdint>
#include <vector>

namespace display_settings {

enum Mode : int { Windowed = 0, Borderless = 1, Fullscreen = 2 };

struct Resolution { uint32_t width, height; };

// 16:9 render resolutions offered in the menu (fixed list; index = menu cursor).
const std::vector<Resolution>& Resolutions();

int GetMode();
int GetResolutionIndex();
Resolution GetResolution();
void SetMode(int mode);                 // clamps, persists, notifies the presenter
void SetResolutionIndex(int index);     // clamps, persists, notifies the presenter

// Bumped on every change; the presenter compares it to apply new settings once.
uint32_t Generation();

} // namespace display_settings
