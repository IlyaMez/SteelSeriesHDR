#include "arena.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <stdexcept>

namespace arena {
std::vector<DeviceInfo> findDevices() {
    GUID guid{}; HidD_GetHidGuid(&guid);
    HDEVINFO list = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (list == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate USB HID devices");
    struct ListGuard { HDEVINFO h; ~ListGuard() { SetupDiDestroyDeviceInfoList(h); } } guard{list};
    std::vector<DeviceInfo> found;
    SP_DEVICE_INTERFACE_DATA item{}; item.cbSize = sizeof(item);
    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(list, nullptr, &guid, index, &item); ++index) {
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(list, &item, nullptr, 0, &bytes, nullptr);
        if (!bytes) continue;
        std::vector<uint8_t> data(bytes);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(data.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(list, &item, detail, bytes, nullptr, nullptr)) continue;
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attr{}; attr.Size = sizeof(attr);
        if (HidD_GetAttributes(h, &attr) && isSupportedDevice(attr.VendorID, attr.ProductID)) {
            PHIDP_PREPARSED_DATA prep = nullptr;
            if (HidD_GetPreparsedData(h, &prep)) {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(prep, &caps) == HIDP_STATUS_SUCCESS)
                    found.push_back({detail->DevicePath, caps.UsagePage, caps.Usage, caps.OutputReportByteLength, attr.ProductID});
                HidD_FreePreparsedData(prep);
            }
        }
        CloseHandle(h);
    }
    return found;
}
void Speaker::close() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
    productId_ = 0;
}
void Speaker::connect() {
    close();
    auto devices = findDevices();
    DWORD error = 0;
    std::string candidateName = "Arena";
    for (const auto& d : devices) {
        candidateName = deviceModelName(d.productId);
        // Never send lighting commands to the audio or consumer-control collections.
        if (d.usagePage != 0xFFC0 || (d.outputBytes != 64 && d.outputBytes != 65)) continue;
        handle_ = CreateFileW(d.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle_ != INVALID_HANDLE_VALUE) {
            outputBytes_ = d.outputBytes;
            productId_ = d.productId;
            return;
        }
        error = GetLastError();
    }
    if (error) throw std::runtime_error(candidateName + " USB access failed (Windows error " + std::to_string(error) + "). Disable Arena in Prism or exit GG and retry.");
    throw std::runtime_error("Arena lighting interface not found. Connect your Arena 7 or Arena 9 speakers by USB.");
}
void Speaker::send(const Colors& colors) {
    if (!connected()) connect();
    auto report = lightingReport(colors, outputBytes_);
    if (!HidD_SetOutputReport(handle_, report.data(), static_cast<ULONG>(report.size()))) {
        DWORD error = GetLastError();
        std::string name = modelName();
        close();
        throw std::runtime_error(name + " lighting write failed (Windows error " + std::to_string(error) + "). Check USB and Prism ownership.");
    }
}
}
