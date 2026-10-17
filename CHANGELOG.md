# Changelog

## 0.1.0-preview.1 — 2026-10-17

First portable Windows x64 preview for SteelSeries Arena 7.

- HDR-aware FP16 screen capture with borderless Desktop Duplication / Windows Graphics Capture fallback.
- Full-side rear lighting, bottom-third base accents, and Vivid, Bright areas, and Average selection modes.
- Brightness, exposure, rear color richness, smoothing, border trimming, and side swapping.
- Native card layout, prominent rear previews, live adjustment values, tooltips, tray support, and optional Ko-fi support link.
- Fixed-color test, preview mode, held-color comparison, and successful-write RGB previews.
- Nonblocking GPU readback, 30 Hz worker cap, 10 Hz USB cap, and coalesced color output.
- CPU/GPU checks, diagnostics, performance notes, Windows CI, and verified ZIP/checksum packaging.

Known limits: Arena 7 only; no onboard-effect restoration; protected video may be black; Dolby Vision and all exclusive-fullscreen configurations are not verified. Physical color fidelity varies with the LEDs and room. The preview is unsigned.
