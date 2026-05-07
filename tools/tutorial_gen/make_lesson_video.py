"""
One-shot orchestrator: TTS -> slides -> MP4.

    python tools/tutorial_gen/make_lesson_video.py --lesson gs_welcome --lang en
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run(script: str, lesson: str, lang: str, extra: list[str] | None = None) -> None:
    cmd = [sys.executable, str(HERE / script),
           "--lesson", lesson, "--lang", lang] + (extra or [])
    print(f"\n=== {script} ===")
    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        raise SystemExit(f"{script} failed (exit {proc.returncode})")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--lesson", default="gs_welcome")
    p.add_argument("--lang", default="en", choices=("en", "es"))
    p.add_argument("--voice", default=None)
    args = p.parse_args()

    tts_extra = ["--voice", args.voice] if args.voice else []
    run("tts_windows.py", args.lesson, args.lang, tts_extra)
    run("slide_render.py", args.lesson, args.lang)
    run("build_video.py", args.lesson, args.lang)

    final = (HERE / "out" / args.lesson / args.lang /
             f"{args.lesson}_{args.lang}.mp4")
    print(f"\nFinal MP4: {final}")
    if final.exists():
        print(f"Size: {final.stat().st_size / 1024 / 1024:.2f} MB")


if __name__ == "__main__":
    main()
