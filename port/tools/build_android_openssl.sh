#!/usr/bin/env bash
set -euo pipefail
# OpenSSL 3.5 LTS; override OPENSSL_TAG to select another reviewed 3.5 release.
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to an Android NDK installation}"
OPENSSL_TAG=${OPENSSL_TAG:-openssl-3.5.4}
OPENSSL_PREFIX=${OPENSSL_PREFIX:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/android-openssl}
OPENSSL_SOURCE=${OPENSSL_SOURCE:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/openssl-src-$OPENSSL_TAG}
if [[ ! -f "$OPENSSL_SOURCE/Configure" ]]; then
  git clone --depth 1 --branch "$OPENSSL_TAG" https://github.com/openssl/openssl.git "$OPENSSL_SOURCE"
fi
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}
export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
export PATH="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
cd "$OPENSSL_SOURCE"
./Configure android-arm64 -D__ANDROID_API__=28 no-shared no-tests --prefix=/ --openssldir=/etc/ssl --libdir=lib
make -j"${OPENSSL_JOBS:-8}"
make install_sw DESTDIR="$OPENSSL_PREFIX"
printf 'OpenSSL Android prefix: %s\n' "$OPENSSL_PREFIX"
