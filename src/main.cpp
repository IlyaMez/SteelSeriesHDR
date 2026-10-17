#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <psapi.h>
#include <winrt/base.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include "arena.h"
#include "capture.h"
#include "output_gate.h"
#include "screen_sampler.h"
#include "version.h"
#include "ui.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace arena;
using Clock = std::chrono::steady_clock;
namespace {
constexpr int IdMonitor = 101, IdStart = 102, IdStop = 103, IdTest = 104, IdPreview = 105,
              IdBrightness = 106, IdExposure = 107, IdSmooth = 108, IdBars = 109, IdSwap = 110, IdCapture = 111, IdColorMode = 112, IdHold = 113, IdRichness = 114, IdDonate = 115;
constexpr UINT TrayMessage = WM_APP + 1;
HWND window{}, statusControl{}, detailControl{}, monitorControl{}, startControl{}, testControl{}, stopControl{};
HFONT font{}, titleFont{}, smallFont{}, headingFont{}, valueFont{};
HBRUSH background{}, surface{};
HWND tooltip{};
std::vector<Monitor> displays;
std::atomic_bool running{false}, quitWorker{false};
std::atomic_int brightness{80}, exposure{100}, smoothing{180};
std::atomic_int rearRichness{85};
std::atomic<ColorMode> colorMode{ColorMode::Vivid};
std::atomic_bool trimBars{true}, swapSides{false};
std::atomic_bool holdColors{false};
bool testing = false;
std::thread worker;
std::mutex stateMutex;
Colors latest{};
Colors heldColors{};
std::wstring status = L"Ready when you are.", details = L"Choose a display and start syncing, or test your four speaker lights.";
std::wstring iniPath;
bool trayInstalled = false;
int uiDpi = 96;
int scaled(int value) { return MulDiv(value, uiDpi, 96); }
RECT scaledRect(int left, int top, int right, int bottom) { return {scaled(left), scaled(top), scaled(right), scaled(bottom)}; }

std::wstring widen(const std::string& s) { return winrt::to_hstring(s).c_str(); }
void publish(const std::wstring& text, const std::wstring& detail = L"") {
    std::scoped_lock lock(stateMutex); status = text; if (!detail.empty()) details = detail;
}
void publishColors(Colors colors) { std::scoped_lock lock(stateMutex); latest = colors; }
std::wstring errorText(const std::exception& e) { return widen(e.what()); }
void stop() { quitWorker = true; if (worker.joinable()) worker.join(); running = false; }

void run(HMONITOR monitor, bool preview, bool test, CaptureBackend backend) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        Speaker speaker;
        if (!preview) speaker.connect();
        if (test) {
            publish(L"Speaker test: left base red, left rear green, right base blue, right rear amber.", L"Test runs for 8 seconds; then leaves a soft white light.");
            Colors colors{{{180, 0, 0}, {0, 180, 0}, {0, 0, 180}, {180, 90, 0}}};
            auto end = Clock::now() + std::chrono::seconds(8);
            while (!quitWorker && Clock::now() < end) {
                speaker.send(colors); publishColors(colors);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            Colors soft{{{40, 40, 40}, {40, 40, 40}, {40, 40, 40}, {40, 40, 40}}};
            speaker.send(soft); publishColors(soft);
            publish(L"Speaker test finished. Start screen sync when ready.");
        } else {
            std::unique_ptr<Capture> capture;
            LinearColors filtered{};
            bool first = true;
            auto lastFrame = Clock::now(), lastTick = lastFrame, retry = lastFrame;
            auto nextUsb = lastFrame;
            auto nextStatus = lastFrame;
            LinearColors zones{};
            ScreenSampler sampler;
            bool previousTrim = trimBars.load();
            auto previousMode = colorMode.load();
            bool haveZones = false;
            float maximum = 0;
            OutputGate outputGate;
            std::wstring usbError;
            std::vector<Pixel> pixels;
            auto deliver = [&](const Colors& colors, Clock::time_point now, bool repeatHeld = false) {
                if (preview) { publishColors(colors); return; }
                double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
                if (now < nextUsb || !outputGate.needsSend(colors, seconds, repeatHeld)) return;
                try {
                    speaker.send(colors);
                    outputGate.sent(colors, std::chrono::duration<double>(Clock::now().time_since_epoch()).count());
                    // A computed target is not a successful output. Keep the old
                    // swatches on throttling/failure; publish the exact sent bytes.
                    publishColors(colors); usbError.clear();
                } catch (const std::exception& e) { usbError = errorText(e); nextUsb = now + std::chrono::seconds(2); }
            };
            while (!quitWorker) {
                auto now = Clock::now();
                if (holdColors.load()) {
                    Colors held;
                    { std::scoped_lock lock(stateMutex); held = heldColors; }
                    deliver(held, now, true);
                    if (now >= nextStatus) {
                        if (!usbError.empty()) publish(usbError, L"Held colors retained; retrying USB in 2 seconds.");
                        else publish(L"Colors held. Compare the rear swatches with the wall.", preview
                            ? L"Preview only: held colors are not sent to the speakers."
                            : L"Repeating the held colors at up to 10 Hz.\nSwatches show successful USB writes, not hardware readback.");
                        nextStatus = now + std::chrono::milliseconds(500);
                    }
                    lastTick = now; lastFrame = now;
                    std::this_thread::sleep_until(now + std::chrono::microseconds(33333));
                    continue;
                }
                if (!capture && now >= retry) {
                    try {
                        capture = std::make_unique<Capture>(monitor, backend);
                        lastFrame = now; first = true; haveZones = false; sampler.reset();
                        publish(preview ? L"Preview only: capturing screen colors." : L"Screen sync running.", capture->description());
                    } catch (const winrt::hresult_error& e) {
                        publish(L"Capture unavailable: " + std::wstring(e.message()), L"Retrying in 2 seconds. Stop and select Compatibility if needed.");
                        retry = now + std::chrono::seconds(2);
                    } catch (const std::exception& e) {
                        publish(errorText(e), L"Retrying capture in 2 seconds."); retry = now + std::chrono::seconds(2);
                    }
                }
                bool hasFrame = false;
                if (capture) {
                    try { hasFrame = capture->read(pixels); }
                    catch (const winrt::hresult_error& e) {
                        publish(L"Capture interrupted: " + std::wstring(e.message()), L"Reconnecting after display or HDR mode change.");
                        capture.reset(); retry = now + std::chrono::seconds(2);
                    }
                }
                if (hasFrame) {
                    lastFrame = now; haveZones = true;
                }
                if (capture && haveZones) {
                    float dt = std::chrono::duration<float>(now - lastTick).count(); lastTick = now;
                    // Mode and border changes also apply to a static screen immediately.
                    bool trim = trimBars.load();
                    auto mode = colorMode.load();
                    if (hasFrame || trim != previousTrim || mode != previousMode)
                        zones = sampler.sample(pixels, Capture::Width, Capture::Height, trim, mode);
                    previousTrim = trim; previousMode = mode;
                    Colors colors{};
                    maximum = 0;
                    for (int z = 0; z < 4; ++z) {
                        maximum = std::max(maximum, peak(zones[z]));
                        auto mapped = toneMap(zones[z], exposure.load() / 100.f, 1.f);
                        filtered[z] = first ? mapped : smooth(filtered[z], mapped, dt, smoothing.load() / 1000.f);
                        colors[z] = encode(filtered[z], brightness.load() / 100.f, z % 2 ? rearRichness.load() / 100.f : 0.f);
                    }
                    first = false;
                    if (swapSides) { std::swap(colors[0], colors[2]); std::swap(colors[1], colors[3]); }
                    deliver(colors, now);
                    if (now >= nextStatus) {
                        auto stats = capture->stats();
                        std::wostringstream detail;
                        detail << capture->description() << (preview ? L"\nPreview targets" : L"\nUSB 10 Hz max; swatches = last successful RGB")
                               << L" | GPU " << std::fixed << std::setprecision(3)
                               << (stats.gpuSamples ? stats.gpuMilliseconds / stats.gpuSamples : 0) << L" ms";
                        if (!usbError.empty()) publish(usbError, L"Retrying USB connection in 2 seconds. Capture is still running.");
                        else if (maximum < .001f) publish(L"Captured image is black. Dark scenes or protected video can cause this.", detail.str());
                        else if (now - lastFrame > std::chrono::seconds(4)) publish(L"Screen unchanged; holding colors and waiting for an update.", detail.str());
                        else publish(preview ? L"Preview only: no lighting commands sent." : L"Screen sync running.", detail.str());
                        nextStatus = now + std::chrono::milliseconds(500);
                    }
                }
                std::this_thread::sleep_until(now + std::chrono::microseconds(33333));
            }
            publish(L"Stopped. Speakers keep the last color; Prism can take over again.");
        }
        winrt::uninit_apartment();
    } catch (const winrt::hresult_error& e) { publish(L"Windows error: " + std::wstring(e.message())); }
      catch (const std::exception& e) { publish(errorText(e)); }
    running = false;
}

