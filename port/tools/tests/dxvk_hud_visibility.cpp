// Compile with -I<prepared-dxvk-source>/src/dxvk/hud, then run with a temporary
// settings-file path. This checks the production helper without a GPU or SDL.
#include "dxvk_hud_visibility.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
  assert(argc == 2);
  using Gate = dxvk::hud::HudVisibilityGate;
  using namespace std::chrono_literals;
  const std::filesystem::path setting(argv[1]);
  const auto staging = setting.string() + ".new";
  const auto start = Gate::Clock::now();
  const auto write = [&](const char* value) {
    { std::ofstream output(staging); output << value; assert(output.good()); }
    assert(std::rename(staging.c_str(), setting.c_str()) == 0);
  };

  Gate ordinaryHud("");
  assert(ordinaryHud.visible(start)); // Hosts without an opt-in path are unchanged.

  std::filesystem::remove(setting);
  Gate liveHud(setting.string());
  assert(!liveHud.visible(start)); // A fresh or missing setting defaults off.
  write("1\n");
  assert(!liveHud.visible(start + 249ms)); // Changes wait for the next poll.
  assert(liveHud.visible(start + 250ms));

  // An incomplete atomic write must leave the currently published value intact.
  { std::ofstream output(staging); output << "0"; }
  assert(liveHud.visible(start + 500ms));
  assert(std::rename(staging.c_str(), setting.c_str()) == 0);
  assert(liveHud.visible(start + 749ms));
  assert(!liveHud.visible(start + 750ms)); // Disabling the running HUD applies.

  write("1\n");
  assert(liveHud.visible(start + 1000ms)); // Re-enabling needs no new HUD object.
  write("invalid\n");
  assert(!liveHud.visible(start + 1250ms));
  write("");
  assert(!liveHud.visible(start + 1500ms));
  write("1\n");
  assert(liveHud.visible(start + 1750ms));
  std::filesystem::remove(setting);
  assert(!liveHud.visible(start + 2000ms));
}
