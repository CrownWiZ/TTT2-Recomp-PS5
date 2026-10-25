#!/usr/bin/env python3
"""Prepare the same pinned, patched DXVK source for Linux and Android."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parent.parent
patch_dir = repo / "tools/patches"
manifest_path = patch_dir / "dxvk-native.json"
manifest = json.loads(manifest_path.read_text())
signature = hashlib.sha256(manifest_path.read_bytes())
for name in manifest["patches"]:
    signature.update((patch_dir / name).read_bytes())
signature = signature.hexdigest()
cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "ttt2-platform/shared-dxvk"
source = cache / ("source-" + signature[:16])
stamp = cache / (source.name + ".json")

def git(*args, capture=False):
    return subprocess.check_output(["git", "-C", str(source), *args]) if capture else subprocess.run(
        ["git", "-C", str(source), *args], check=True, stdout=sys.stderr)

def source_digest():
    return hashlib.sha256(git("diff", "HEAD", "--binary", capture=True)).hexdigest()

cache.mkdir(parents=True, exist_ok=True)
if stamp.exists():
    saved = json.loads(stamp.read_text())
    if (saved["manifest_sha256"] != signature or
            git("rev-parse", "HEAD", capture=True).decode().strip() != manifest["commit"] or
            saved["source_diff_sha256"] != source_digest() or
            git("ls-files", "--others", "--exclude-standard", capture=True).strip()):
        sys.exit("Prepared DXVK source was modified; preserve/review those changes before rebuilding: " + str(source))
else:
    if source.exists():
        sys.exit("Incomplete DXVK preparation; inspect it before retrying: " + str(source))
    mirror = os.environ.get("TTT2_DXVK_MIRROR")
    if mirror:
        subprocess.run(["git", "clone", "--shared", mirror, str(source)], check=True, stdout=sys.stderr)
    else:
        subprocess.run(["git", "clone", "--depth", "1", "--branch", manifest["tag"],
                        manifest["repository"], str(source)], check=True, stdout=sys.stderr)
    git("checkout", "--detach", manifest["commit"])
    for name in manifest["patches"]:
        git("apply", "--index", "--ignore-whitespace", str(patch_dir / name))
    stamp.write_text(json.dumps({"commit": manifest["commit"], "manifest_sha256": signature,
                                "source_diff_sha256": source_digest()}, indent=2) + "\n")
print(source)
