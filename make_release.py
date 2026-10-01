"""Pack the five effects for sharing: py make_release.py
Needs the .ZDL files in dist\\ (run the build_*.bat files first).
Writes release\\Matujuice_ZoomMS_pack.zip with the ZDLs, README.md, LICENSE and the cover sheet."""
import sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
NAMES = ["WaveFold", "DualShft", "Choral", "EuGate", "DubSiren"]
missing = [n for n in NAMES if not (ROOT / "dist" / (n + ".ZDL")).exists()]
if missing:
    sys.exit("Missing in dist\\: " + ", ".join(n + ".ZDL" for n in missing) + "\nRun the matching build_*.bat first.")
out = ROOT / "release" / "Matujuice_ZoomMS_pack.zip"
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for n in NAMES:
        z.write(ROOT / "dist" / (n + ".ZDL"), n + ".ZDL")
    for f in ("README.md", "LICENSE", "covers.png"):
        if (ROOT / "release" / f).exists():
            z.write(ROOT / "release" / f, f)
print("wrote", out)
