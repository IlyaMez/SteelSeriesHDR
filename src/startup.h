#pragma once
#include <windows.h>
#include <string>

namespace arena::startup {
inline constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t ValueName[] = L"ArenaHDR";

inline std::wstring command(const std::wstring& executable) {
    return L"\"" + executable + L"\" --startup";
}

// The key is supplied by the caller so tests can use an isolated registry key.
inline LSTATUS enabled(HKEY key, const std::wstring& executable, bool& result) {
    result = false;
    wchar_t value[261]{};
    DWORD bytes = sizeof(value);
    auto error = RegGetValueW(key, nullptr, ValueName, RRF_RT_REG_SZ, nullptr, value, &bytes);
    if (error == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (error != ERROR_SUCCESS) return error;
    result = _wcsicmp(value, command(executable).c_str()) == 0;
    return ERROR_SUCCESS;
}

inline LSTATUS setEnabled(HKEY key, const std::wstring& executable, bool enable) {
    if (!enable) {
        auto error = RegDeleteValueW(key, ValueName);
        return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
    }
    if (executable.empty() || executable.find(L'"') != std::wstring::npos) return ERROR_INVALID_PARAMETER;
    auto value = command(executable);
    // Windows documents a 260-character limit for Run command lines.
    if (value.size() > 260) return ERROR_FILENAME_EXCED_RANGE;
    return RegSetValueExW(key, ValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}
}