HWND control(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, scaled(x), scaled(y), scaled(w), scaled(h), window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return c;
}
void tip(HWND target, const wchar_t* text) {
    TOOLINFOW info{sizeof(info)}; info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = window; info.uId = reinterpret_cast<UINT_PTR>(target); info.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}
HWND label(const wchar_t* text, int x, int y, int w, int h, HFONT type = nullptr, DWORD style = 0, int id = 0) {
    HWND c = control(L"STATIC", text, style | SS_NOPREFIX, x, y, w, h, id);
    if (type) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(type), TRUE);
    return c;
}
HWND button(const wchar_t* text, int x, int y, int w, int h, int id) {
    HWND c = control(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, h, id);
    SetWindowSubclass(c, ui::buttonProc, 1, 0); return c;
}
void slider(const wchar_t* name, int y, int id, int maximum, int value, const wchar_t* help) {
    label(name, 44, y, 250, 24);
    HWND c = control(TRACKBAR_CLASSW, name, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 40, y + 26, 374, 28, id);
    label(L"", 300, y, 110, 24, valueFont, SS_RIGHT, id + 1000);
    SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(id == IdExposure ? 10 : 0, maximum));
    SendMessageW(c, TBM_SETPAGESIZE, 0, maximum / 10); SendMessageW(c, TBM_SETPOS, TRUE, value);
    tip(c, help);
}
void updateValues() {
    SetWindowTextW(GetDlgItem(window, IdBrightness + 1000), (std::to_wstring(brightness.load()) + L"%").c_str());
    std::wostringstream value; value << std::fixed << std::setprecision(2) << exposure.load() / 100.f << L"\u00d7";
    SetWindowTextW(GetDlgItem(window, IdExposure + 1000), value.str().c_str());
    SetWindowTextW(GetDlgItem(window, IdRichness + 1000), (std::to_wstring(rearRichness.load()) + L"%").c_str());
    SetWindowTextW(GetDlgItem(window, IdSmooth + 1000), (std::to_wstring(smoothing.load()) + L" ms").c_str());
}
void saveSettings() {
    auto write = [](const wchar_t* key, int value) { WritePrivateProfileStringW(L"Settings", key, std::to_wstring(value).c_str(), iniPath.c_str()); };
    write(L"Brightness", brightness); write(L"Exposure", exposure); write(L"Smoothing", smoothing);
    write(L"TrimBars", trimBars); write(L"SwapSides", swapSides);
    write(L"ColorMode", static_cast<int>(colorMode.load()));
    write(L"RearRichness", rearRichness);
    write(L"Monitor", static_cast<int>(SendMessageW(monitorControl, CB_GETCURSEL, 0, 0)));
    write(L"CaptureBackend", static_cast<int>(SendMessageW(GetDlgItem(window, IdCapture), CB_GETCURSEL, 0, 0)));
}
void loadSettings() {
    wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
    iniPath = std::wstring(path); iniPath = iniPath.substr(0, iniPath.find_last_of(L"\\/") + 1) + L"ArenaHDR.ini";
    auto read = [](const wchar_t* key, int value) { return static_cast<int>(GetPrivateProfileIntW(L"Settings", key, value, iniPath.c_str())); };
    brightness = std::clamp(read(L"Brightness", 80), 0, 100);
    exposure = std::clamp(read(L"Exposure", 100), 10, 400);
    smoothing = std::clamp(read(L"Smoothing", 180), 0, 1000);
    trimBars = read(L"TrimBars", 1) != 0; swapSides = read(L"SwapSides", 0) != 0;
    colorMode = static_cast<ColorMode>(std::clamp(read(L"ColorMode", 0), 0, 2));
    rearRichness = std::clamp(read(L"RearRichness", 85), 0, 100);
}
void start(bool test) {
    if (running) return;
    stop();
    int index = static_cast<int>(SendMessageW(monitorControl, CB_GETCURSEL, 0, 0));
    if (!test && (index < 0 || index >= static_cast<int>(displays.size()))) { publish(L"No monitor selected."); return; }
    bool preview = !test && SendMessageW(GetDlgItem(window, IdPreview), BM_GETCHECK, 0, 0) == BST_CHECKED;
    holdColors = false; testing = test;
    SendMessageW(GetDlgItem(window, IdHold), BM_SETCHECK, BST_UNCHECKED, 0);
    quitWorker = false; running = true;
    publish(test ? L"Connecting to Arena 7..." : L"Starting capture...");
    auto backend = SendMessageW(GetDlgItem(window, IdCapture), CB_GETCURSEL, 0, 0) == 1 ? CaptureBackend::WindowsGraphicsCapture : CaptureBackend::Borderless;
    worker = std::thread(run, test ? nullptr : displays[index].handle, preview, test, backend);
}
void updateUi() {
    std::wstring text, detail; Colors colors;
    { std::scoped_lock lock(stateMutex); text = status; detail = details; colors = latest; }
    static std::wstring previousText, previousDetail;
    static Colors previousColors{};
    static int previousRunning = -1;
    if (text != previousText) { SetWindowTextW(statusControl, text.c_str()); previousText = text; }
    if (detail != previousDetail) { SetWindowTextW(detailControl, detail.c_str()); previousDetail = detail; }
    int active = running ? 1 : 0;
    if (active != previousRunning) {
        EnableWindow(startControl, !active); EnableWindow(testControl, !active); EnableWindow(stopControl, active);
        EnableWindow(monitorControl, !active); EnableWindow(GetDlgItem(window, IdPreview), !active);
        EnableWindow(GetDlgItem(window, IdCapture), !active); previousRunning = active;
        EnableWindow(GetDlgItem(window, IdHold), active && !testing);
    }
    if (colors != previousColors) {
        RECT rect = scaledRect(472, 194, 856, 348); InvalidateRect(window, &rect, FALSE); previousColors = colors;
    }
}
void tray(bool add) {
    NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = window; n.uID = 1;
    if (add) {
        n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; n.uCallbackMessage = TrayMessage;
        n.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1)); wcscpy_s(n.szTip, L"Arena HDR - click to open");
        trayInstalled = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
    } else if (trayInstalled) { Shell_NotifyIconW(NIM_DELETE, &n); trayInstalled = false; }
}
LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        window = h;
        auto makeFont = [](int size, int weight) { return CreateFontW(-scaled(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); };
        font = makeFont(15, FW_NORMAL); titleFont = makeFont(32, FW_SEMIBOLD);
        smallFont = makeFont(12, FW_NORMAL); headingFont = makeFont(18, FW_SEMIBOLD); valueFont = makeFont(15, FW_SEMIBOLD);
        background = CreateSolidBrush(ui::Canvas); surface = CreateSolidBrush(ui::Surface);
        tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, h, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, scaled(350));
        label(L"Arena HDR", 88, 22, 220, 44, titleFont);
        label(L"Bring your screen into the room.", 90, 68, 450, 25);
        label(L"v" ARENA_VERSION_W, 310, 40, 230, 24, smallFont);
        tip(button(L"\u2665  Donate", 766, 34, 110, 36, IdDonate), L"Support Ilya on Ko-fi. Opens your default browser.");
        label(L"Screen source", 44, 134, 345, 28, headingFont);
        label(L"Display", 44, 179, 86, 24);
        monitorControl = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 134, 174, 276, 180, IdMonitor);
        displays = monitors(); int selected = 0;
        for (size_t i = 0; i < displays.size(); ++i) {
            SendMessageW(monitorControl, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(displays[i].name.c_str()));
            if (displays[i].primary) selected = static_cast<int>(i);
        }
        selected = static_cast<int>(GetPrivateProfileIntW(L"Settings", L"Monitor", selected, iniPath.c_str()));
        if (selected < 0 || selected >= static_cast<int>(displays.size())) selected = 0;
        SendMessageW(monitorControl, CB_SETCURSEL, selected, 0);
        label(L"Capture", 44, 223, 86, 24);
        HWND captureControl = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 134, 218, 276, 150, IdCapture);
        SendMessageW(captureControl, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Borderless (recommended)"));
        SendMessageW(captureControl, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Compatibility (Windows yellow border)"));
        SendMessageW(captureControl, CB_SETCURSEL, GetPrivateProfileIntW(L"Settings", L"CaptureBackend", 0, iniPath.c_str()) == 1 ? 1 : 0, 0);
        SendMessageW(captureControl, CB_SETDROPPEDWIDTH, scaled(355), 0);
        tip(captureControl, L"Borderless uses HDR capture without the yellow frame when Windows permits it. Compatibility keeps the Windows capture border.");
        label(L"Colors", 44, 267, 86, 24);
        HWND modeControl = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 134, 262, 276, 150, IdColorMode);
        for (const auto* label : {L"Vivid (recommended)", L"Bright areas", L"Average"})
            SendMessageW(modeControl, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(modeControl, CB_SETCURSEL, static_cast<int>(colorMode.load()), 0);
        tip(modeControl, L"Vivid follows prominent saturated colors. Bright areas favors luminous regions. Average gives a softer blended wash.");
        label(L"Color & motion", 44, 346, 345, 28, headingFont);
        slider(L"Brightness", 390, IdBrightness, 100, brightness, L"Overall brightness of all four lights.");
        slider(L"Exposure", 454, IdExposure, 400, exposure, L"Adjust scene intensity before highlight compression. 1.00x is neutral exposure.");
        slider(L"Rear color richness", 518, IdRichness, 100, rearRichness, L"Reduce white in saturated rear colors. 0% is natural; 100% gives the strongest color. Neutral grays stay neutral.");
        slider(L"Smoothing", 582, IdSmooth, 1000, smoothing, L"Longer times soften transitions. 0 ms follows color changes immediately.");
        updateValues();
        label(L"Live lighting", 472, 134, 250, 28, headingFont);
        label(L"Rear lights follow each full side of the screen.", 472, 168, 384, 22, smallFont);
        startControl = button(L"Start screen sync", 452, 390, 204, 44, IdStart);
        stopControl = button(L"Stop", 668, 390, 76, 44, IdStop);
        testControl = button(L"Test lights", 756, 390, 120, 44, IdTest);
        tip(testControl, L"Eight-second test: left base red, left rear green, right base blue, right rear amber. Ends in soft white.");
        HWND preview = control(L"BUTTON", L"Preview only", BS_AUTOCHECKBOX | WS_TABSTOP, 456, 454, 210, 25, IdPreview);
        tip(preview, L"Show captured colors without sending anything to your speakers.");
        HWND hold = control(L"BUTTON", L"Hold colors", BS_AUTOCHECKBOX | WS_TABSTOP, 684, 454, 188, 25, IdHold);
        tip(hold, L"Freeze and repeat the last output so you can compare the swatches to the real lights. Adjustments pause while held.");
        HWND bars = control(L"BUTTON", L"Ignore black borders", BS_AUTOCHECKBOX | WS_TABSTOP, 456, 492, 220, 25, IdBars);
        SendMessageW(bars, BM_SETCHECK, trimBars ? BST_CHECKED : BST_UNCHECKED, 0);
        HWND swap = control(L"BUTTON", L"Swap left / right", BS_AUTOCHECKBOX | WS_TABSTOP, 684, 492, 188, 25, IdSwap);
        SendMessageW(swap, BM_SETCHECK, swapSides ? BST_CHECKED : BST_UNCHECKED, 0);
        statusControl = label(L"", 472, 556, 384, 46, valueFont);
        detailControl = label(L"", 472, 608, 384, 46, smallFont);
        label(L"Disable Arena 7 in GG Prism before syncing.", 26, 688, 440, 24, smallFont);
        label(L"Minimize to tray  \u00b7  Close to stop", 506, 688, 368, 24, smallFont, SS_RIGHT);
        SetTimer(h, 1, 100, nullptr); updateUi(); return 0;
    }
    case WM_HSCROLL:
        brightness = static_cast<int>(SendMessageW(GetDlgItem(h, IdBrightness), TBM_GETPOS, 0, 0));
        exposure = std::max(10, static_cast<int>(SendMessageW(GetDlgItem(h, IdExposure), TBM_GETPOS, 0, 0)));
        smoothing = static_cast<int>(SendMessageW(GetDlgItem(h, IdSmooth), TBM_GETPOS, 0, 0));
        rearRichness = static_cast<int>(SendMessageW(GetDlgItem(h, IdRichness), TBM_GETPOS, 0, 0));
        updateValues();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IdDonate:
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(h, L"open", L"https://ko-fi.com/ilyamezerowsky", nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                MessageBoxW(h, L"Could not open your browser. Visit https://ko-fi.com/ilyamezerowsky to support the project.", L"Donate on Ko-fi", MB_OK | MB_ICONINFORMATION);
            break;
        case IdStart: start(false); break;
        case IdTest: start(true); break;
        case IdStop: stop(); break;
        case IdBars: trimBars = SendMessageW(GetDlgItem(h, IdBars), BM_GETCHECK, 0, 0) == BST_CHECKED; break;
        case IdSwap: swapSides = SendMessageW(GetDlgItem(h, IdSwap), BM_GETCHECK, 0, 0) == BST_CHECKED; break;
        case IdHold: {
            bool hold = SendMessageW(GetDlgItem(h, IdHold), BM_GETCHECK, 0, 0) == BST_CHECKED;
            { std::scoped_lock lock(stateMutex); heldColors = latest; holdColors = hold; }
            break;
        }
        case IdColorMode:
            if (HIWORD(wp) == CBN_SELCHANGE)
                colorMode = static_cast<ColorMode>(SendMessageW(GetDlgItem(h, IdColorMode), CB_GETCURSEL, 0, 0));
            break;
        } return 0;
    case WM_TIMER: if (IsWindowVisible(h) && !IsIconic(h)) updateUi(); return 0;
    case WM_DRAWITEM: {
        auto item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (item->CtlType == ODT_BUTTON) { ui::drawButton(*item, valueFont, uiDpi, item->CtlID == IdStart, item->CtlID == IdDonate); return TRUE; }
        break;
    }
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp); HWND child = reinterpret_cast<HWND>(lp);
        RECT r{}; GetWindowRect(child, &r); MapWindowPoints(nullptr, h, reinterpret_cast<POINT*>(&r), 2);
        bool onCard = (r.left < scaled(434) && r.top >= scaled(116) && r.top < scaled(662)) ||
            (r.left >= scaled(452) && ((r.top >= scaled(116) && r.top < scaled(366)) || (r.top >= scaled(540) && r.top < scaled(666))));
        SetBkColor(dc, onCard ? ui::Surface : ui::Canvas);
        SetTextColor(dc, reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0)) == smallFont ? ui::Muted : ui::Ink);
        return reinterpret_cast<LRESULT>(onCard ? surface : background);
    }
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(h, &r); FillRect(reinterpret_cast<HDC>(wp), &r, background); return 1; }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(h, &ps); Colors colors;
        { std::scoped_lock lock(stateMutex); colors = latest; }
        RECT client{}; GetClientRect(h, &client); FillRect(dc, &client, background);
        for (RECT card : {scaledRect(24, 116, 434, 314), scaledRect(24, 330, 434, 662),
                          scaledRect(452, 116, 876, 366), scaledRect(452, 540, 876, 666)})
            ui::rounded(dc, card, ui::Surface, ui::Line, scaled(20));
        DrawIconEx(dc, scaled(26), scaled(30), LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1)), scaled(48), scaled(48), 0, nullptr, DI_NORMAL);
        SelectObject(dc, valueFont); SetBkMode(dc, TRANSPARENT);
        for (int side = 0; side < 2; ++side) {
            int x = 472 + side * 200;
            const auto rear = colors[side * 2 + 1], base = colors[side * 2];
            RECT r = scaledRect(x, 196, x + 184, 276);
            ui::rounded(dc, r, RGB(rear.r, rear.g, rear.b), ui::Line, scaled(14));
            SetTextColor(dc, ui::Ink); SelectObject(dc, valueFont);
            RECT name = scaledRect(x, 284, x + 184, 306);
            DrawTextW(dc, side ? L"Right rear" : L"Left rear", -1, &name, DT_SINGLELINE);
            wchar_t hex[8]{}; swprintf_s(hex, L"#%02X%02X%02X", rear.r, rear.g, rear.b);
            SelectObject(dc, smallFont); SetTextColor(dc, ui::Muted);
            DrawTextW(dc, hex, -1, &name, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
            r = scaledRect(x, 320, x + 30, 340);
            ui::rounded(dc, r, RGB(base.r, base.g, base.b), ui::Line, scaled(6));
            wchar_t baseText[32]{}; swprintf_s(baseText, L"Base  #%02X%02X%02X", base.r, base.g, base.b);
            r = scaledRect(x + 40, 320, x + 184, 342); DrawTextW(dc, baseText, -1, &r, DT_SINGLELINE | DT_VCENTER);
        }
        EndPaint(h, &ps); return 0;
    }
    case WM_SIZE: if (wp == SIZE_MINIMIZED) { tray(true); if (trayInstalled) ShowWindow(h, SW_HIDE); } return 0;
    case TrayMessage:
        if (lp == WM_LBUTTONDBLCLK || lp == WM_LBUTTONUP || lp == WM_RBUTTONUP) { ShowWindow(h, SW_RESTORE); SetForegroundWindow(h); tray(false); } return 0;
    case WM_CLOSE: saveSettings(); stop(); DestroyWindow(h); return 0;
    case WM_DESTROY:
        tray(false); KillTimer(h, 1);
        for (HFONT f : {font, titleFont, smallFont, headingFont, valueFont}) DeleteObject(f);
        DeleteObject(background); DeleteObject(surface); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

