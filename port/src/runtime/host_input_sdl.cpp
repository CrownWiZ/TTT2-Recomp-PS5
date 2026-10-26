#include "host_input_sdl.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace host_input {
namespace {
constexpr int kShift = 0x10, kControl = 0x11, kAlt = 0x12;
constexpr int kLeftShift = 0xA0, kRightShift = 0xA1;
constexpr int kLeftControl = 0xA2, kRightControl = 0xA3;
constexpr int kLeftAlt = 0xA4, kRightAlt = 0xA5;

struct Slot {
    SDL_JoystickID id = 0;
    SDL_Gamepad* handle = nullptr;
};

std::mutex g_mutex;
Snapshot g_snapshot;
std::array<Slot, 4> g_slots;
struct TestPress {
    uint64_t atMs;
    uint64_t untilMs;
    SDL_GamepadButton button;
};
SDL_JoystickID g_testPadId = 0;
SDL_Joystick* g_testJoystick = nullptr;
std::vector<TestPress> g_testPresses;
uint32_t g_testButtons = 0;
std::chrono::steady_clock::time_point g_testStart;
void AddGamepad(SDL_JoystickID id);

SDL_GamepadButton TestButton(const std::string& name)
{
    if (name == "START") return SDL_GAMEPAD_BUTTON_START;
    if (name == "BACK") return SDL_GAMEPAD_BUTTON_BACK;
    if (name == "A") return SDL_GAMEPAD_BUTTON_SOUTH;
    if (name == "B") return SDL_GAMEPAD_BUTTON_EAST;
    if (name == "X") return SDL_GAMEPAD_BUTTON_WEST;
    if (name == "Y") return SDL_GAMEPAD_BUTTON_NORTH;
    if (name == "UP") return SDL_GAMEPAD_BUTTON_DPAD_UP;
    if (name == "DOWN") return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    if (name == "LEFT") return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    if (name == "RIGHT") return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    if (name == "LB") return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    if (name == "RB") return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    return SDL_GAMEPAD_BUTTON_INVALID;
}

void StartTestGamepad(const char* script)
{
    if (!script || !*script) return;
    const std::string all(script);
    for (size_t pos = 0; pos < all.size();) {
        const size_t end = std::min(all.find(',', pos), all.size());
        const std::string item = all.substr(pos, end - pos);
        const size_t first = item.find(':');
        const size_t second = first == std::string::npos ? first : item.find(':', first + 1);
        if (first != std::string::npos && second != std::string::npos) {
            const uint64_t at = std::strtoull(item.c_str(), nullptr, 10);
            const uint64_t hold = std::strtoull(item.c_str() + second + 1, nullptr, 10);
            const SDL_GamepadButton button = TestButton(item.substr(first + 1, second - first - 1));
            if (button != SDL_GAMEPAD_BUTTON_INVALID && hold)
                g_testPresses.push_back({at, at + hold, button});
        }
        pos = end + 1;
    }
    if (g_testPresses.empty()) return;
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "TTT2 SDL test gamepad";
    g_testPadId = SDL_AttachVirtualJoystick(&desc);
    if (!g_testPadId) {
        std::fprintf(stderr, "[input] SDL test gamepad attach: %s\n", SDL_GetError());
        g_testPresses.clear();
        return;
    }
    AddGamepad(g_testPadId);
    SDL_Gamepad* gamepad = SDL_GetGamepadFromID(g_testPadId);
    g_testJoystick = gamepad ? SDL_GetGamepadJoystick(gamepad) : nullptr;
    g_testStart = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[input] SDL test gamepad script: %zu presses\n", g_testPresses.size());
}

int FindSlot(SDL_JoystickID id)
{
    for (int i = 0; i < int(g_slots.size()); ++i)
        if (g_slots[i].id == id) return i;
    return -1;
}

void AddGamepad(SDL_JoystickID id)
{
    if (!id) return;
    SDL_Gamepad* handle = nullptr;
    int slot = -1;
    {
        std::lock_guard lock(g_mutex);
        if (FindSlot(id) >= 0) return;
        for (int i = 0; i < int(g_slots.size()); ++i)
            if (!g_slots[i].id) { slot = i; break; }
    }
    if (slot < 0) return;
    handle = SDL_OpenGamepad(id);
    if (!handle) {
        std::fprintf(stderr, "[input] SDL_OpenGamepad(%u): %s\n", unsigned(id), SDL_GetError());
        return;
    }
    {
        std::lock_guard lock(g_mutex);
        if (g_slots[slot].id) {
            SDL_CloseGamepad(handle);
            return;
        }
        g_slots[slot] = {id, handle};
        g_snapshot.pads[slot] = {};
        g_snapshot.pads[slot].connected = true;
    }
    std::fprintf(stderr, "[input] SDL gamepad %u assigned host slot %d\n", unsigned(id), slot);
}

void RemoveGamepad(SDL_JoystickID id)
{
    SDL_Gamepad* handle = nullptr;
    {
        std::lock_guard lock(g_mutex);
        const int slot = FindSlot(id);
        if (slot < 0) return;
        handle = g_slots[slot].handle;
        g_slots[slot] = {};
        g_snapshot.pads[slot] = {};
    }
    SDL_CloseGamepad(handle);
    std::fprintf(stderr, "[input] SDL gamepad %u disconnected\n", unsigned(id));
}

uint16_t ButtonMask(SDL_GamepadButton button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 0x0001;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 0x0002;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 0x0004;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 0x0008;
    case SDL_GAMEPAD_BUTTON_START: return 0x0010;
    case SDL_GAMEPAD_BUTTON_BACK: return 0x0020;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return 0x0040;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return 0x0080;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 0x0100;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 0x0200;
    case SDL_GAMEPAD_BUTTON_GUIDE: return 0x0400;
    case SDL_GAMEPAD_BUTTON_SOUTH: return 0x1000;
    case SDL_GAMEPAD_BUTTON_EAST: return 0x2000;
    case SDL_GAMEPAD_BUTTON_WEST: return 0x4000;
    case SDL_GAMEPAD_BUTTON_NORTH: return 0x8000;
    default: return 0;
    }
}

