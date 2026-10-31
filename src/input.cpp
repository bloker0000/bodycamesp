#include "input.hpp"
#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cwctype>

namespace input {

State s;

namespace {

HWND window;
WNDPROC original;
std::atomic<int> wheel_acc, raw_dx, raw_dy;
std::atomic<bool> block;
bool now[256], before[256], was_blocking;

bool swallowed(UINT msg) {
    switch (msg) {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_CHAR: case WM_SYSCHAR:
        return true;
    }
    return false;
}

// menu open: no mouse or key downs reach the game, ups do so nothing sticks
LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEWHEEL) wheel_acc += GET_WHEEL_DELTA_WPARAM(wp);
    if (block && msg == WM_INPUT) {
        RAWINPUT ri;
        UINT size = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && ri.header.dwType == RIM_TYPEMOUSE) {
            if (!(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                raw_dx += ri.data.mouse.lLastX;
                raw_dy += ri.data.mouse.lLastY;
            }
            return DefWindowProcW(h, msg, wp, lp);
        }
    }
    if (block && swallowed(msg)) return 0;
    return CallWindowProcW(original, h, msg, wp, lp);
}

bool cursor_visible() {
    CURSORINFO ci{sizeof ci};
    return GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING);
}

BOOL CALLBACK find_window(HWND h, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !wcscmp(cls, L"UnrealWindow")) {
        window = h;
        return FALSE;
    }
    return TRUE;
}

}

bool install() {
    EnumWindows(find_window, 0);
    if (!window) return false;
    original = (WNDPROC)SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)proc);
    log("window %p subclassed", window);
    return original != nullptr;
}

// only safe if nobody subclassed on top of us
bool uninstall() {
    block = false;
    if (!window || !original) return true;
    if ((WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC) != proc) return false;
    SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)original);
    original = nullptr;
    return true;
}

bool installed() { return original != nullptr; }

void set_block(bool on) { block = on; }

void poll(double cw, double ch) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    s.focused = pid == GetCurrentProcessId();
    for (int vk = 1; vk < 256; vk++) {
        before[vk] = now[vk];
        now[vk] = s.focused && (GetAsyncKeyState(vk) & 0x8000);
    }
    int dx = raw_dx.exchange(0), dy = raw_dy.exchange(0);
    bool blocking = block;
    if (blocking && !was_blocking) s.mx = cw / 2, s.my = ch / 2;
    was_blocking = blocking;
    s.virtual_cursor = blocking && !cursor_visible();
    if (s.virtual_cursor) {
        double k = ch / 1080.0 * 1.1;
        s.mx = std::clamp(s.mx + dx * k, 0.0, cw - 1);
        s.my = std::clamp(s.my + dy * k, 0.0, ch - 1);
    } else {
        POINT p;
        GetCursorPos(&p);
        RECT r{};
        if (window) {
            ScreenToClient(window, &p);
            GetClientRect(window, &r);
        }
        double kx = r.right > 0 ? cw / r.right : 1, ky = r.bottom > 0 ? ch / r.bottom : 1;
        s.mx = p.x * kx;
        s.my = p.y * ky;
    }
    s.down = now[VK_LBUTTON];
    s.pressed = now[VK_LBUTTON] && !before[VK_LBUTTON];
    s.released = !now[VK_LBUTTON] && before[VK_LBUTTON];
    s.wheel = wheel_acc.exchange(0) / 120.0;
}

bool pressed(int vk) { return vk > 0 && vk < 256 && now[vk] && !before[vk]; }
bool held(int vk) { return vk > 0 && vk < 256 && now[vk]; }

int first_pressed() {
    for (int vk = 3; vk < 256; vk++)
        if (now[vk] && !before[vk] && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) return vk;
    return 0;
}

std::wstring key_name(int vk) {
    switch (vk) {
    case 0: return L"none";
    case VK_MBUTTON: return L"mouse 3";
    case VK_XBUTTON1: return L"mouse 4";
    case VK_XBUTTON2: return L"mouse 5";
    case VK_INSERT: return L"insert";
    case VK_DELETE: return L"delete";
    case VK_HOME: return L"home";
    case VK_END: return L"end";
    case VK_PRIOR: return L"page up";
    case VK_NEXT: return L"page down";
    case VK_LEFT: return L"left";
    case VK_RIGHT: return L"right";
    case VK_UP: return L"up";
    case VK_DOWN: return L"down";
    case VK_LSHIFT: return L"left shift";
    case VK_RSHIFT: return L"right shift";
    case VK_LCONTROL: return L"left ctrl";
    case VK_RCONTROL: return L"right ctrl";
    case VK_LMENU: return L"left alt";
    case VK_RMENU: return L"right alt";
    case VK_CAPITAL: return L"caps lock";
    case VK_SCROLL: return L"scroll lock";
    case VK_PAUSE: return L"pause";
    case VK_PRINT: case VK_SNAPSHOT: return L"print";
    }
    if (vk >= VK_F1 && vk <= VK_F24) return L"f" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"num " + std::to_wstring(vk - VK_NUMPAD0);
    wchar_t buf[64] = {};
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextW((LONG)(scan << 16), buf, 64)) {
        for (auto& ch : buf) ch = (wchar_t)towlower(ch);
        return buf;
    }
    return L"key " + std::to_wstring(vk);
}

}
