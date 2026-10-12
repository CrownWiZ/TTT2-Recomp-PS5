# Tekken Tag Tournament 2 — native PC port

A native Windows port of the Xbox 360 game. **Not an emulator**: the game's PowerPC code is
statically recompiled to C++ and built as a native x64 executable, on a runtime that replaces
the Xbox 360 kernel, GPU, XMA audio, storage and Xbox LIVE with Windows implementations
(Direct3D 11, XAudio2, FFmpeg's XMA decoder, Win32 files, Steam networking).

**This repository contains no game data and no game code.** You supply your own disc image and
the game's Title Update 3; the installer builds every game file on your PC from them. The disc
image, title updates, extracted archives and recompiled game code are never committed here.

---

## Installing

1. Download the installer from [Releases](../../releases).
2. Run it and choose:
   - your own **Tekken Tag Tournament 2 (USA, Europe)** Xbox 360 disc image (`.iso`), and
   - your own **Title Update 3**: the file `TU_174Q22P_0000008000000.00000000000O2` (it ends in
     the letter **O**, then **2**), or the folder holding your title updates — the installer
     picks TU3 out by itself.
3. It checks both against the retail versions the port was built from, builds the game files
   (about 13 GB), and creates Desktop and Start-menu shortcuts.

Windows 10 or 11, 64-bit, and a DirectX 11 graphics card. No redistributables to install. The
installer is not code-signed, so Windows SmartScreen may warn: **More info → Run anyway**.

Saves, profiles, settings, the shader cache and `log.txt` live in `%LOCALAPPDATA%\TTT2Recomp`,
so they survive uninstalling (unless you tick the box) and a Program Files install can write them.

## What works

- **The game, offline**: Arcade and Versus battles play start to finish; the other modes (Fight
  Lab, Customize, Gallery, Tekken Tunes) are on the menu but less tested.
- **All 59 fighters**, including the 10 that were unlocked by Namco's servers, and Title Update 3's
  balance and fixes.
- **Hardware rendering** (Direct3D 11) at a true **render resolution of 1x to 3x** 720p, at 60 fps,
  **windowed, borderless or full screen** — set in the game's own `OPTIONS → DISPLAY SETTINGS`.
- **XMA music and sound**, movies, saving, and **profiles** handled inside the game's own menus.
- **Controllers** (any XInput pad; whoever presses a button first is player 1) and the keyboard.
- **Online Player Match over Steam** — see below.

### Keyboard

| Key | Button | Key | Button |
|---|---|---|---|
| Arrow keys | D-pad | A / S / Z / X | A / B / X / Y |
| Enter or Space | START | Q / W | LB / RB |
| Backspace | BACK | Alt+Enter | windowed ↔ borderless |

## Online (Steam)

Player Match is played peer to peer over Steam: no port forwarding, no Xbox LIVE.

- Keep the **Steam client running** on both PCs, signed in to **two different accounts**.
- Both players install from **the same installer version**.
- **Host:** `ONLINE MODE → PLAYER MATCH → CREATE SESSION` (accept Namco's Online Mode terms the
  first time).
- **Join:** `ONLINE MODE → PLAYER MATCH → SEARCH SESSION`, and pick the host's session.
- Steam shows you as playing **Spacewar**: the port uses Valve's public test app id (480) for now.

Steam runs in a small helper process the game starts next to itself (a second
`Tekken Tag Tournament 2.exe` in Task Manager). That is deliberate: Steam Input hides the
controllers from whichever process initialises Steam as Spacewar.

Not available: leaderboards, World Arena / online lobby, replays and ghosts (they need Namco's
servers), and voice chat. Ranked matches and Steam's "Invite to Game" / "Join Game" are untested.

## Building from source

This is a static recompilation, so building needs your own copy of the game too: the recompiled
C++ is generated on your machine from your `Default.xex` + TU3, never stored here.

Requirements: Visual Studio 2022 Build Tools (MSVC x64), LLVM/Clang (clang-cl), the CMake +
Ninja bundled with Visual Studio, Python 3.10+. The scripts expect the checkout at `C:\ttt2`
(a junction is fine: `mklink /J C:\ttt2 <checkout>`), because some tools choke on spaces and
parentheses in paths.

1. **Your game files.** Put your disc's `Default.xex` at `re\Default.xex` and extract Title
   Update 3 (files from `Updates\` → `re\extracted_tu\`) with `python port\tools\stfs.py`.
2. **The recompiler.** Clone [XenonRecomp](https://github.com/hedge-dev/XenonRecomp)
   (`--recursive`) into `tools\XenonRecomp`, check out `ddd128b`, apply
   `tools\patches\XenonRecomp.patch` (`git apply`), and run `tools\build_xenonrecomp.cmd`.
3. **Generate the C++** (into `re\ppc\`): XenonAnalyse on the TU3-patched image →
   `python port\tools\gen_missing_switches.py` → `XenonRecomp re\recomp\tekken.toml
   tools\XenonRecomp\XenonUtils\ppc_context.h` → `python port\tools\gen_missing_functions.py`
   and re-run XenonRecomp until no `// ERROR` sites remain → `python port\tools\debug_probes.py`
   (applies the runtime's generated-code patches) → `python port\tools\check_recomp_acceptance.py`
   → `python port\tools\gen_kernel_stubs.py`.
4. **FFmpeg** n7.1 source in `tools\ffmpeg_dl\FFmpeg-n7.1`, configured as described at the top of
   `port\third_party\ffmpeg_xma\CMakeLists.txt`.
5. **Steamworks SDK** 1.65 in `tools\steamworks_sdk_165\sdk` (optional; without it the build has
   no Steam online).
6. Build: `tools\build_port.cmd build-o1 1 8 ON` (the game), then `tools\build_setup.cmd build-o1 8`
   (the installer, `port\build-o1\setup\ttt2_setup.exe`). Installer details: `port\INSTALLER.md`.

To run a development build without regenerating anything, install a release once and point the
build at it: `set TTT2_VFS_ROOT=<install folder>\game`, then
`port\build-o1\tekken_recomp.exe "<install folder>\game\tekken.img"`.

## Credits

[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) (MIT) — the recompiler, extended here
for this game. [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp) and
[Xenia](https://github.com/xenia-project/xenia) for reference. FFmpeg (LGPL) for XMA decoding,
libmspack (LGPL) for LZX, tiny-AES-c, Steamworks for networking.

## Legal

Unofficial, non-commercial, and not affiliated with or endorsed by Bandai Namco or Microsoft.
You must own the game. Tekken and all related characters and content are the property of their
respective owners. `steam_api64.dll` is © Valve Corporation, redistributed under the Steamworks
SDK licence.
