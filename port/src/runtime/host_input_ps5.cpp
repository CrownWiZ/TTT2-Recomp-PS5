// PS5 host input (interface: host_input.h API used by the shared kernel).
//
// scePad feeds a locked snapshot exactly the way the SDL build's event thread
// does. Buttons map to the Xbox style values the guest ABI expects (the same
// mapping the SDL ApplyGamepadButton helpers produce). Keyboard emulation is
// empty: the console has no keyboard in the M4 scope.
#include "host_input_sdl.h"
#include "kernel/sce_ps5.h"
#include "host_thread.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

// Xbox-style button values (the values the guest's XInput layer expects).
inline constexpr uint16_t kButtonDpadUp = 1, kButtonDpadDown = 2, kButtonDpadLeft = 4, kButtonDpadRight = 8;
inline constexpr uint16_t kButtonStart = 0x10, kButtonBack = 0x20, kButtonL3 = 0x40, kButtonR3 = 0x80;
inline constexpr uint16_t kButtonLb = 0x100, kButtonRb = 0x200, kButtonA = 0x1000,
                            kButtonB = 0x2000, kButtonX = 0x4000, kButtonY = 0x8000;

namespace host_input {

namespace {
struct State
{
    std::mutex mutex;
    Snapshot snapshot;
    int32_t padHandle = -1;
    int32_t userId = -1;
    bool started = false;
};
State& InputState() { static auto* state = new State; return *state; }

void ApplyButtons(Pad& pad, uint32_t sceButtons)
{
    uint16_t& buttons = pad.buttons;
    auto set = [&](uint16_t value, unsigned flag) { if (flag) buttons |= value; else buttons &= ~value; };
    set(kButtonDpadUp, SCE_PAD_BUTTON_UP & sceButtons);
    set(kButtonDpadDown, SCE_PAD_BUTTON_DOWN & sceButtons);
    set(kButtonDpadLeft, SCE_PAD_BUTTON_LEFT & sceButtons);
    set(kButtonDpadRight, SCE_PAD_BUTTON_RIGHT & sceButtons);
    set(kButtonStart, SCE_PAD_BUTTON_OPTIONS & sceButtons);
    set(kButtonBack, SCE_PAD_BUTTON_TOUCH_PAD & sceButtons);
    set(kButtonL3, SCE_PAD_BUTTON_L3 & sceButtons);
    set(kButtonR3, SCE_PAD_BUTTON_R3 & sceButtons);
    set(kButtonLb, SCE_PAD_BUTTON_L1 & sceButtons);
    set(kButtonRb, SCE_PAD_BUTTON_R1 & sceButtons);
    // The guest's layout: A/X swaps to the face buttons the way the DualSense
    // layout is presented: cross = A, circle = B, square = X, triangle = Y.
    set(kButtonA, SCE_PAD_BUTTON_CROSS & sceButtons);
    set(kButtonB, SCE_PAD_BUTTON_CIRCLE & sceButtons);
    set(kButtonX, SCE_PAD_BUTTON_SQUARE & sceButtons);
    set(kButtonY, SCE_PAD_BUTTON_TRIANGLE & sceButtons);
    pad.connected = true;
}

void PumpPad()
{
    auto& state = InputState();
    std::lock_guard lock(state.mutex);
    if (state.padHandle < 0) return;
    constexpr int kSamples = 8;
    std::array<ScePadData, kSamples> data{};
    const int got = scePadRead(state.padHandle, data.data(), kSamples);
    if (got <= 0) { state.snapshot.pads[0].connected = true; return; }
    const ScePadData& last = data[got - 1];
    ApplyButtons(state.snapshot.pads[0], last.buttons);
    const float lx = last.analogLeft.x, ly = last.analogLeft.y;
    const float rx = last.analogRight.x, ry = last.analogRight.y;
    state.snapshot.pads[0].leftX  = int16_t(std::clamp(lx, -1.0f, 1.0f) * 32767.0f);
    state.snapshot.pads[0].leftY  = int16_t(std::clamp(-ly, -1.0f, 1.0f) * 32767.0f);
    state.snapshot.pads[0].rightX = int16_t(std::clamp(rx, -1.0f, 1.0f) * 32767.0f);
    state.snapshot.pads[0].rightY = int16_t(std::clamp(-ry, -1.0f, 1.0f) * 32767.0f);
    // The triggers are the guest's analog shoulders, straight from the sticks.
    state.snapshot.pads[0].leftTrigger  = uint8_t(std::clamp(ly < 0.0f ? 0.0f : ly, 0.0f, 1.0f) * 255.0f);
    state.snapshot.pads[0].rightTrigger = uint8_t(std::clamp(ry < 0.0f ? 0.0f : ry, 0.0f, 1.0f) * 255.0f);
}
} // namespace

bool Start()
{
    auto& state = InputState();
    std::lock_guard lock(state.mutex);
    if (state.started) return true;
    if (scePadInit() != 0)
    {
        std::fprintf(stderr, "[input] scePadInit failed\n");
        return false;
    }
    int32_t userId = -1;
    if (sceUserServiceGetInitialUser(&userId) != 0) userId = -1;
    state.userId = userId;
    state.padHandle = scePadOpen(userId, SCE_PAD_PORT_TYPE_STANDARD, 0, nullptr);
    if (state.padHandle < 0)
    {
        std::fprintf(stderr, "[input] scePadOpen failed\n");
        return false;
    }
    state.snapshot.focused = true;
    state.started = true;
    std::fprintf(stderr, "[input] DualSense open (handle %d)\n", state.padHandle);
    return true;
}

void Stop()
{
    auto& state = InputState();
    std::lock_guard lock(state.mutex);
    if (!state.started) return;
    if (state.padHandle >= 0) scePadClose(state.padHandle);
    state.padHandle = -1;
    state.started = false;
}

void Tick() { PumpPad(); }

void SetFocused(bool focused)
{
    auto& state = InputState();
    std::lock_guard lock(state.mutex);
    state.snapshot.focused = focused;
}

Snapshot Read()
{
    auto& state = InputState();
    std::lock_guard lock(state.mutex);
    return state.snapshot;
}

bool KeyDown(int legacyKey)
{
    if (legacyKey < 0 || legacyKey >= 256) return false;
    auto& state = InputState();
    std::lock_guard lock(state.mutex);
    return state.snapshot.keys[size_t(legacyKey)];
}

bool AnyKeyDown()
{
    auto& state = InputState();
    std::lock_guard lock(state.mutex);
    for (const bool down : state.snapshot.keys)
        if (down) return true;
    return false;
}

bool Focused()
{
    auto& state = InputState();
    std::lock_guard lock(state.mutex);
    return state.snapshot.focused;
}

} // namespace host_input
