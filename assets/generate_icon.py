"""
Generate SAMPLAR icons from the master logo.png at the repo root.

Source: ../logo.png  (the real product logo)
Outputs (next to this script):
  - samplar_icon_512.png     -> JUCE ICON_BIG
  - samplar_icon_64.png      -> JUCE ICON_SMALL
  - samplar_icon.ico         -> Windows multi-res app icon

The source has a tight crop of the S glyph on a near-white background.
We:
  1) Auto-crop transparent / near-white margins to the visible glyph.
  2) Pad to a square so nothing is squished.
  3) Resize cleanly with Lanczos for each target.
  4) Export ICO with all standard Windows sizes.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent
SRC = ROOT.parent / "logo.png"


def auto_crop(img: Image.Image, threshold: int = 240) -> Image.Image:
    """Trim borders that are either transparent or near-white."""
    rgba = img.convert("RGBA")
    px = rgba.load()
    w, h = rgba.size

    def is_bg(r, g, b, a):
        if a < 8:
            return True
        return r > threshold and g > threshold and b > threshold

    left, top, right, bottom = w, h, 0, 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if not is_bg(r, g, b, a):
                if x < left: left = x
                if y < top: top = y
                if x > right: right = x
                if y > bottom: bottom = y

    if right < left or bottom < top:
        return rgba
    pad = max(2, int(min(w, h) * 0.02))
    left = max(0, left - pad)
    top = max(0, top - pad)
    right = min(w - 1, right + pad)
    bottom = min(h - 1, bottom + pad)
    return rgba.crop((left, top, right + 1, bottom + 1))


def square_pad(img: Image.Image) -> Image.Image:
    """Center the image on a transparent square canvas."""
    w, h = img.size
    side = max(w, h)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(img, ((side - w) // 2, (side - h) // 2), img)
    return canvas


def zoom_square(img: Image.Image, zoom: float) -> Image.Image:
    """Zoom into a squared image by `zoom` (e.g. 1.35) — scale up then
    center-crop back to the original side. The glyph fills more of the
    final canvas; the bleed off the sides is clipped (which is what we
    want for an icon — the cropped logo already has whitespace padding)."""
    side = min(img.size)
    big_w = int(round(img.width  * zoom))
    big_h = int(round(img.height * zoom))
    big = img.resize((big_w, big_h), Image.LANCZOS)
    left = (big_w - side) // 2
    top  = (big_h - side) // 2
    return big.crop((left, top, left + side, top + side))


def main():
    if not SRC.exists():
        raise SystemExit(f"Master logo not found at {SRC}")

    print(f"Loading {SRC} ...")
    src = Image.open(SRC)

    print("Auto-cropping margins ...")
    cropped = auto_crop(src)
    print(f"  cropped: {cropped.size}")

    print("Squaring canvas ...")
    squared = square_pad(cropped)
    print(f"  squared: {squared.size}")

    # The Windows app icon needs the glyph to dominate the small 16-256 px
    # tiles. Zoom 1.35x in (glyph fills more of the canvas, whitespace edges
    # are cropped). We keep `squared` untouched for the in-app logo PNG.
    print("Zooming icon canvas 1.35x ...")
    squared_icon = zoom_square(squared, 1.35)
    print(f"  icon canvas: {squared_icon.size}")

    print("Rendering 512x512 ...")
    big = squared_icon.resize((512, 512), Image.LANCZOS)
    big.save(ROOT / "samplar_icon_512.png")
    print(f"  -> {ROOT / 'samplar_icon_512.png'}")

    print("Rendering 64x64 ...")
    small = squared_icon.resize((64, 64), Image.LANCZOS)
    small.save(ROOT / "samplar_icon_64.png")
    print(f"  -> {ROOT / 'samplar_icon_64.png'}")

    # In-app logo: keep the original aspect ratio, but downscale to a width that
    # is plenty for any UI use (top bar is ~130 px wide; 800 covers HiDPI).
    print("Rendering in-app logo (aspect-preserving) ...")
    aspect = cropped.width / cropped.height
    target_w = 800
    target_h = int(round(target_w / aspect))
    logo_wide = cropped.resize((target_w, target_h), Image.LANCZOS)
    logo_wide.save(ROOT / "samplar_logo.png", optimize=True)
    print(f"  -> {ROOT / 'samplar_logo.png'} ({target_w}x{target_h})")

    print("Building multi-res .ico ...")
    sizes = [256, 128, 64, 48, 32, 16]
    images = [squared_icon.resize((s, s), Image.LANCZOS) for s in sizes]
    images[0].save(
        ROOT / "samplar_icon.ico",
        format="ICO",
        sizes=[(s, s) for s in sizes],
        append_images=images[1:],
    )
    print(f"  -> {ROOT / 'samplar_icon.ico'}")
    print("Done.")


if __name__ == "__main__":
    main()
