"""Makes Purrlude's Windows installer from a Release build:

    python tools/packaging/make_windows_installer.py build/Purrlude_artefacts/Release out/

It draws the wizard's images and the setup icon from branding/, downloads Microsoft's Visual C++
runtime installer (ONNX Runtime needs it; the installer runs it only when the PC's is older), and
compiles tools/packaging/purrlude.iss with Inno Setup 6. Needs Pillow and Inno Setup 6
(ISCC.exe on the PATH or in its usual folder). The separation model isn't included: the app
downloads it when it's first needed.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)

if not (build / "Purrlude.exe").exists():
    sys.exit(f"No Purrlude.exe in {build}")

version = re.search(r"^project\(Purrlude VERSION ([0-9.]+)", (root / "CMakeLists.txt").read_text(), re.M)[1]

iscc = shutil.which("ISCC") or next((p for p in (Path(os.environ.get(v, "")) / "Inno Setup 6" / "ISCC.exe"
                                                 for v in ("ProgramFiles(x86)", "ProgramFiles")) if p.exists()), None)
if not iscc:
    sys.exit("Inno Setup 6 isn't installed (https://jrsoftware.org/isdl.php)")

with tempfile.TemporaryDirectory() as work:
    work = Path(work)
    logo = Image.open(root / "branding" / "purrlude-logo.jpg").convert("RGB")
    icon = Image.open(root / "branding" / "purrlude-icon.png").convert("RGBA")
    background = logo.getpixel((4, 4))   # the logo's own dark background

    # The wizard's side image at 100%, 150% and 200%: the logo centred on its background.
    for width, height in ((164, 314), (246, 459), (328, 628)):
        image = Image.new("RGB", (width, height), background)
        scaled = logo.resize((width, width), Image.LANCZOS)
        image.paste(scaled, (0, (height - width) // 2))
        image.save(work / f"wizard-{width}.bmp")

    # The small image at the top right of the inner pages, on the page's white.
    for width, height in ((55, 55), (83, 80), (110, 106)):
        image = Image.new("RGB", (width, height), "white")
        size = min(width, height)
        scaled = icon.resize((size, size), Image.LANCZOS)
        image.paste(scaled, ((width - size) // 2, (height - size) // 2), scaled)
        image.save(work / f"small-{width}.bmp")

    icon.save(work / "purrlude.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

    # Microsoft's current Visual C++ 2015-2022 runtime (always the latest, so it's not pinned).
    redist = work / "vc_redist.x64.exe"
    print("Downloading the Visual C++ runtime...")
    urllib.request.urlretrieve("https://aka.ms/vs/17/release/vc_redist.x64.exe", redist)
    redist_version = subprocess.run(
        ["powershell", "-NoProfile", "-Command", f"(Get-Item '{redist}').VersionInfo.ProductVersion"],
        capture_output=True, text=True, check=True).stdout.strip()
    major, minor, build_number = (int(n) for n in redist_version.split(".")[:3])
    print(f"Visual C++ runtime {redist_version}")

    subprocess.run([iscc, "/Q",
                    f"/DAppVersion={version}", f"/DSourceDir={build}", f"/DWorkDir={work}", f"/DOutputDir={out}",
                    f"/DRedistMajor={major}", f"/DRedistMinor={minor}", f"/DRedistBuild={build_number}",
                    str(root / "tools" / "packaging" / "purrlude.iss")], check=True)

setup = out / f"Purrlude-{version}-Windows-x64-Setup.exe"
print(f"Made {setup} ({setup.stat().st_size / 1e6:.0f} MB)")
