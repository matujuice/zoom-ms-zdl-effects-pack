# Cover rules (moved from CLAUDE.md)

- 128 x 64, 1 bit. Previews black on white (project convention: dark blue on white, stretched 1.4x). The screen shows the first three knobs: labels at y=37, number boxes at y=46..61 (about 20 wide at x=14, 55, 96), dial drawn by `_VSquash` under them.
- Pixels are 1.4x taller than wide (`build/lcd_geometry.py`). Draw round things through `_VSquash` or divide y by 1.4; check the preview stretched 1.4x.
- No bottom rule on WaveFold, DualShft, Choral, EuGate. DubSiren's framed bottom edge stays (Luca, 2026-10-03).
- Use `Canvas` text helpers. Knob labels come from manifest names: renaming a knob means re-running `make_cover.py` (EuGate's labels are in its `LABELS`).
