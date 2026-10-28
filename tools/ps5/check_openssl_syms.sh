#!/bin/sh
d=/root/.cache/ttt2-platform/openssl-src-ps5
cd "$d" || exit 1
echo "== configuration.h 40-60 =="
sed -n '40,60p' include/openssl/configuration.h
echo "== async.o mtime =="
ls -la crypto/async/libcrypto-lib-async.o crypto/async/arch/libcrypto-lib-async_posix.o 2>/dev/null
echo "== Makefile async rule =="
grep -n 'async/async\.c\|async_posix\.c' Makefile | head -10
