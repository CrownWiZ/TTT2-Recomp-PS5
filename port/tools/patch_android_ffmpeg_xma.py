#!/usr/bin/env python3
"""Apply the game's single-stream XMA fixes to an isolated FFmpeg source copy."""

from pathlib import Path
import argparse


REPLACEMENTS = (
    (
        "        if (!eof && avpkt->size)\n            nb_samples -= FFMIN(nb_samples, 4096);",
        "        if (!eof && avpkt->size && s->num_streams > 1)\n"
        "            nb_samples -= FFMIN(nb_samples, 4096);",
    ),
    (
        "        ret = decode_init(&s->xma[i], avctx, i);\n"
        "        if (ret < 0)\n            return ret;\n",
        "        ret = decode_init(&s->xma[i], avctx, i);\n"
        "        if (ret < 0)\n            return ret;\n"
        "        if (s->num_streams == 1)\n            s->xma[i].skip_frame = 0;\n",
    ),
    (
        "    for (i = 0; i < s->num_streams; i++)\n        flush(&s->xma[i]);\n",
        "    for (i = 0; i < s->num_streams; i++) {\n"
        "        flush(&s->xma[i]);\n"
        "        if (s->num_streams == 1)\n            s->xma[i].skip_frame = 0;\n"
        "    }\n",
    ),
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_dir", type=Path)
    args = parser.parse_args()
    path = args.source_dir / "libavcodec/wmaprodec.c"
    content = path.read_text()
    for original, replacement in REPLACEMENTS:
        if content.count(original) == 1:
            content = content.replace(original, replacement)
        elif content.count(replacement) != 1:
            raise SystemExit(f"unexpected FFmpeg XMA source layout: {path}")
    if path.read_text() != content:
        path.write_text(content)


if __name__ == "__main__":
    main()
