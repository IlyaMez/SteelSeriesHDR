#include "startup.h"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace arena::startup;
    // Never touch the user's actual Run key, even when a test fails.
    auto path = L"Software\\ArenaHDR-Startup-Test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HKEY key{};
    auto check = [](bool pass, const char* message) { if (!pass) throw std::runtime_error(message); };
    int result = 0;
    try {
        check(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, REG_OPTION_VOLATILE,
                              KEY_ALL_ACCESS, nullptr, &key, nullptr) == ERROR_SUCCESS, "Create isolated key");
        const std::wstring exe = L"C:\\Program Files\\Arena HDR\\\u05d0\\ArenaHDR.exe";
        bool active = true;
        check(enabled(key, exe, active) == ERROR_SUCCESS && !active, "Missing registration defaults off");
        check(setEnabled(key, exe, false) == ERROR_SUCCESS, "Disabling absent registration succeeds");
        check(setEnabled(key, exe, true) == ERROR_SUCCESS, "Enable with spaces and Unicode");
        check(enabled(key, exe, active) == ERROR_SUCCESS && active, "Registration survives re-read");
        wchar_t value[261]{}; DWORD bytes = sizeof(value);
        check(RegGetValueW(key, nullptr, ValueName, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS &&
              value == L"\"" + exe + L"\" --startup", "Stored command quotes full executable and includes startup argument");
        check(enabled(key, L"C:\\Moved\\ArenaHDR.exe", active) == ERROR_SUCCESS && !active, "Moved executable does not report stale registration as enabled");
        check(setEnabled(key, std::wstring(260, L'a'), true) == ERROR_FILENAME_EXCED_RANGE, "Reject oversized Run command");
        check(enabled(key, exe, active) == ERROR_SUCCESS && active, "Failed update preserves existing registration");
        check(setEnabled(key, L"", true) == ERROR_INVALID_PARAMETER, "Reject empty executable path");
        check(setEnabled(key, L"C:\\bad\"path.exe", true) == ERROR_INVALID_PARAMETER, "Reject embedded quote");
        DWORD sentinel = 42;
        check(RegSetValueExW(key, L"OtherApp", 0, REG_DWORD, reinterpret_cast<BYTE*>(&sentinel), sizeof(sentinel)) == ERROR_SUCCESS, "Create unrelated startup entry");
        check(setEnabled(key, exe, false) == ERROR_SUCCESS, "Disable removes registration");
        check(enabled(key, exe, active) == ERROR_SUCCESS && !active, "Disable survives re-read");
        DWORD read = 0; bytes = sizeof(read);
        check(RegGetValueW(key, nullptr, L"OtherApp", RRF_RT_REG_DWORD, nullptr, &read, &bytes) == ERROR_SUCCESS && read == sentinel,
              "Disabling preserves other entries");
        HKEY readOnly{};
        check(RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &readOnly) == ERROR_SUCCESS, "Open read-only key");
        auto denied = setEnabled(readOnly, exe, true); RegCloseKey(readOnly);
        check(denied == ERROR_ACCESS_DENIED, "Permission failure is returned to caller");
        std::cout << "PASS: startup registration, quoting, Unicode, persistence, removal, and error handling\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; result = 1; }
    if (key) { RegCloseKey(key); RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str()); }
    return result;
}
