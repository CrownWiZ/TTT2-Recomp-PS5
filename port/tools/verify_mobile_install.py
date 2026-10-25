#!/usr/bin/env python3
"""Compare Android importer output with the existing extracted reference (no game launch)."""
import argparse
import hashlib
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('installed', type=Path)
p.add_argument('game_source', type=Path)
a = p.parse_args()
entries = [line.split(' ', 1) for line in (a.installed / 'installed.txt').read_text().splitlines()]
assert len(entries) == 48, 'Expected 48 installed files'
for size, relative in entries:
    out = a.installed / relative
    ref = (a.game_source / 're/decrypted/Default_patched_tu3.bin' if relative == 'tekken.img'
           else a.game_source / 're/game_files_x' / Path(relative).relative_to('game_files_x'))
    assert out.stat().st_size == ref.stat().st_size == int(size), relative
    with out.open('rb') as f, ref.open('rb') as g:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
        assert digest == hashlib.file_digest(g, 'sha256').hexdigest(), relative
    print(digest, relative, flush=True)
print('All 48 files match the reference byte for byte.')