double processCpuSeconds() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) throw std::runtime_error("Cannot read process CPU time");
    auto ticks = [](FILETIME t) { return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 1e7;
}
int diagnose(const std::wstring& path, bool captureFrames, bool lightingTest, bool benchmark = false, CaptureBackend backend = CaptureBackend::Borderless) {
    std::ofstream out(path); if (!out) return 2;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto devices = findDevices(); out << "Arena 7 HID collections: " << devices.size() << '\n';
        for (const auto& d : devices) out << "usagePage=0x" << std::hex << d.usagePage << " usage=0x" << d.usage << std::dec << " outputBytes=" << d.outputBytes << '\n';
        auto screens = monitors(); out << "Monitors: " << screens.size() << '\n';
        for (const auto& m : screens) out << winrt::to_string(m.name) << '\n';
        if (lightingTest) {
            Speaker speaker; speaker.connect();
            Colors colors{{{150, 0, 0}, {0, 150, 0}, {0, 0, 150}, {150, 75, 0}}};
            speaker.send(colors); out << "Lighting output report accepted by Windows. Visual confirmation required.\n";
        }
        if (captureFrames) {
            if (screens.empty()) throw std::runtime_error("No monitor available");
            auto target = screens.front(); for (const auto& s : screens) if (s.primary) target = s;
            Capture capture(target.handle, backend); std::vector<Pixel> pixels; int frames = 0; float maximum = 0;
            ScreenSampler sampler;
            std::vector<double> iterationMs;
            const double cpuStart = processCpuSeconds();
            auto began = Clock::now(), end = began + std::chrono::seconds(benchmark ? 12 : 5);
            while (Clock::now() < end) {
                auto tick = Clock::now();
                if (capture.read(pixels)) {
                    ++frames;
                    for (const auto& p : pixels) maximum = std::max({maximum, clean(p.r), clean(p.g), clean(p.b)});
                    auto zones = sampler.sample(pixels, Capture::Width, Capture::Height, true);
                    Colors colors{};
                    for (int i = 0; i < 4; ++i) colors[i] = encode(toneMap(zones[i], 1, 1), .8f, i % 2 ? .85f : 0.f);
                    auto report = lightingReport(colors, 64);
                    if (report[0] != 6) throw std::runtime_error("Invalid lighting report");
                }
                iterationMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - tick).count());
                std::this_thread::sleep_until(tick + std::chrono::microseconds(33333));
            }
            double elapsed = std::chrono::duration<double>(Clock::now() - began).count();
            double cpuPercent = 100 * (processCpuSeconds() - cpuStart) / elapsed;
            out << "Capture: " << winrt::to_string(capture.description()) << "\nFrames: " << frames << "\nPeak linear sample: " << maximum << '\n';
            auto stats = capture.stats();
            std::sort(iterationMs.begin(), iterationMs.end());
            double total = 0; for (double ms : iterationMs) total += ms;
            PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory);
            K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
            out << std::fixed << std::setprecision(3)
                << "Measured seconds (initialization excluded): " << elapsed
                << "\nCPU % of one logical core: " << cpuPercent
                << "\nCPU % of whole machine: " << cpuPercent / GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)
                << "\nWorking set MiB: " << memory.WorkingSetSize / 1048576.0
                << "\nCapture + color CPU-thread wall ms mean: " << total / iterationMs.size()
                << "\nCapture + color CPU-thread wall ms p95: " << iterationMs[iterationMs.size() * 95 / 100]
                << "\nGPU sample/readback ms mean: " << (stats.gpuSamples ? stats.gpuMilliseconds / stats.gpuSamples : 0)
                << "\nGPU timing samples: " << stats.gpuSamples
                << "\nDirect sampled frames: " << stats.directFrames << " / " << stats.submitted
                << "\nExtra full-screen texture copy bytes: " << stats.copiedBytes
                << "\nReadback bytes: " << stats.readbacks * Capture::Width * Capture::Height * sizeof(Pixel)
                << "\nBusy readbacks skipped (never blocked): " << stats.busyReadbacks
                << "\nColor selection: Vivid, full-side rear / bottom-third base, rear richness 85%."
                << "\nBenchmark excludes USB I/O, GUI, and Windows compositor GPU work.\n";
            if (!frames) throw std::runtime_error("No frames received");
        }
        out << "PASS\n"; return 0;
    } catch (const winrt::hresult_error& e) { out << "FAIL 0x" << std::hex << static_cast<uint32_t>(e.code().value) << ": " << winrt::to_string(e.message()) << '\n'; }
      catch (const std::exception& e) { out << "FAIL: " << e.what() << '\n'; }
    return 1;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    uiDpi = static_cast<int>(GetDpiForSystem());
    // Fit the fixed layout to the available work area, including at high scale.
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    uiDpi = std::min({uiDpi, MulDiv(work.right - work.left - 32, 96, 900), MulDiv(work.bottom - work.top - 64, 96, 724)});
    uiDpi = std::max(uiDpi, 64);
    int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3 && std::wstring(argv[1]) == L"--gpu-test") {
        std::ofstream out(argv[2]); LocalFree(argv);
        try { Capture::testGpuPipeline(); out << "PASS: GPU FP16 HDR, SDR linearization, physical zones, and 90/180/270 rotation.\n"; return 0; }
        catch (const winrt::hresult_error& e) { out << "FAIL: " << winrt::to_string(e.message()) << '\n'; }
        catch (const std::exception& e) { out << "FAIL: " << e.what() << '\n'; }
        return 1;
    }
    if (argc >= 3 && (std::wstring(argv[1]) == L"--diagnose" || std::wstring(argv[1]) == L"--capture-test" || std::wstring(argv[1]) == L"--lighting-test" || std::wstring(argv[1]) == L"--benchmark" || std::wstring(argv[1]) == L"--benchmark-wgc")) {
        std::wstring mode = argv[1], path = argv[2]; LocalFree(argv);
        bool bench = mode == L"--benchmark" || mode == L"--benchmark-wgc";
        return diagnose(path, mode == L"--capture-test" || bench, mode == L"--lighting-test", bench,
                        mode == L"--benchmark-wgc" ? CaptureBackend::WindowsGraphicsCapture : CaptureBackend::Borderless);
    }
    LocalFree(argv);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ArenaHDR-Desktop-App");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"ArenaHDR", nullptr)) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); PostMessageW(existing, TrayMessage, 0, WM_LBUTTONUP); }
        if (mutex) CloseHandle(mutex); return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
    loadSettings();
    WNDCLASSW wc{}; wc.lpfnWndProc = wndProc; wc.hInstance = instance; wc.lpszClassName = L"ArenaHDR";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    RECT r = scaledRect(0, 0, 900, 724); AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"Arena HDR", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, nullptr);
    if (!h) { if (mutex) CloseHandle(mutex); return 1; }
    ShowWindow(h, show);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    stop(); if (mutex) CloseHandle(mutex); return static_cast<int>(msg.wParam);
}
