#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "color.h"

namespace arena {
struct DeviceInfo { std::wstring path; USHORT usagePage{}, usage{}, outputBytes{}; };
std::vector<DeviceInfo> findDevices();
class Speaker {
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    USHORT outputBytes_{};
public:
    Speaker() = default;
    Speaker(const Speaker&) = delete;
    Speaker& operator=(const Speaker&) = delete;
    ~Speaker() { close(); }
    void connect();
    void close();
    void send(const Colors& colors);
    bool connected() const { return handle_ != INVALID_HANDLE_VALUE; }
};
}
