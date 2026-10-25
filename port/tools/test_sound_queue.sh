#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_source=${TTT2_GAME_SOURCE:-$repo_root}
ppc_dir=$game_source/re/ppc
xenon=$game_source/tools/XenonRecomp
build_dir=${TTT2_SOUND_QUEUE_TEST_BUILD:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/sound-queue-test}
mkdir -p "$build_dir"

python3 - "$ppc_dir" "$build_dir/reference.cpp" <<'PY'
from pathlib import Path
import re
import sys
root = Path(sys.argv[1])
result = ['#include "ppc_recomp_shared.h"', 'void SoundQueueTestAfterCountRead();']
for unit, functions in {
    240: ['82B1D970', '82B1D228', '82B1D298'],
    290: ['82CFF140', '82CFF228', '82CFF2D8'],
}.items():
    source = (root / f'ppc_recomp.{unit}.cpp').read_text()
    for address in functions:
        name = f'__imp__sub_{address}'
        match = re.search(r'PPC_FUNC_IMPL\(' + name + r'\) \{.*?^\}', source, re.M | re.S)
        if not match:
            sys.exit(f'Missing generated reference: {name}')
        function = match[0]
        if address != '82B1D970':
            function = function.replace(name, f'sub_{address}', 1)
            function = function.replace('PPC_FUNC_IMPL(', 'PPC_FUNC(', 1)
        if address == '82B1D298':
            point = 'ctx.r11.u64 = PPC_LOAD_U32(ctx.r8.u32 + -12);'
            if function.count(point) != 1:
                sys.exit('Missing consumer scheduling point')
            function = function.replace(point, point + '\n\tSoundQueueTestAfterCountRead();')
        result.append(function)
Path(sys.argv[2]).write_text('\n\n'.join(result))
PY

"${CXX:-clang++}" -std=c++20 -O2 -march=x86-64-v2 -ffp-contract=off -pthread \
    -I "$repo_root/port/src/runtime" -I "$ppc_dir" \
    -I "$xenon/XenonUtils" -I "$xenon/thirdparty/simde" \
    "$repo_root/port/tools/test_sound_queue.cpp" \
    "$repo_root/port/src/runtime/kernel/sound_queue.cpp" "$build_dir/reference.cpp" \
    -o "$build_dir/test_sound_queue"
"$build_dir/test_sound_queue"
