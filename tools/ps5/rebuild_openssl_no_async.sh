#!/bin/sh
# Force-recompile OpenSSL's async fibre objects with the current (no-async)
# configuration and refresh libcrypto.a. The archive shipped an async.o built
# before OPENSSL_NO_ASYNC was in configuration.h, and that object is what pulls
# in _setjmp/_longjmp, which the PS5 kernel libc does not export.
set -e
d=/root/.cache/ttt2-platform/openssl-src-ps5
cd "$d"
sdk=/root/m4/native/ps5-payload-sdk-mihawk
export PATH=/usr/lib/llvm-18/bin:/usr/bin:/bin

rm -f crypto/async/libcrypto-lib-async.o \
      crypto/async/arch/libcrypto-lib-async_posix.o \
      crypto/async/libcrypto-lib-async.d \
      crypto/async/arch/libcrypto-lib-async_posix.d

make -j8 libcrypto.a libssl.a > /root/openssl_rebuild.log 2>&1 || { tail -20 /root/openssl_rebuild.log; exit 1; }

echo "== undefined after rebuild =="
/usr/lib/llvm-18/bin/llvm-nm crypto/async/libcrypto-lib-async.o 2>/dev/null | grep -E ' U (_setjmp|_longjmp|async_fibre)' || echo "clean: no setjmp/fibre refs"

cp libssl.a libcrypto.a /root/.cache/ttt2-platform/ps5-openssl/lib/
echo OPENSSL_NO_ASYNC_OK
