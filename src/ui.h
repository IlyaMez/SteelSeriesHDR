#pragma once
#include <windows.h>
#include <commctrl.h>

// Native controls retain keyboard navigation and accessibility. Only their
// decoration is custom; no animation or additional rendering thread is needed.
namespace ui {
constexpr COLORREF Canvas = RGB(242, 245, 249), Surface = RGB(255, 255, 255),
    Ink = RGB(24, 38, 56), Muted = RGB(96, 112, 132), Line = RGB(221, 228, 237),
    Teal = RGB(15, 118, 110);

inline void rounded(HDC dc, RECT r, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, border);
    auto oldBrush = SelectObject(dc, brush); auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen); DeleteObject(brush);
}

inline LRESULT CALLBACK buttonProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEMOVE && !GetPropW(h, L"ArenaHover")) {
        SetPropW(h, L"ArenaHover", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track); InvalidateRect(h, nullptr, FALSE);
    } else if (msg == WM_MOUSELEAVE || msg == WM_NCDESTROY) {
        RemovePropW(h, L"ArenaHover"); InvalidateRect(h, nullptr, FALSE);
        if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, buttonProc, 1);
    }
    return DefSubclassProc(h, msg, wp, lp);
}

inline void drawButton(const DRAWITEMSTRUCT& item, HFONT font, int dpi, bool primary, bool donate) {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hover = GetPropW(item.hwndItem, L"ArenaHover") != nullptr;
    COLORREF fill = primary ? Teal : Surface, border = primary ? Teal : Line;
    if (donate) { fill = RGB(255, 245, 240); border = RGB(245, 207, 187); }
    if (hover && !disabled) fill = primary ? RGB(12, 98, 92) : RGB(232, 240, 244);
    if (pressed) fill = primary ? RGB(9, 78, 74) : RGB(218, 229, 237);
    if (disabled) { fill = RGB(232, 237, 242); border = fill; }
    HBRUSH bg = CreateSolidBrush(Canvas); FillRect(item.hDC, &item.rcItem, bg); DeleteObject(bg);
    rounded(item.hDC, item.rcItem, fill, border, MulDiv(14, dpi, 96));
    auto oldFont = SelectObject(item.hDC, font); SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? Muted : primary ? Surface : Ink);
    wchar_t text[128]{}; GetWindowTextW(item.hwndItem, text, 128);
    RECT r = item.rcItem; DrawTextW(item.hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        InflateRect(&r, -MulDiv(5, dpi, 96), -MulDiv(5, dpi, 96)); DrawFocusRect(item.hDC, &r);
    }
    SelectObject(item.hDC, oldFont);
}
}
