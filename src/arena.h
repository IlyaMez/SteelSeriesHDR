#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "color.h"

namespace arena {
constexpr USHORT VendorId = 0x1038;
constexpr USHORT ProductIdArena7 = 0x1A00;
constexpr USHORT ProductIdArena9 = 0x1A05;

inline constexpr bool isSupportedDevice(USHORT vid, USHORT pid) {
    return vid == VendorId && (pid == ProductIdArena7 || pid == ProductIdArena9);
}

inline const char* deviceModelName(USHORT pid) {
    switch (pid) {
    case ProductIdArena7: return "Arena 7";
    case ProductIdArena9: return "Arena 9";
    default: return "Arena";
    }
}

inline const wchar_t* deviceModelNameW(USHORT pid) {
    switch (pid) {
    case ProductIdArena7: return L"Arena 7";
    case ProductIdArena9: return L"Arena 9";
    default: return L"Arena";
    }
}

struct DeviceInfo {
    std::wstring path;
    USHORT usagePage{};
    USHORT usage{};
    USHORT outputBytes{};
    USHORT productId{};
};

std::vector<DeviceInfo> findDevices();

class Speaker {
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    USHORT outputBytes_{};
    USHORT productId_{};
public:
    Speaker() = default;
    Speaker(const Speaker&) = delete;
    Speaker& operator=(const Speaker&) = delete;
    ~Speaker() { close(); }
    void connect();
    void close();
    void send(const Colors& colors);
    bool connected() const { return handle_ != INVALID_HANDLE_VALUE; }
    USHORT productId() const { return productId_; }
    const char* modelName() const { return deviceModelName(productId_); }
    const wchar_t* modelNameW() const { return deviceModelNameW(productId_); }
};
}
