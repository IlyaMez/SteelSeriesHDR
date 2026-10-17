# Contributing

Build with `./build.ps1 -Test` on Windows before submitting a change. Warnings are treated as errors. Keep GPU validation enabled on a desktop GPU; `-SkipGpuTests` is for environments where that validation is unavailable.

Keep the app small and native. Capture, color selection, and USB output should have bounded work and avoid unnecessary full-screen copies. Changes to sampling or tone mapping should include meaningful regressions in `tests/tests.cpp`; capture changes should also exercise the real GPU fixtures.

For UI changes, check keyboard navigation, readable labels at your display scale, sync/stop/hold, and minimize/restore. Avoid sending diagnostic commands to unrelated USB devices. The current HID code only supports the Arena 7 vendor collection and its validated report length.

For bug reports, provide reproduction steps, expected/actual behavior, Windows version, GPU/driver, app version, capture/color modes, HDR status, and whether GG Prism is enabled for the device. If lights differ from the app, compare the fixed test and Hold colors, and include the rear hex values. Attach text diagnostics after reviewing them; do not include personal screenshots or settings unless needed.

Keep pull requests focused and describe the user-visible result and checks performed. Preserve attribution in `THIRD_PARTY.md`.
