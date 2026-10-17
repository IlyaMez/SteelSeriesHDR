# Protocol attribution

The Arena 7 USB lighting packet layout and zone order were researched and published by **Maciej Szamowski** in [Prismatic for macOS](https://github.com/szamski/Prismatic-for-macOS), specifically `prisma-led-macos/ArenaUSBController.swift` (accessed October 16, 2026).

This project implements that protocol independently using Windows HID APIs. It does not include the upstream Swift code. The upstream project is MIT licensed; its research is credited here for making interoperability possible.

Windows SDK headers, Direct3D, C++/WinRT, and HID libraries are provided by Microsoft and are used under their respective SDK terms.
