#!/bin/sh
# Fix the bit_cast builtin's comma form (the sweep rewrote it with '<T>' syntax).
cd /mnt/c/ttt2/port/src/runtime || exit 1
python3 - <<'EOF'
import re
for path in ("audio_filter_native.h", "audio_output.cpp"):
    try:
        text = open(path).read()
    except FileNotFoundError:
        continue
    fixed = re.sub(r"__builtin_bit_cast<([^>]+)>\(", r"__builtin_bit_cast(\1, ", text)
    if fixed != text:
        open(path, "w").write(fixed)
        print("fixed", path)
EOF
grep -rn '__builtin_bit_cast<' . 2>/dev/null | head -3
echo BITCAST_DONE
