#!/usr/bin/env python3
"""Makes Purrlude's app icon (branding/purrlude-icon.png, 1024 x 1024, and a 256 x 256 copy for
Linux, purrlude-icon-256.png) and the logo the app shows on its splash screen and About window
(branding/purrlude-logo.jpg, 840 x 840) from the logo.

The icon is the logo's cat in its circle, without the wordmark (unreadable at Dock size), on a
rounded square in the logo's own background colour, sized to macOS's icon grid (an 824 px
body with 100 px of margin, so it sits beside other apps' icons).

    python3 tools/branding/make_icon.py

Needs Pillow. CMakeLists.txt passes the icon to juce_add_gui_app (ICON_BIG), which makes the
macOS .icns and the Windows .ico from it, and compiles the logo in (PurrludeBranding). The
AppImage (tools/packaging/make_appimage.sh) uses the 256 px copy: Linux icons come in fixed sizes.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
LOGO = ROOT / "docs" / "user-guide" / "images" / "Purrlude-Cat-Logo.png"
ICON = ROOT / "branding" / "purrlude-icon.png"
ICON_256 = ROOT / "branding" / "purrlude-icon-256.png"
SMALL_LOGO = ROOT / "branding" / "purrlude-logo.jpg"
SMALL_LOGO_SIZE = 840  # twice the splash screen's 420 points, for Retina screens

SIZE, BODY, RADIUS = 1024, 824, 185
CAT = (205, 125, 1125, 828)  # the cat, its circle and the note in the 1254 px logo, above the wordmark


def main():
    logo = Image.open(LOGO).convert("RGB")
    background = logo.getpixel((8, 8))

    body = Image.new("RGB", (BODY, BODY), background)
    cat = logo.crop(CAT)
    width = BODY - 70
    cat = cat.resize((width, round(cat.height * width / cat.width)), Image.LANCZOS)
    # Its edges fade into the background, so the crop doesn't show.
    feather = Image.new("L", cat.size, 0)
    ImageDraw.Draw(feather).rectangle((24, 24, cat.width - 25, cat.height - 25), fill=255)
    feather = feather.filter(ImageFilter.GaussianBlur(14))
    body.paste(cat, ((BODY - cat.width) // 2, (BODY - cat.height) // 2), feather)

    mask = Image.new("L", (BODY, BODY), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, BODY - 1, BODY - 1), RADIUS, fill=255)

    icon = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    margin = (SIZE - BODY) // 2
    icon.paste(body, (margin, margin), mask)

    ICON.parent.mkdir(parents=True, exist_ok=True)
    icon.save(ICON, optimize=True)
    print(f"Wrote {ICON.relative_to(ROOT)}")

    icon.resize((256, 256), Image.LANCZOS).save(ICON_256, optimize=True)
    print(f"Wrote {ICON_256.relative_to(ROOT)}")

    # The whole logo, smaller, as a JPEG (it has no transparency): a few hundred KB compiled in
    # instead of 1.5 MB.
    small = logo.resize((SMALL_LOGO_SIZE, SMALL_LOGO_SIZE), Image.LANCZOS)
    small.save(SMALL_LOGO, quality=90, optimize=True)
    print(f"Wrote {SMALL_LOGO.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
