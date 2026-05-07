"""
Render the entire Mentor lesson library to a polished PDF.

Reads `resources/mentor/mentor_lessons.json`, builds a styled HTML book
(English and Spanish columns side by side), and uses headless Microsoft Edge
to print it to a PDF saved on the user's Desktop.

Usage:
    python tools/tutorial_gen/mentor_to_pdf.py
    python tools/tutorial_gen/mentor_to_pdf.py --lang en  # English only
    python tools/tutorial_gen/mentor_to_pdf.py --out "C:/some/path.pdf"
"""
from __future__ import annotations

import argparse
import html
import json
import os
import shutil
import subprocess
import sys
import tempfile
from datetime import date
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
LESSONS_JSON = REPO_ROOT / "resources" / "mentor" / "mentor_lessons.json"


def _para(text: str) -> str:
    """Convert a body-text block (with \\n\\n paragraph breaks) into HTML."""
    blocks = [p.strip() for p in text.split("\n\n") if p.strip()]
    return "\n".join(f"<p>{html.escape(b).replace(chr(10), '<br>')}</p>" for b in blocks)


def _props(props: list[str] | None) -> str:
    if not props:
        return ""
    chips = []
    for raw in props:
        if ":" in raw:
            kind, _, label = raw.partition(":")
            chips.append(f'<span class="prop prop-{kind.lower()}">{html.escape(label)}</span>')
        else:
            chips.append(f'<span class="prop">{html.escape(raw)}</span>')
    return f'<div class="props">{" ".join(chips)}</div>'


def build_html(data: dict, lang_mode: str) -> str:
    """lang_mode: 'both' | 'en' | 'es'."""
    show_en = lang_mode in ("both", "en")
    show_es = lang_mode in ("both", "es")

    def t(en: str, es: str) -> str:
        if show_en and show_es:
            return f"<div class='lang-en'>{en}</div><div class='lang-es'>{es}</div>"
        if show_en:
            return f"<div class='lang-en'>{en}</div>"
        return f"<div class='lang-es'>{es}</div>"

    sections_html = []
    total_lessons = 0
    total_steps = 0
    for sec in data.get("sections", []):
        title_en = html.escape(sec.get("title", sec["id"]))
        title_es = html.escape(sec.get("title_es", title_en))
        desc_en = html.escape(sec.get("description", ""))
        desc_es = html.escape(sec.get("description_es", desc_en))
        lessons_html = []
        for lesson in sec.get("lessons", []):
            total_lessons += 1
            lt_en = html.escape(lesson.get("title", lesson["id"]))
            lt_es = html.escape(lesson.get("title_es", lt_en))
            steps_html = []
            for i, step in enumerate(lesson.get("steps", []), start=1):
                total_steps += 1
                st_en = html.escape(step.get("title", ""))
                st_es = html.escape(step.get("title_es", st_en))
                body_en = _para(step.get("body", ""))
                body_es = _para(step.get("body_es", step.get("body", "")))
                props = _props(step.get("props"))

                steps_html.append(f"""
                <div class="step">
                  <div class="step-no">Step {i}</div>
                  {t(f"<h3>{st_en}</h3>", f"<h3 class='es'>{st_es}</h3>")}
                  {props}
                  {t(f"<div class='body'>{body_en}</div>", f"<div class='body es'>{body_es}</div>")}
                </div>
                """)

            lessons_html.append(f"""
            <div class="lesson">
              {t(f"<h2>{lt_en}</h2>", f"<h2 class='es'>{lt_es}</h2>")}
              {''.join(steps_html)}
            </div>
            """)

        sections_html.append(f"""
        <section class="section">
          {t(f"<h1>{title_en}</h1>", f"<h1 class='es'>{title_es}</h1>")}
          {t(f"<p class='desc'>{desc_en}</p>", f"<p class='desc es'>{desc_es}</p>")}
          {''.join(lessons_html)}
        </section>
        """)

    today = date.today().isoformat()
    summary = (
        f"{len(data.get('sections', []))} sections &middot; "
        f"{total_lessons} lessons &middot; {total_steps} steps"
    )
    css = """
    @page { size: Letter; margin: 18mm 16mm 18mm 16mm; }
    * { box-sizing: border-box; }
    body {
        font-family: 'Segoe UI', Arial, sans-serif;
        color: #1a1a1a; font-size: 11pt; line-height: 1.45;
    }
    .cover {
        page-break-after: always; padding: 60mm 0 0 0; text-align: center;
    }
    .cover h1 { font-size: 36pt; margin: 0 0 6pt 0; color: #111; }
    .cover .subtitle { font-size: 13pt; color: #666; margin: 0 0 24pt 0; }
    .cover .summary { font-size: 11pt; color: #888; }
    .cover .meta { margin-top: 40mm; font-size: 9pt; color: #999; }
    .toc { page-break-after: always; }
    .toc h1 { font-size: 18pt; margin: 0 0 12pt 0; }
    .toc ul { list-style: none; padding-left: 0; }
    .toc li { padding: 3pt 0; border-bottom: 0.5px dotted #ddd; }
    .toc li.section { font-weight: 600; padding-top: 10pt; }
    .section { page-break-before: always; }
    .section h1 {
        font-size: 22pt; color: #0a3a8a; border-bottom: 2px solid #3a86ff;
        padding-bottom: 4pt; margin-bottom: 6pt;
    }
    .section h1.es { color: #555; font-size: 13pt; border: none; padding: 0; margin-top: 0; }
    .desc { color: #555; font-style: italic; margin: 0 0 18pt 0; }
    .desc.es { color: #888; font-size: 10pt; }
    .lesson { margin: 16pt 0 12pt 0; page-break-inside: avoid; }
    .lesson h2 { font-size: 14pt; color: #1a1a1a; margin: 0 0 6pt 0; }
    .lesson h2.es { font-size: 11pt; color: #777; margin-top: 0; }
    .step {
        margin: 8pt 0 10pt 0; padding: 8pt 12pt;
        background: #fafbfc; border-left: 3px solid #3a86ff; border-radius: 3px;
        page-break-inside: avoid;
    }
    .step-no {
        font-size: 8.5pt; color: #888; font-weight: 600;
        text-transform: uppercase; letter-spacing: 0.6pt; margin-bottom: 2pt;
    }
    .step h3 { font-size: 11.5pt; margin: 0 0 4pt 0; color: #111; }
    .step h3.es { font-size: 10pt; color: #666; margin-top: 0; }
    .props { margin: 4pt 0 6pt 0; }
    .prop {
        display: inline-block; padding: 2pt 7pt; margin: 2pt 4pt 2pt 0;
        background: #eef2ff; color: #1a3a7a; border: 0.5px solid #c0cfee;
        border-radius: 3pt; font-size: 9pt; font-weight: 500;
    }
    .prop-stem { background: #fff5e0; color: #7a4a00; border-color: #f0d090; }
    .prop-icon { background: #e0f5ee; color: #0a5a3a; border-color: #90d8b8; }
    .prop-card { background: #f0e8ff; color: #4a1a8a; border-color: #c8b0ee; }
    .body { margin: 0; }
    .body p { margin: 0 0 4pt 0; }
    .body.es { color: #777; font-size: 10pt; border-top: 0.5px dashed #ddd; padding-top: 4pt; margin-top: 4pt; }
    .lang-en + .lang-es { margin-top: 6pt; padding-top: 4pt; border-top: 0.5px dashed #ddd; }
    """

    # Table of contents
    toc_items = []
    for sec in data.get("sections", []):
        toc_items.append(f"<li class='section'>{html.escape(sec.get('title', sec['id']))}</li>")
        for lesson in sec.get("lessons", []):
            toc_items.append(f"<li>&nbsp;&nbsp;{html.escape(lesson.get('title', lesson['id']))}</li>")

    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Sample Monsta Mentor Guide</title>
