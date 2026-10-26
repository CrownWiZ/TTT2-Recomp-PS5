"""Build the production SDL output and run its independent audio-device probe.

SDL's dummy driver exercises the real queue, mixing, callback and close path
without requiring speakers or game data. Set CMAKE_PREFIX_PATH or SDL3_DIR on
Windows so CMake can find the SDL3 development package.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "port/src/runtime"
PROBE = ROOT / "port/tools/sdl_audio_probe.cpp"


def main():
    with tempfile.TemporaryDirectory(prefix="ttt2-sdl-audio-") as temp:
        folder = Path(temp)
        source = folder / "source"
        build = folder / "build"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\n"
            "project(ttt2_sdl_audio_probe CXX)\n"
            "set(CMAKE_CXX_STANDARD 20)\n"
            "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
            "find_package(SDL3 3.4.0 REQUIRED CONFIG)\n"
            f"add_executable(ttt2_sdl_audio_probe \"{RUNTIME / 'audio_output.cpp'}\" \"{PROBE}\")\n"
            f"target_include_directories(ttt2_sdl_audio_probe PRIVATE \"{RUNTIME}\")\n"
            "target_link_libraries(ttt2_sdl_audio_probe PRIVATE SDL3::SDL3)\n"
        )
        configure = ["cmake", "-S", str(source), "-B", str(build)]
        if shutil.which("ninja"):
            configure += ["-G", "Ninja"]
        subprocess.run(configure, check=True)
        subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
        exe = build / ("ttt2_sdl_audio_probe.exe" if os.name == "nt" else "ttt2_sdl_audio_probe")
        env = dict(os.environ, SDL_AUDIODRIVER="dummy")
        subprocess.run([str(exe)], env=env, check=True, timeout=15)
        subprocess.run([str(exe)], env=dict(env, TTT2_AUDIO_SILENT="1"),
                       check=True, timeout=15)


if __name__ == "__main__":
    main()
