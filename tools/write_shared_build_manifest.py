#!/usr/bin/env python3
"""Record shared code and DXVK provenance after a successful platform build."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--dxvk-source", type=Path, required=True)
parser.add_argument("--platform", choices=["linux", "android"], required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
runtime = repo / "port/src/runtime"
files = {}
for path in sorted(runtime.rglob("*")):
    if path.is_file() and path.suffix in {".h", ".cpp"}:
        files[str(path.relative_to(repo))] = hashlib.sha256(path.read_bytes()).hexdigest()
shared = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
source = args.dxvk_source.resolve()
stamp = source.parent / (source.name + ".json")
dxvk = json.loads(stamp.read_text())
actual = hashlib.sha256(subprocess.check_output(
    ["git", "-C", str(source), "diff", "HEAD", "--binary"])).hexdigest()
if actual != dxvk["source_diff_sha256"]:
    parser.error("DXVK changed after preparation")
cache = (args.build / "CMakeCache.txt").read_text()
home = next(line.split("=", 1)[1] for line in cache.splitlines()
            if line.startswith("CMAKE_HOME_DIRECTORY:"))
expected = repo / ("port" if args.platform == "linux" else "port/android/game")
if Path(home).resolve() != expected:
    parser.error("Build belongs to another worktree; rebuild from this shared source")
output = args.build / "shared-source-manifest.json"
output.write_text(json.dumps({"platform": args.platform, "shared_source_sha256": shared,
                              "dxvk": dxvk, "files": files}, indent=2) + "\n")
print(f"{args.platform}: shared source {shared}; DXVK {dxvk['manifest_sha256']}")