int16_t InvertY(int16_t value)
{
    return int16_t(std::min(32767, -int(value)));
}

} // namespace

void ApplyGamepadButton(Pad& pad, SDL_GamepadButton button, bool down)
{
    const uint16_t mask = ButtonMask(button);
    if (down) pad.buttons |= mask;
    else pad.buttons &= ~mask;
}

void ApplyGamepadAxis(Pad& pad, SDL_GamepadAxis axis, int16_t value)
{
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: pad.leftX = value; break;
    case SDL_GAMEPAD_AXIS_LEFTY: pad.leftY = InvertY(value); break;
    case SDL_GAMEPAD_AXIS_RIGHTX: pad.rightX = value; break;
    case SDL_GAMEPAD_AXIS_RIGHTY: pad.rightY = InvertY(value); break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        pad.leftTrigger = uint8_t(std::clamp(int(value), 0, 32767) * 255 / 32767); break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        pad.rightTrigger = uint8_t(std::clamp(int(value), 0, 32767) * 255 / 32767); break;
    default: break;
    }
}

int LegacyKey(SDL_Keycode key)
{
    if (key >= SDLK_A && key <= SDLK_Z) return 'A' + int(key - SDLK_A);
    if (key >= SDLK_0 && key <= SDLK_9) return int(key);
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return 0x61 + int(key - SDLK_KP_1);
    if (key >= SDLK_F1 && key <= SDLK_F12) return 0x70 + int(key - SDLK_F1);
    if (key >= SDLK_F13 && key <= SDLK_F24) return 0x7C + int(key - SDLK_F13);
    switch (key) {
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB: return 0x09;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0D;
    case SDLK_ESCAPE: return 0x1B;
    case SDLK_SPACE: return 0x20;
    case SDLK_PAGEUP: return 0x21;
    case SDLK_PAGEDOWN: return 0x22;
    case SDLK_END: return 0x23;
    case SDLK_HOME: return 0x24;
    case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;
    case SDLK_PRINTSCREEN: return 0x2C;
    case SDLK_INSERT: return 0x2D;
    case SDLK_DELETE: return 0x2E;
    case SDLK_LGUI: return 0x5B;
    case SDLK_RGUI: return 0x5C;
    case SDLK_APPLICATION: return 0x5D;
    case SDLK_KP_0: return 0x60;
    case SDLK_KP_MULTIPLY: return 0x6A;
    case SDLK_KP_PLUS: return 0x6B;
    case SDLK_KP_MINUS: return 0x6D;
    case SDLK_KP_PERIOD: return 0x6E;
    case SDLK_KP_DIVIDE: return 0x6F;
    case SDLK_NUMLOCKCLEAR: return 0x90;
    case SDLK_SCROLLLOCK: return 0x91;
    case SDLK_LSHIFT: return kLeftShift;
    case SDLK_RSHIFT: return kRightShift;
    case SDLK_LCTRL: return kLeftControl;
    case SDLK_RCTRL: return kRightControl;
    case SDLK_LALT: return kLeftAlt;
    case SDLK_RALT: return kRightAlt;
    case SDLK_SEMICOLON: return 0xBA;
    case SDLK_EQUALS: return 0xBB;
    case SDLK_COMMA: return 0xBC;
    case SDLK_MINUS: return 0xBD;
    case SDLK_PERIOD: return 0xBE;
    case SDLK_SLASH: return 0xBF;
    case SDLK_GRAVE: return 0xC0;
    case SDLK_LEFTBRACKET: return 0xDB;
    case SDLK_BACKSLASH: return 0xDC;
    case SDLK_RIGHTBRACKET: return 0xDD;
    case SDLK_APOSTROPHE: return 0xDE;
    default: return 0;
    }
}

