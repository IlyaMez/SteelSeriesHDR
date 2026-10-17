# Performance validation — October 17, 2026

Release x64 build, 3440 x 1440 HDR display, **Vivid color selection, rear richness 85%**, with full-side rear lights and bottom-third base accents. The local graphics driver rejected Desktop Duplication with `DXGI_ERROR_UNSUPPORTED`; the automatic Windows Graphics Capture fallback obtained borderless access and returned FP16 scRGB. The floating-point peak of 6 confirms that HDR values survived capture.

Final 12.015-second desktop pipeline benchmark (initialization excluded), recorded in `build/benchmark-richness.txt`:

| Measurement | Result |
|---|---:|
| Process CPU, one logical core = 100% | 0.390% |
| Process CPU, whole 16-thread machine = 100% | 0.024% |
| Working set | 48.9 MiB |
| Capture/color call time, mean / p95 | 0.112 / 0.276 ms |
| GPU sampling and small readback, mean | 0.007 ms |
| Completed frame samples | 127 (10.6/s on this desktop workload) |
| Shader-readable capture frames | 128 of 128 |
| Additional full-screen texture copies | 0 bytes |
| CPU readback per sampled frame | 36 KiB |
| Deferred busy readbacks | 128; skipped without waiting |

This is a 30 Hz **maximum**, not a promise of 30 fresh samples every second. The nonblocking pipeline trades some latency for avoiding stalls; driver scheduling and screen update cadence affect throughput. The measured GPU time excludes Windows/compositor capture work. This benchmark exercises capture, representative color selection, tone mapping, and report construction; it excludes actual USB writes and the GUI. It is not an in-game FPS benchmark or a before/after speedup claim.

Validation also includes 70 CPU assertions covering HDR colors, full-side rear mapping, bottom-third base accents, competing hues, HDR white glints, tiny HUD patches, dark saturated noise, neutral scenes, hue wraparound, stable color selection, black borders, smoothing, packet layout, redundant-update suppression, retry eligibility, the 10 Hz USB cap, held-color refresh cadence, orange color regression values, richness controls, and neutral-color preservation, plus an actual GPU test using synthetic FP16/SDR textures and all display rotation branches. This verifies algorithm behavior, not subjective color fidelity on a physical wall.

The orange color regression fixtures verify that, at exposure 0.59 and brightness 100%, rear outputs change from `(155,67,40)` / `(168,83,40)` to `(155,42,8)` / `(168,62,7)` at richness 85%. Peak brightness is unchanged and the common white component is reduced. These fixed sample values do not establish exact live capture values or physical LED colorimetry.

Reproduce with the GUI stopped:

```powershell
.\build.ps1 -Test
Start-Process .\build\ArenaHDR.exe -ArgumentList '--benchmark', 'build\benchmark-borderless.txt' -WindowStyle Hidden -Wait
Get-Content .\build\benchmark-borderless.txt
```

## Native UI smoke measurement

The redesigned UI was also measured during a separate 15.03-second live-sync desktop run, with the window visible and Arena 7 USB output enabled. The entire app process used **0.728% of one logical CPU core** (about 0.046% of this 16-thread machine) and **58.8 MiB** working set. This includes the app's UI and USB work, but not Windows compositor work in other processes. The scene was an ordinary desktop, not an in-game performance workload; these numbers are a sanity check, not directly comparable to the pipeline-only benchmark above.

To assess game impact, compare the same repeatable game scene with Arena HDR stopped and running in the tray, leaving resolution, HDR, frame cap, and game settings unchanged. Compare frame-time percentiles as well as average FPS. That test has not been performed here.
