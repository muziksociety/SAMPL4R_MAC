"""
Slide renderer for SM5 tutorial videos.

Generates one 1920x1080 PNG per step in a lesson, written next to the WAVs
produced by tts_windows.py.

    python tools/tutorial_gen/slide_render.py --lesson gs_welcome --lang en
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parents[2]
LESSONS_JSON = REPO_ROOT / "resources" / "mentor" / "mentor_lessons.json"
OUT_DIR = REPO_ROOT / "tools" / "tutorial_gen" / "out"

W, H = 1920, 1080
BG = (26, 26, 26)
BG_BOTTOM = (12, 12, 16)
CYAN = (0, 255, 255)
WHITE = (240, 240, 240)
GREY = (140, 140, 140)
DIM = (90, 90, 95)


def load_lesson(lesson_id: str) -> tuple[dict, dict]:
    data = json.loads(LESSONS_JSON.read_text(encoding="utf-8"))
    for sec in data.get("sections", []):
        for les in sec.get("lessons", []):
            if les["id"] == lesson_id:
                return sec, les
    raise SystemExit(f"Lesson '{lesson_id}' not found")


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = (
        ["C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"]
        if bold else
        ["C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"]
    )
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def make_background() -> Image.Image:
    img = Image.new("RGB", (W, H), BG)
    px = img.load()
    for y in range(H):
        t = y / (H - 1)
        r = int(BG[0] * (1 - t) + BG_BOTTOM[0] * t)
        g = int(BG[1] * (1 - t) + BG_BOTTOM[1] * t)
        b = int(BG[2] * (1 - t) + BG_BOTTOM[2] * t)
        for x in range(W):
            px[x, y] = (r, g, b)
    return img


def wrap_text(text: str, font: ImageFont.FreeTypeFont, max_w: int,
              draw: ImageDraw.ImageDraw) -> list[str]:
    out: list[str] = []
    for paragraph in text.split("\n\n"):
        paragraph = paragraph.replace("\n", " ").strip()
        if not paragraph:
            out.append("")
            continue
        words = paragraph.split()
        line = ""
        for w in words:
            cand = (line + " " + w).strip()
            if draw.textlength(cand, font=font) <= max_w:
                line = cand
            else:
                if line:
                    out.append(line)
                line = w
        if line:
            out.append(line)
        out.append("")  # paragraph break
    while out and out[-1] == "":
        out.pop()
    return out


def render_step(bg: Image.Image, lesson_title: str, step_idx: int,
                total_steps: int, title: str, body: str) -> Image.Image:
    img = bg.copy()
    d = ImageDraw.Draw(img)

    f_brand = load_font(28, bold=True)
    f_lesson = load_font(22)
    f_step = load_font(24, bold=True)
    f_title = load_font(64, bold=True)
    f_body = load_font(30)
    f_footer = load_font(20)

    margin = 80
    d.text((margin, 50), "SAMPLE MONSTA", font=f_brand, fill=CYAN)
    lesson_w = d.textlength(lesson_title, font=f_lesson)
    d.text((W - margin - lesson_w, 56), lesson_title, font=f_lesson, fill=GREY)

    step_label = f"STEP {step_idx + 1} OF {total_steps}"
    d.text((margin, 200), step_label, font=f_step, fill=CYAN)

    d.text((margin, 250), title, font=f_title, fill=WHITE)

    body_max_w = W - 2 * margin
    lines = wrap_text(body, f_body, body_max_w, d)
    y = 360
    line_h = 44
    for line in lines:
        if y > H - 140:
            break
        d.text((margin, y), line, font=f_body, fill=WHITE)
        y += line_h if line else int(line_h * 0.6)

    d.text((margin, H - 50), "samplemonsta.com", font=f_footer, fill=DIM)
    d.line([(margin, 180), (W - margin, 180)], fill=DIM, width=1)
    return img


def render_lesson(lesson_id: str, lang: str) -> Path:
    sec, lesson = load_lesson(lesson_id)
    out_root = OUT_DIR / lesson_id / lang
    out_root.mkdir(parents=True, exist_ok=True)

    bg = make_background()
    steps = lesson["steps"]
    lesson_title = lesson.get(
        "title_es" if lang == "es" else "title", lesson_id)

    for idx, step in enumerate(steps):
        if lang == "es":
            title = step.get("title_es") or step.get("title", "")
            body = step.get("body_es") or step.get("body", "")
        else:
            title = step.get("title", "")
            body = step.get("body", "")
        slide = render_step(bg, lesson_title, idx, len(steps), title, body)
        out = out_root / f"step_{idx:02d}.png"
        slide.save(out, "PNG", optimize=True)
        print(f"  slide {idx:02d}: {out.name}")
    return out_root


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--lesson", default="gs_welcome")
    p.add_argument("--lang", default="en", choices=("en", "es"))
    args = p.parse_args()
    out_root = render_lesson(args.lesson, args.lang)
    print(f"Done. Slides in {out_root}")


if __name__ == "__main__":
    main()
