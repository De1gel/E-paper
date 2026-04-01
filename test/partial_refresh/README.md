# Partial Refresh Test Notes

This folder keeps the partial-refresh experiment notes out of the runtime app flow.

## Verified capability
- Device can accept a partial-window command path using `CMD_PARTIAL_WINDOW (0x83)`.
- Reference implementation is modularized in:
  - `src/display/PartialRefresh.h`
  - `src/display/PartialRefresh.cpp`

## Runtime policy
- Main app no longer runs random partial-refresh demo logic.
- Production flow remains standard page carousel rendering.

## How to re-run experiment (future)
1. Draw full background frame once.
2. Call `partial_refresh::fillWindowSolid(x, y, w, h, color_nibble)` on a small block.
3. Observe whether non-target regions stay visually stable.

## Caution
- Partial refresh quality still depends on panel/controller waveform behavior.
- Keep this as an experiment path until long-run stability is validated.