<style>{css}</style>
</head>
<body>
  <div class="cover">
    <h1>Sample Monsta Mentor</h1>
    <div class="subtitle">The complete in-app tutorial library, exported.</div>
    <div class="summary">{summary}</div>
    <div class="meta">Generated {today} &middot; English &amp; Espa&ntilde;ol</div>
  </div>
  <div class="toc">
    <h1>Contents</h1>
    <ul>{''.join(toc_items)}</ul>
  </div>
  {''.join(sections_html)}
</body>
</html>"""


def find_edge() -> str | None:
    candidates = [
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    ]
    for c in candidates:
        if Path(c).exists():
            return c
    found = shutil.which("msedge.exe") or shutil.which("msedge")
    return found


def find_chrome() -> str | None:
    candidates = [
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    ]
    for c in candidates:
        if Path(c).exists():
            return c
    return shutil.which("chrome.exe") or shutil.which("chrome")


def html_to_pdf(html_path: Path, pdf_path: Path) -> None:
    browser = find_edge() or find_chrome()
    if not browser:
        raise SystemExit("Neither Microsoft Edge nor Google Chrome found. Install one.")
    pdf_path.parent.mkdir(parents=True, exist_ok=True)
    file_url = "file:///" + str(html_path).replace("\\", "/")
    cmd = [
        browser,
        "--headless=new",
        "--disable-gpu",
        f"--print-to-pdf={pdf_path}",
        "--no-pdf-header-footer",
        file_url,
    ]
    print(f"  $ {Path(browser).name} --headless=new --print-to-pdf=...")
    res = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if not pdf_path.exists():
        # Older Chrome/Edge use --headless without =new
        cmd[1] = "--headless"
        res = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if not pdf_path.exists():
        raise SystemExit(
            f"Headless print-to-pdf produced no file.\nstdout:\n{res.stdout}\nstderr:\n{res.stderr}"
        )


def desktop_path() -> Path:
    home = Path(os.path.expanduser("~"))
    candidates = [home / "Desktop", home / "OneDrive" / "Desktop"]
    for c in candidates:
        if c.exists():
            return c
    return home / "Desktop"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--lang", default="both", choices=("both", "en", "es"))
    ap.add_argument("--out", default=None, help="Override output PDF path.")
    args = ap.parse_args()

    if not LESSONS_JSON.exists():
        sys.exit(f"Lessons JSON not found: {LESSONS_JSON}")

    data = json.loads(LESSONS_JSON.read_text(encoding="utf-8"))
    print(f"Loaded {len(data.get('sections', []))} sections from {LESSONS_JSON.name}")

    out_pdf = (
        Path(args.out)
        if args.out
        else desktop_path() / f"SampleMonsta_Mentor_Guide_{args.lang}.pdf"
    )
    print(f"Output: {out_pdf}")

    html_doc = build_html(data, args.lang)
    with tempfile.NamedTemporaryFile(
        "w", suffix=".html", delete=False, encoding="utf-8"
    ) as f:
        f.write(html_doc)
        tmp_html = Path(f.name)

    try:
        html_to_pdf(tmp_html, out_pdf)
    finally:
        try:
            tmp_html.unlink()
        except OSError:
            pass

    size_kb = out_pdf.stat().st_size / 1024
    print(f"Done. PDF: {out_pdf}  ({size_kb:.1f} KB)")


if __name__ == "__main__":
    main()
