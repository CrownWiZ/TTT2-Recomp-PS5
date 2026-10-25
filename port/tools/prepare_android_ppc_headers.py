#!/usr/bin/env python3
"""Prepare ARM64-only overlays for generated PPC headers outside the source tree."""

from pathlib import Path
import argparse


def write_if_changed(path: Path, content: str) -> None:
    if not path.is_file() or path.read_text() != content:
        path.write_text(content)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ppc-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    original = (args.ppc_dir / "ppc_context.h").read_text()
    old_shift = "return simde_mm_or_si128(simde_mm_slli_epi64(a, shift), simde_mm_srli_epi64(simde_mm_slli_si128(a, 8), 64 - shift));"
    new_shift = """const simde__m128i left = simde_mm_cvtsi64_si128(shift);
    const simde__m128i right = simde_mm_cvtsi64_si128(64 - shift);
    return simde_mm_or_si128(simde_mm_sll_epi64(a, left),
                             simde_mm_srl_epi64(simde_mm_slli_si128(a, 8), right));"""
    old_fpcr = '__asm__ __volatile__("msr fpcr, %0" : : "r"(csr));'
    new_fpcr = 'uint64_t nativeCsr = csr;\n        __asm__ __volatile__("msr fpcr, %0" : : "r"(nativeCsr));'
    old_round = 'static constexpr size_t HostToGuest[] = { PPC_ROUND_NEAREST, PPC_ROUND_DOWN, PPC_ROUND_UP, PPC_ROUND_TOWARD_ZERO };'
    new_round = '''#if defined(__aarch64__) || defined(_M_ARM64)
    // ARM FPCR encodes 01 as +Infinity and 10 as -Infinity.
    static constexpr size_t HostToGuest[] = { PPC_ROUND_NEAREST, PPC_ROUND_UP, PPC_ROUND_DOWN, PPC_ROUND_TOWARD_ZERO };
#else
    static constexpr size_t HostToGuest[] = { PPC_ROUND_NEAREST, PPC_ROUND_DOWN, PPC_ROUND_UP, PPC_ROUND_TOWARD_ZERO };
#endif'''
    for label, old in (("variable shift", old_shift), ("ARM64 FPCR", old_fpcr),
                       ("ARM64 rounding modes", old_round)):
        if original.count(old) != 1:
            raise SystemExit(f"expected exactly one {label} in {args.ppc_dir / 'ppc_context.h'}")
    patched = original.replace(old_shift, new_shift).replace(old_fpcr, new_fpcr).replace(old_round, new_round)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_if_changed(args.output_dir / "ppc_context_android.h", patched)
    write_if_changed(args.output_dir / "ppc_arm64_compat.h",
        '#pragma once\n'
        '#if !defined(__ANDROID__) || !defined(__aarch64__)\n'
        '#error "Android PPC overlay requires ARM64"\n'
        '#endif\n'
        '#include "ppc_config.h"\n'
        '#include "ppc_context_android.h"\n'
        '#ifndef _mm_pause\n'
        '#define _mm_pause() __asm__ __volatile__("yield")\n'
        '#endif\n'
    )


if __name__ == "__main__":
    main()
