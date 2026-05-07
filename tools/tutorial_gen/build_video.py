"""
FFmpeg muxer: combines per-step PNGs + WAVs from tutorial_gen/out/<lesson>/<lang>
into a single MP4 with hard cuts between segments.

    python tools/tutorial_gen/build_video.py --lesson gs_welcome --lang en
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = REPO_ROOT / "tools" / "tutorial_gen" / "out"


def find_tool(name: str) -> str:
    found = shutil.which(name) or shutil.which(name + ".exe")
    if found:
        return found
    for cand in (Path("C:/ffmpeg/bin") / f"{name}.exe",):
        if cand.exists():
            return str(cand)
    raise SystemExit(f"{name} not found on PATH")


FFMPEG = find_tool("ffmpeg")
FFPROBE = find_tool("ffprobe")


def probe_duration(wav: Path) -> float:
    res = subprocess.run(
        [FFPROBE, "-v", "error", "-show_entries", "format=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", str(wav)],
        capture_output=True, text=True, check=True,
    )
    return float(res.stdout.strip())


def build_segment(png: Path, wav: Path, out_mp4: Path) -> None:
    cmd = [
        FFMPEG, "-y", "-loop", "1", "-i", str(png), "-i", str(wav),
        "-c:v", "libx264", "-tune", "stillimage", "-pix_fmt", "yuv420p",
        "-r", "30", "-c:a", "aac", "-b:a", "192k",
        "-shortest", "-movflags", "+faststart",
        str(out_mp4),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(
            f"ffmpeg segment failed for {out_mp4.name}:\n{proc.stderr[-2000:]}"
        )


def concat_segments(segments: list[Path], out_mp4: Path, work_dir: Path) -> None:
    list_txt = work_dir / "concat_list.txt"
    list_txt.write_text(
        "\n".join(f"file '{seg.as_posix()}'" for seg in segments) + "\n",
        encoding="utf-8",
    )
    cmd = [
        FFMPEG, "-y", "-f", "concat", "-safe", "0", "-i", str(list_txt),
        "-c", "copy", str(out_mp4),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"ffmpeg concat failed:\n{proc.stderr[-2000:]}")


def build_video(lesson_id: str, lang: str) -> Path:
    work = OUT_DIR / lesson_id / lang
    manifest = json.loads((work / "manifest.json").read_text(encoding="utf-8"))
    segments: list[Path] = []
    total = 0.0
    for entry in manifest["steps"]:
        idx = entry["index"]
        wav = work / entry["wav"]
        png = work / f"step_{idx:02d}.png"
        if not png.exists():
            raise SystemExit(f"Missing slide: {png} (run slide_render first)")
        seg = work / f"segment_{idx:02d}.mp4"
        dur = probe_duration(wav)
        total += dur
        print(f"  segment {idx:02d}: {dur:.2f}s")
        build_segment(png, wav, seg)
        segments.append(seg)

    final = work / f"{lesson_id}_{lang}.mp4"
    concat_segments(segments, final, work)
    print(f"Final: {final} (~{total:.1f}s)")
    return final


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--lesson", default="gs_welcome")
    p.add_argument("--lang", default="en", choices=("en", "es"))
    args = p.parse_args()
    out = build_video(args.lesson, args.lang)
    print(f"Done. MP4: {out}")


if __name__ == "__main__":
    main()