bool Start()
{
    // The scripted pad is used by unattended tests while the desktop may move focus.
    if (const char* script = std::getenv("TTT2_SDL_TEST_PAD_SCRIPT"); script && *script)
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "[input] SDL gamepad init: %s\n", SDL_GetError());
        return false;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) AddGamepad(ids[i]);
    SDL_free(ids);
    StartTestGamepad(std::getenv("TTT2_SDL_TEST_PAD_SCRIPT"));
    return true;
}

void Stop()
{
    for (const auto& slot : g_slots)
        if (slot.id) RemoveGamepad(slot.id);
    {
        std::lock_guard lock(g_mutex);
        g_snapshot = {};
    }
    if (g_testPadId) SDL_DetachVirtualJoystick(g_testPadId);
    g_testPadId = 0;
    g_testJoystick = nullptr;
    g_testPresses.clear();
    g_testButtons = 0;
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void Tick()
{
    if (!g_testJoystick) return;
    const uint64_t elapsed = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_testStart).count());
    uint32_t buttons = 0;
    for (const auto& press : g_testPresses)
        if (elapsed >= press.atMs && elapsed < press.untilMs)
            buttons |= 1u << unsigned(press.button);
    if (buttons == g_testButtons) return;
    const uint32_t changed = buttons ^ g_testButtons;
    for (unsigned button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
        if (changed & (1u << button)) {
            if (!SDL_SetJoystickVirtualButton(g_testJoystick, int(button),
                                               (buttons & (1u << button)) != 0))
                std::fprintf(stderr, "[input] SDL virtual button %u: %s\n", button, SDL_GetError());
        }
    g_testButtons = buttons;
    std::fprintf(stderr, "[input] SDL test gamepad buttons %08X at %llums\n",
                 buttons, (unsigned long long)elapsed);
}

void SetFocused(bool focused)
{
    std::lock_guard lock(g_mutex);
    g_snapshot.focused = focused;
    if (!focused) g_snapshot.keys.fill(false);
}

void Event(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED: SetFocused(true); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: SetFocused(false); break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const int key = LegacyKey(event.key.key);
        if (key <= 0 || key >= 256) break;
        std::lock_guard lock(g_mutex);
        if (!g_snapshot.focused) break;
        g_snapshot.keys[key] = event.type == SDL_EVENT_KEY_DOWN;
        g_snapshot.keys[kShift] = g_snapshot.keys[kLeftShift] || g_snapshot.keys[kRightShift];
        g_snapshot.keys[kControl] = g_snapshot.keys[kLeftControl] || g_snapshot.keys[kRightControl];
        g_snapshot.keys[kAlt] = g_snapshot.keys[kLeftAlt] || g_snapshot.keys[kRightAlt];
        break;
    }
    case SDL_EVENT_GAMEPAD_ADDED: AddGamepad(event.gdevice.which); break;
    case SDL_EVENT_GAMEPAD_REMOVED: RemoveGamepad(event.gdevice.which); break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        std::lock_guard lock(g_mutex);
        const int slot = FindSlot(event.gbutton.which);
        if (slot < 0) break;
        ApplyGamepadButton(g_snapshot.pads[slot], SDL_GamepadButton(event.gbutton.button),
                           event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        if (const char* timing = std::getenv("TTT2_INPUT_TIMING"); timing && *timing == '1')
            std::fprintf(stderr, "[input] SDL pad event slot=%d button=%u down=%u mapped=%04X\n",
                         slot, unsigned(event.gbutton.button),
                         unsigned(event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN),
                         g_snapshot.pads[slot].buttons);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        std::lock_guard lock(g_mutex);
        const int slot = FindSlot(event.gaxis.which);
        if (slot >= 0)
            ApplyGamepadAxis(g_snapshot.pads[slot], SDL_GamepadAxis(event.gaxis.axis), event.gaxis.value);
        break;
    }
    default: break;
    }
}

Snapshot Read()
{
    std::lock_guard lock(g_mutex);
    return g_snapshot;
}

bool KeyDown(int legacyKey)
{
    if (legacyKey <= 0 || legacyKey >= 256) return false;
    std::lock_guard lock(g_mutex);
    return g_snapshot.focused && g_snapshot.keys[legacyKey];
}

bool AnyKeyDown()
{
    std::lock_guard lock(g_mutex);
    if (!g_snapshot.focused) return false;
    for (int key = 8; key < 256; ++key)
        if (g_snapshot.keys[key]) return true;
    return false;
}

bool Focused()
{
    std::lock_guard lock(g_mutex);
    return g_snapshot.focused;
}

} // namespace host_input
