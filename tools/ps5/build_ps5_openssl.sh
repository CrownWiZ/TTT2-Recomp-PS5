#!/usr/bin/env bash
# OpenSSL 3.5 LTS cross build for the PS5 port (community TLS, Task 5).
# Same reviewed release the Android build uses; the payload SDK's wrappers
# drive the target. Configure uses the BSD-family target (the SIE toolchain
# presents __FreeBSD__; the C code is plain POSIX).
set -euo pipefail

ROOT=/mnt/c/ttt2
sdk=/root/m4/native/ps5-payload-sdk-mihawk
cache=/root/.cache/ttt2-platform
openssl_prefix=$cache/ps5-openssl
openssl_source=$cache/openssl-src-ps5
OPENSSL_TAG=${OPENSSL_TAG:-openssl-3.5.4}

export PATH="/usr/lib/llvm-18/bin:/usr/bin:/bin"
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}

if [[ ! -f "$openssl_source/Configure" ]]; then
    git clone --depth 1 --branch "$OPENSSL_TAG" https://github.com/openssl/openssl.git "$openssl_source"
fi
cd "$openssl_source"

CC="$sdk/bin/prospero-clang" \
CXX="$sdk/bin/prospero-clang++" \
AR="$sdk/bin/prospero-ar" RANLIB="$sdk/bin/llvm-ranlib" \
./Configure linux-x86_64 no-asm no-shared no-tests no-engine \
    --cross-compile-prefix=""
# Configure wrote probe flags; replace them with the SDK wrapper lines.
sed -i \
    -e "s|^CNF_CFLAGS=.*|CNF_CFLAGS=-I$cache/dxvk-stubs/include -O2 -Wno-everything|" \
    -e "s|CC=.*|CC=$sdk/bin/prospero-clang|" \
    Makefile 2>/dev/null || true
make -j"${OPENSSL_JOBS:-8}" -k || true
make -j"${OPENSSL_JOBS:-8}" libssl.a libcrypto.a > /dev/null 2>&1 || {
    printf 'OpenSSL (targets) re-run failed; quote the log.\n' >&2
    exit 1
}
mkdir -p "$openssl_prefix/lib" "$openssl_prefix/include"
cp libssl.a libcrypto.a "$openssl_prefix/lib/"
cp -r include/* "$openssl_prefix/include/"
printf 'OpenSSL PS5 prefix: %s\n' "$openssl_prefix"
