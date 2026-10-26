#!/usr/bin/env python3
"""Build the generated game PPC code as an optimized Android ARM64 archive."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


def ninja_path(path: Path) -> str:
    return str(path).replace("$", "$$").replace(" ", "$ ").replace(":", "$:")


def write_if_changed(path: Path, content: str) -> None:
    if not path.is_file() or path.read_text() != content:
        path.write_text(content)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ppc-dir", type=Path, required=True)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument("--opt-level", choices=("O1", "O2", "O3"), default="O1")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    tools = args.ndk.resolve() / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    compiler = tools / "aarch64-linux-android33-clang++"
    archiver = tools / "llvm-ar"
    if not compiler.is_file() or not archiver.is_file():
        parser.error(f"Android ARM64 toolchain not found under {args.ndk}")

    ppc_dir = args.ppc_dir.resolve(strict=True)
    source_root = Path(__file__).resolve().parents[2]
    build_dir = args.build_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)
    header_dir = build_dir / "headers"
    subprocess.run(
        [sys.executable, str(source_root / "port/tools/prepare_android_ppc_headers.py"),
         "--ppc-dir", str(ppc_dir), "--output-dir", str(header_dir)], check=True)

    sources = sorted(ppc_dir.glob("ppc_recomp.*.cpp"))
    mapping = ppc_dir / "ppc_func_mapping.cpp"
    if not sources or not mapping.is_file():
        parser.error(f"generated PPC sources are missing from {ppc_dir}")
    split = subprocess.run(
        [sys.executable, str(source_root / "port/tools/split_ppc_units.py"),
         "--output-dir", str(build_dir / "split"), "--threshold-bytes", "4194304",
         "--chunk-bytes", "2097152", *(str(path) for path in sources), str(mapping)],
        check=True, text=True, capture_output=True)
    compile_sources = [Path(line) for line in split.stdout.splitlines()]

    # The generated code lives beside the original checkout, while this script
    # lives in an independent stacked worktree. Resolve support files beside
    # the generated sources rather than beside the worktree.
    utils_dir = ppc_dir.parents[1] / "tools/XenonRecomp/XenonUtils"
    simde_dir = ppc_dir.parents[1] / "tools/XenonRecomp/thirdparty/simde"
    for directory in (utils_dir, simde_dir):
        if not directory.is_dir():
            parser.error(f"missing generated PPC dependency: {directory}")

    flags = ["-std=c++20", f"-{args.opt_level}", "-march=armv8-a", "-ffp-contract=off", "-fPIC",
             "-fno-exceptions", "-fno-rtti", "-finline", "-Wno-everything"]
    for directory in (header_dir, ppc_dir, utils_dir, simde_dir,
                      source_root / "port/src/runtime"):
        flags.extend(["-I", str(directory)])
    flags.extend(["-include", str(header_dir / "ppc_arm64_compat.h"),
                  "-include", str(source_root / "port/src/runtime/ppc_store_guard_linux.h")])

    objects_dir = build_dir / "objects"
    objects_dir.mkdir(exist_ok=True)
    objects = [objects_dir / f"{index:04d}-{source.stem}.o"
               for index, source in enumerate(compile_sources)]
    archive = build_dir / "libttt2_android_ppc.a"

    # A response file keeps the tool command short and lets Ninja notice flag changes.
    flag_file = build_dir / "compile_flags.rsp"
    write_if_changed(flag_file, "\n".join(flags) + "\n")
    ninja = ["rule cxx",
             f"  command = {ninja_path(compiler)} @{ninja_path(flag_file)} -MMD -MT $out -MF $out.d -c $in -o $out",
             "  depfile = $out.d",
             "  deps = gcc",
             "  description = CXX $in",
             "rule ar",
             f"  command = {ninja_path(archiver)} rcs $out $in",
             "  description = AR $out"]
    for source, obj in zip(compile_sources, objects):
        ninja.append(f"build {ninja_path(obj)}: cxx {ninja_path(source)} | "
                     f"{ninja_path(header_dir / 'ppc_arm64_compat.h')} "
                     f"{ninja_path(header_dir / 'ppc_context_android.h')} "
                     f"{ninja_path(source_root / 'port/src/runtime/ppc_store_guard_linux.h')} "
                     f"{ninja_path(flag_file)}")
    ninja.append(f"build {ninja_path(archive)}: ar " + " ".join(map(ninja_path, objects)))
    ninja.append(f"default {ninja_path(archive)}")
    write_if_changed(build_dir / "build.ninja", "\n".join(ninja) + "\n")
    print(f"Compiling {len(compile_sources)} generated PPC units at -{args.opt_level} for Android ARM64", flush=True)
    subprocess.run(["ninja", "-C", str(build_dir), "-f", str(build_dir / "build.ninja"), "-j", str(args.jobs)],
                   check=True)
    print(f"Archive: {archive}")


if __name__ == "__main__":
    main()
