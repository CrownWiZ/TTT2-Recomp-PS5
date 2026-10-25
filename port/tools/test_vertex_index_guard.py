#!/usr/bin/env python3
"""CPU-only regression tests for vertex-fetch index preservation.

Uses the production microcode decoders and translator. No GPU, SDK, or game
assets required. Optional local shader files must translate successfully.
"""
import argparse
import os
from pathlib import Path
import subprocess
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[2]
KERNEL = ROOT / "port/src/runtime/kernel"
TEST = r'''
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstring>
std::vector<uint32_t> Shader(unsigned x, unsigned dst = 0) {
    // Two fetches followed by a position export. The first optionally preserves
    // r0.x; the second must still address the original vertex index.
    return {1u | (3u << 12) | (5u << 16), CF_EXEC_END << 12, 0,
        (dst << 12) | (31u << 20) | (2u << 25),
        (FMT_32_32_32_32_FLOAT << 16) | 0x688u | x, 4,
        (1u << 12) | (31u << 20) | (2u << 25),
        (FMT_32_32_32_32_FLOAT << 16) | 0x688u, 4,
        (S_RETAIN_PREV << 26) | 0xF8000u | 62u, 0,
        (V_MAX << 24) | 0x00010100u | 0xC0000000u};
}
int main(int argc, char** argv) {
    for (unsigned x = 0; x < 8; ++x) {
        const auto t = ttt2_hw::TranslateVertexShader(Shader(x));
        assert(t.unsupported.empty() == (x >= 6));
        // Writing another register's x does not damage r0.x.
        assert(ttt2_hw::TranslateVertexShader(Shader(x, 2)).unsupported.empty());
    }
    const auto six = ttt2_hw::TranslateVertexShader(Shader(6));
    const auto seven = ttt2_hw::TranslateVertexShader(Shader(7));
    assert(six.function == seven.function); // HLSL is shared, validation is per microcode.
    assert(six.fetches[0].dstSwizzle != seven.fetches[0].dstSwizzle);
    // ALU writes to the index remain unsafe, including relative destinations.
    for (bool scalar : {false, true}) for (bool relative : {false, true}) {
        ttt2_hw::detail::Writer w; w.vertex = true;
        Alu alu{};
        alu.w0 = scalar ? (1u << 20) | (relative ? 1u << 14 : 0)
                        : (1u << 16) | (relative ? 1u << 6 : 0);
        w.WriteResult(alu, scalar, "float4(0,0,0,0)");
        w.VFetch_(VFetch{31u << 20, (FMT_32_FLOAT << 16) | 0xFFFu, 1});
        assert(!w.unsupported.empty());
    }
    // With control flow, even an index write after the final fetch can precede
    // another fetch on a different iteration; retain the conservative guard.
    auto branch = Shader(7);
    branch[2] = (CF_COND_JMP << 12) << 16;
    branch[9] = (S_RETAIN_PREV << 26) | (1u << 16); // final ALU writes r0.x
    assert(!ttt2_hw::TranslateVertexShader(branch).unsupported.empty());
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary);
        std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
        assert(!b.empty() && b.size() % 4 == 0);
        std::vector<uint32_t> c(b.size() / 4);
        std::memcpy(c.data(), b.data(), b.size());
        const auto t = ttt2_hw::TranslateVertexShader(c);
        if (!t.unsupported.empty()) {
            std::fprintf(stderr, "%s: %s\n", argv[i], t.unsupported.c_str());
            return 1;
        }
        std::printf("PASS captured shader: %zu fetches\n", t.fetches.size());
    }
    std::puts("PASS vertex index guards: preserve x, overwrite x, ALU, relative, branching");
}
'''


def write_parity_cases(folder):
    """Synthetic microcode for gpu_hw_vs_parity.cpp; contains no game assets."""
    folder.mkdir(parents=True, exist_ok=True)
    formats = [2, 3, 4, 6, 7, 10, 15, 25, 26, 31, 32, 36, 37, 38, 57]
    for fmt in formats:
        for signed in range(2):
            for integer in range(2):
                for exponent, offset in [(-4, -2), (0, 0), (3, 3)]:
                    # EXEC_END: one fetch into r1, export r1 as interpolator 0,
                    # export constant c0 as position. Slot 95, four-word stride.
                    # Stable finite positions keep decoding failures observable.
                    words = [
                        1 | (3 << 12) | (1 << 16), 2 << 12, 0,
                        (1 << 12) | (31 << 20) | (2 << 25),
                        0x688 | (fmt << 16) | (signed << 14) | (integer << 13) |
                        ((exponent & 63) << 24),
                        4 | ((offset & 0xFFFFFF) << 8),
                        (50 << 26) | 0xF8000, 0, (2 << 24) | 0x10100 | 0xC0000000,
                        (50 << 26) | 0xF8000 | 62, 0, 2 << 24,
                    ]
                    name = f"fmt{fmt:02}_signed{signed}_int{integer}_exp{exponent:+}.bin"
                    (folder / name).write_bytes(struct.pack("<12I", *words))
    # Cover all selector values, permutations, constants and preserved
    # components. Initialize r1 from c0 so a wrong write to a preserved
    # component cannot pass merely because the register started at zero.
    swizzles = [value * 0x249 for value in range(8)] + [0x688, 0x053, 0x1CE, 0xFAC]
    for fmt in formats:
        for index, swizzle in enumerate(swizzles):
            exponent, offset = [(-4, -2), (0, 0), (3, 3)][index % 3]
            words = [
                1 | (4 << 12) | (4 << 16), 2 << 12, 0,
                (50 << 26) | (15 << 16) | 1, 0, 2 << 24,
                (1 << 12) | (31 << 20) | (2 << 25),
                swizzle | (fmt << 16) | ((index & 1) << 14) |
                (((index >> 1) & 1) << 13) | ((exponent & 63) << 24),
                4 | ((offset & 0xFFFFFF) << 8),
                (50 << 26) | 0xF8000, 0, (2 << 24) | 0x10100 | 0xC0000000,
                (50 << 26) | 0xF8000 | 62, 0, 2 << 24,
            ]
            name = f"fmt{fmt:02}_swizzle{swizzle:03X}.bin"
            (folder / name).write_bytes(struct.pack("<15I", *words))
    print(f"Wrote 360 synthetic vertex shaders to {folder}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("shaders", nargs="*", type=Path)
    parser.add_argument("--write-parity-cases", type=Path,
                        help="also generate format cases for the GPU/interpreter parity harness")
    args = parser.parse_args()
    if args.write_parity_cases:
        write_parity_cases(args.write_parity_cases)
    source = (KERNEL / "gpu_render.cpp").read_text()
    # Keep the tested decoders identical to those used by the interpreter.
    begin, end = source.index("enum CfOp"), source.index("// Bits per texel")
    prefix = "#include <algorithm>\n#include <cmath>\n#include <cstdint>\n#include <cstdio>\n"
    with tempfile.TemporaryDirectory(prefix="ttt2-index-guard-") as directory:
        cpp = Path(directory) / "guard.cpp"
        exe = Path(directory) / "guard"
        cpp.write_text(prefix + source[begin:end] + '\n#include "gpu_hw_translate.h"\n' + TEST)
        subprocess.run([args.cxx, "-std=c++20", "-O2", "-I" + str(KERNEL),
                        str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe), *map(str, args.shaders)], check=True)


if __name__ == "__main__":
    main()
