#!/usr/bin/env bash
set -euo pipefail
# Private signing material stays outside the repository and the distribution folder.
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
build_tools=${TTT2_ANDROID_BUILD_TOOLS:-$sdk_root/build-tools/35.0.0}
key_dir=${TTT2_ANDROID_SIGNING_DIR:-${XDG_CONFIG_HOME:-$HOME/.config}/ttt2/android-release}
out_dir=${TTT2_ANDROID_DIST:-$repo_root/dist/android}
umask 077
mkdir -p "$key_dir" "$out_dir"
if [[ ! -f $key_dir/release.jks ]]; then
    if [[ -f $key_dir/password ]]; then
        printf 'Signing password exists but key is missing. Restore the key before releasing updates.\n' >&2
        exit 1
    fi
    python3 - "$key_dir/password" <<'PY'
import secrets, sys
from pathlib import Path
Path(sys.argv[1]).write_text(secrets.token_urlsafe(40) + '\n')
PY
    keytool -genkeypair -keystore "$key_dir/release.jks" -storetype JKS \
        -alias ttt2 -keyalg RSA -keysize 3072 -validity 10000 \
        -storepass:file "$key_dir/password" -keypass:file "$key_dir/password" \
        -dname 'CN=TTT2 Android' -noprompt
fi
apk=$out_dir/ttt2-android-arm64-release.apk
"$build_tools/zipalign" -f -P 16 4 "$1" "$out_dir/aligned-unsigned.apk"
"$build_tools/apksigner" sign --ks "$key_dir/release.jks" --ks-key-alias ttt2 \
    --ks-pass "file:$key_dir/password" \
    --out "$apk" "$out_dir/aligned-unsigned.apk"
rm "$out_dir/aligned-unsigned.apk"
"$build_tools/apksigner" verify --verbose "$apk"
"$build_tools/zipalign" -c -P 16 4 "$apk"
chmod 644 "$apk"
(cd "$out_dir" && sha256sum ttt2-android-arm64-release.apk > SHA256SUMS)
printf 'Signed Android release APK: %s\n' "$apk"
