"""Pack the twelve effects for sharing: py make_release.py
Needs the .ZDL files in dist\\ (run py build_all.py first).
Writes release\\Matujuice_ZoomMS_pack.zip with the ZDLs, README.md, LICENSE (from the repo root) and the cover sheet."""
import sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
NAMES = ["WaveFold", "DualShft", "Choral", "EuGate", "DubSiren", "SGNL", "Scrub",
         "DirtBox", "Breather", "SyncEQ", "GridDly", "Sweep"]
missing = [n for n in NAMES if not (ROOT / "dist" / (n + ".ZDL")).exists()]
if missing:
    sys.exit("Missing in dist\\: " + ", ".join(n + ".ZDL" for n in missing) + "\nRun py build_all.py first.")
out = ROOT / "release" / "Matujuice_ZoomMS_pack.zip"
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for n in NAMES:
        z.write(ROOT / "dist" / (n + ".ZDL"), n + ".ZDL")
    for f in (ROOT / "release" / "README.md", ROOT / "LICENSE", ROOT / "release" / "covers.png"):
        if f.exists():
            z.write(f, f.name)
print("wrote", out)
