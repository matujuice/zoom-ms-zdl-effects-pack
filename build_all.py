#!/usr/bin/env python3
"""Build the five effects into ./dist/.

    py build_all.py              # all five
    py build_all.py eugate       # one (wavefold, dualshft, formant, eugate, dubsiren)

Needs the TI C6000 compiler (see README). formant is Choral.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EFFECTS = ["wavefold", "dualshft", "formant", "eugate", "dubsiren"]

want = sys.argv[1:] or EFFECTS
bad = [n for n in want if n not in EFFECTS]
if bad:
    sys.exit("Unknown effect: " + ", ".join(bad) + ". Choose from: " + ", ".join(EFFECTS))
failed = []
for name in want:
    script = ROOT / "src" / "custom" / name / "build.py"
    if subprocess.run([sys.executable, "-B", str(script)]).returncode != 0:
        failed.append(name)
print("\nBuilt:", ", ".join(n for n in want if n not in failed) or "nothing")
if failed:
    sys.exit("Failed: " + ", ".join(failed))
