#!/usr/bin/env bash
# Sends the TTT2 PS5 smoke ELF (and optionally the SDK hello_world) to the
# console's ELF loader (Relapse / ps5-payload-elfldr) on port 9021.
#
# Usage:
#   PS5_HOST=192.168.1.50 [PS5_PORT=9021] bash tools/ps5/send_smoke.sh
#
# Expects: PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk, socat installed,
# and the jailbreak's ELF loader listening (ps5-payload-elfldr alive).
set -e

PS5_HOST="${PS5_HOST:?export PS5_HOST=<PS5 IP>}"
PS5_PORT="${PS5_PORT:-9021}"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
ELF="$REPO/tools/ps5/build/ttt2_smoke.elf"

if [ ! -f "$ELF" ]; then
  make -f "$REPO/tools/ps5/Makefile" all
fi

echo "[send-smoke] $ELF -> $PS5_HOST:$PS5_PORT"
"$PS5_PAYLOAD_SDK/bin/prospero-deploy" -h "$PS5_HOST" -p "$PS5_PORT" "$ELF"
echo "[send-smoke] sent; a notification bubble must appear on the PS5."
