# Preparing a release

The initial target is `v0.1.0-preview.1`. The preview is portable Windows x64 software for Arena 7; it is not a replacement firmware or a driver.

1. Select the project's license and add `LICENSE` before presenting the repository as open source. Update the license line in `README.md`. Packaging includes `LICENSE` automatically when present.
2. Update `VERSION` and `CHANGELOG.md` together. `build.ps1` generates matching UI and executable metadata.
3. Close the running app and run `./build.ps1 -Test` on a Windows desktop with a Direct3D 11 GPU. Check the UI, test pattern, HDR capture, stop, and tray restore on hardware.
4. Run `./package.ps1`. It writes `dist/ArenaHDR-<version>-windows-x64.zip` and `.zip.sha256`, with an allowlist and hash verification. Inspect the resulting ZIP; it must contain no settings, diagnostics, test captures, or compiler output except the app EXE.
5. Check `git status` and the staged diff. Commit the source, assets, and documentation. Keep `build/` and `dist/` out of Git.
6. When ready to publish, create the GitHub repository, set its remote, and push `main`. Let the Windows workflow finish. It compiles with warnings as errors and runs CPU tests; it intentionally skips desktop GPU tests.
7. Tag the verified commit with `v<VERSION>` and create a GitHub **prerelease**, attaching the local verified ZIP and its checksum. Use the matching changelog entry as release notes. Clearly describe the unsigned preview and known compatibility limits.

The build workflow has read-only repository permissions and uploads build artifacts. It does not tag, push, or publish a release. Configure publishing separately if it is wanted later.

## Local verification record

The initial preview was built with MSVC x64 and Windows SDK 10.0.26100. The 70 CPU assertions and synthetic-texture GPU checks passed locally. Live FP16 capture and Arena 7 writes have been exercised on one machine. Broader compatibility, in-game frame-time impact, and exact physical color reproduction remain unverified.

Performance numbers and their exclusions are in `PERFORMANCE.md`. Do not turn them into an in-game FPS guarantee.
