#include "menu.hpp"
#include "config.hpp"
#include "draw.hpp"
#include "input.hpp"
#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <initializer_list>
#include <iterator>
#include <unordered_map>

using namespace draw;
using cfg::c;
using ue::UObject;

namespace menu {
namespace {

bool open_, unload_;
float shown;
double dt;

uint32_t active, listening, popup;
int* listen_target;
bool listen_wait;
FLinearColor* popup_col;
double popup_x, popup_y, drag_dx, drag_dy;
int popup_drag;
bool popup_fresh;
float hsv[3];
bool inputs;
uint32_t salt;
std::unordered_map<uint32_t, float> anims;
std::unordered_map<double, double> text_heights;

constexpr uint32_t window_id = 1;
constexpr double window_w = 640, window_h = 460;

struct Col { double x, y, w; } col;

FLinearColor fg() { return rgba(0.9f, 0.91f, 0.93f); }
FLinearColor dim() { return rgba(0.53f, 0.55f, 0.6f); }
FLinearColor faint() { return rgba(0.34f, 0.355f, 0.39f); }
FLinearColor ctrl() { return rgba(0.135f, 0.14f, 0.157f); }
FLinearColor ctrl_hover() { return rgba(0.175f, 0.18f, 0.2f); }
FLinearColor bg() { return rgba(0.067f, 0.07f, 0.078f, 0.97f); }

double S(double v) { return v * ui; }

uint32_t id_of(const wchar_t* s) {
    uint32_t h = 2166136261u ^ salt;
    for (; *s; s++) h = (h ^ (uint32_t)*s) * 16777619u;
    return h | 0x10;
}

bool over(double x, double y, double w, double h) {
    auto& m = input::s;
    return m.mx >= x && m.mx < x + w && m.my >= y && m.my < y + h;
}

float ease(uint32_t id, float target, float speed = 16) {
    float& v = anims[id];
    v += (target - v) * (float)std::min(1.0, dt * speed);
    return v;
}

double text_h(double pt) {
    auto it = text_heights.find(pt * ui);
    if (it != text_heights.end()) return it->second;
    return text_heights[pt * ui] = measure(L"Ag", pt).y;
}

double centered(double y, double h, double pt) { return y + (h - text_h(pt)) / 2; }

void label(const wchar_t* s, double y, double h) {
    text(s, col.x, centered(y, h, 10), 10, fg());
}

void section(const wchar_t* s) {
    col.y += S(4);
    text(s, col.x, col.y, 9, faint());
    col.y += S(22);
}

void checkbox(const wchar_t* s, bool& v) {
    uint32_t id = id_of(s);
    double h = S(26), y = col.y;
    bool hov = inputs && over(col.x, y, col.w, h);
    if (hov && input::s.pressed) v = !v;
    float on = ease(id, v ? 1.f : 0.f), hv = ease(id ^ 1, hov ? 1.f : 0.f);
    label(s, y, h);
    double bs = S(14), bx = col.x + col.w - bs, by = y + (h - bs) / 2;
    rrect(bx, by, bs, bs, S(4), mix(mix(ctrl(), ctrl_hover(), hv), c.accent, on));
    col.y += h;
}

void segmented(const wchar_t* s, int& v, std::initializer_list<const wchar_t*> opts, double pt = 9.5) {
    uint32_t id = id_of(s);
    double h = S(26), y = col.y, gap = S(11);
    if (*s) label(s, y, h);
    double total = 0;
    for (auto o : opts) total += measure(o, pt).x;
    total += gap * (opts.size() - 1);
    double x = *s ? col.x + col.w - total : col.x;
    int i = 0;
    for (auto o : opts) {
        double w = measure(o, pt).x;
        bool hov = inputs && over(x - gap / 2, y, w + gap, h);
        if (hov && input::s.pressed) v = i;
        float sel = ease(id + i * 7, v == i ? 1.f : 0.f), hv = ease(id + i * 7 + 3, hov ? 1.f : 0.f);
        text(o, x, centered(y, h, pt), pt, mix(mix(dim(), fg(), hv), c.accent, sel));
        x += w + gap;
        i++;
    }
    col.y += h;
}

void slider(const wchar_t* s, float& v, float lo, float hi, const wchar_t* fmt, float shown_scale = 1) {
    uint32_t id = id_of(s);
    double h = S(40), y = col.y;
    bool hov = inputs && over(col.x, y, col.w, h);
    if (hov && input::s.pressed) active = id;
    if (active == id) {
        if (input::s.down) v = lo + (float)std::clamp((input::s.mx - col.x) / col.w, 0.0, 1.0) * (hi - lo);
        else active = 0;
    }
    if (hov && input::s.wheel != 0 && !active) v = std::clamp(v + (float)input::s.wheel * (hi - lo) / 50, lo, hi);
    float hv = ease(id ^ 1, hov || active == id ? 1.f : 0.f);
    text(s, col.x, y + S(3), 10, fg());
    wchar_t buf[32];
    swprintf(buf, 32, fmt, v * shown_scale);
    text(buf, col.x + col.w, y + S(3), 9.5, dim(), Right);
    double ty = y + S(27), th = S(4), t = (v - lo) / (hi - lo);
    rrect(col.x, ty, col.w, th, th / 2, ctrl());
    rrect(col.x, ty, std::max(th, col.w * t), th, th / 2, c.accent);
    circle(col.x + col.w * t, ty + th / 2, S(5) + S(1.5) * hv, fg());
    col.y += h;
}

void hsv_from(const FLinearColor& k) {
    float mx = std::max({k.r, k.g, k.b}), mn = std::min({k.r, k.g, k.b}), d = mx - mn;
    float hue = 0;
    if (d > 1e-5f) {
        if (mx == k.r) hue = std::fmod((k.g - k.b) / d, 6.f);
        else if (mx == k.g) hue = (k.b - k.r) / d + 2;
        else hue = (k.r - k.g) / d + 4;
        hue /= 6;
        if (hue < 0) hue += 1;
    }
    hsv[0] = hue;
    hsv[1] = mx > 0 ? d / mx : 0;
    hsv[2] = mx;
}

FLinearColor hsv_to(float hh, float ss, float vv, float a = 1) {
    float hx = hh * 6, f = hx - std::floor(hx);
    float p = vv * (1 - ss), q = vv * (1 - ss * f), t = vv * (1 - ss * (1 - f));
    switch ((int)std::floor(hx) % 6) {
    case 0: return {vv, t, p, a};
    case 1: return {q, vv, p, a};
    case 2: return {p, vv, t, a};
    case 3: return {p, q, vv, a};
    case 4: return {t, p, vv, a};
    default: return {vv, p, q, a};
    }
}

void color(const wchar_t* s, FLinearColor& v) {
    uint32_t id = id_of(s);
    double h = S(26), y = col.y;
    bool hov = inputs && over(col.x, y, col.w, h);
    float hv = ease(id ^ 1, hov ? 1.f : 0.f);
    label(s, y, h);
    double bw = S(30), bh = S(14), bx = col.x + col.w - bw, by = y + (h - bh) / 2;
    rrect(bx, by, bw, bh, S(4), mix(ctrl(), ctrl_hover(), hv));
    rrect(bx, by, bw, bh, S(4), v);
    if (hov && input::s.pressed) {
        popup = id;
        popup_col = &v;
        popup_x = bx + bw - S(204);
        popup_y = by + bh + S(6);
        popup_drag = 0;
        popup_fresh = true;
        hsv_from(v);
    }
    col.y += h;
}

void keybind(const wchar_t* s, int& vk) {
    uint32_t id = id_of(s);
    double h = S(26), y = col.y;
    bool hov = inputs && over(col.x, y, col.w, h);
    if (hov && input::s.pressed && !listening) {
        listening = id;
        listen_target = &vk;
        listen_wait = true;
    }
    float hv = ease(id ^ 1, hov ? 1.f : 0.f), on = ease(id, listening == id ? 1.f : 0.f);
    label(s, y, h);
    std::wstring name = listening == id ? L"press a key" : input::key_name(vk);
    double bw = std::max(S(72), measure(name.c_str(), 9.5).x + S(18)), bh = S(20);
    double bx = col.x + col.w - bw, by = y + (h - bh) / 2;
    rrect(bx, by, bw, bh, S(5), mix(ctrl(), ctrl_hover(), std::max(hv, on)));
    text(name.c_str(), bx + bw / 2, centered(by, bh, 9.5), 9.5, mix(dim(), c.accent, on), Center);
    col.y += h;
}

bool button(const wchar_t* s) {
    uint32_t id = id_of(s);
    double h = S(28), y = col.y + S(3);
    bool hov = inputs && over(col.x, y, col.w, h);
    float hv = ease(id ^ 1, hov ? 1.f : 0.f);
    rrect(col.x, y, col.w, h, S(6), mix(ctrl(), ctrl_hover(), hv));
    text(s, col.x + col.w / 2, centered(y, h, 10), 10, fg(), Center);
    col.y += h + S(6);
    return hov && input::s.pressed;
}

void note(const wchar_t* s) {
    text(s, col.x, col.y + S(4), 9, faint());
    col.y += S(22);
}

void picker() {
    if (!popup) return;
    double pw = S(204), pad = S(12), svw = pw - pad * 2, svh = S(118), barh = S(10);
    double ph = pad + svh + S(12) + barh + S(10) + barh + S(9) + text_h(9) + pad;
    double px = std::clamp(popup_x, 0.0, sw - pw), py = std::clamp(popup_y, 0.0, sh - ph);
    auto& m = input::s;
    double sx = px + pad, sy = py + pad;
    double hy = sy + svh + S(12);
    double ay = hy + barh + S(10);

    if (m.pressed && !popup_fresh) {
        if (over(sx, sy, svw, svh)) popup_drag = 1;
        else if (over(sx, hy - S(3), svw, barh + S(6))) popup_drag = 2;
        else if (over(sx, ay - S(3), svw, barh + S(6))) popup_drag = 3;
        else if (!over(px, py, pw, ph)) { popup = 0; return; }
    }
    popup_fresh = false;
    if (!m.down) popup_drag = 0;
    double fx = std::clamp((m.mx - sx) / svw, 0.0, 1.0), fy = std::clamp((m.my - sy) / svh, 0.0, 1.0);
    if (popup_drag == 1) hsv[1] = (float)fx, hsv[2] = 1 - (float)fy;
    if (popup_drag == 2) hsv[0] = (float)std::min(fx, 0.9999);
    if (popup_drag == 3) popup_col->a = (float)fx;
    if (popup_drag) {
        float a = popup_col->a;
        *popup_col = hsv_to(hsv[0], hsv[1], hsv[2], a);
    }

    shadow(px, py, pw, ph, S(18), rgba(0, 0, 0, 0.5f));
    rrect(px, py, pw, ph, S(8), rgba(0.095f, 0.1f, 0.11f, 1));

    std::vector<sdk::UVTri> mesh;
    auto quad = [&](double x0, double y0, double x1, double y1, FLinearColor a, FLinearColor b, FLinearColor cc, FLinearColor d) {
        mesh.push_back({{x0, y0}, {0, 0}, a, {x1, y0}, {1, 0}, b, {x1, y1}, {1, 1}, cc});
        mesh.push_back({{x0, y0}, {0, 0}, a, {x1, y1}, {1, 1}, cc, {x0, y1}, {0, 1}, d});
    };
    const int gx = 10, gy = 8;
    for (int j = 0; j < gy; j++)
        for (int i = 0; i < gx; i++) {
            auto sv = [&](int a, int b) { return hsv_to(hsv[0], (float)a / gx, 1 - (float)b / gy); };
            quad(sx + svw * i / gx, sy + svh * j / gy, sx + svw * (i + 1) / gx, sy + svh * (j + 1) / gy, sv(i, j), sv(i + 1, j), sv(i + 1, j + 1), sv(i, j + 1));
        }
    for (int i = 0; i < 6; i++) {
        auto a = hsv_to(i / 6.f, 1, 1), b = hsv_to((i + 1) / 6.f - 1e-4f, 1, 1);
        quad(sx + svw * i / 6, hy, sx + svw * (i + 1) / 6, hy + barh, a, b, b, a);
    }
    auto base = rgba(0.13f, 0.135f, 0.15f), solid = *popup_col;
    solid.a = 1;
    quad(sx, ay, sx + svw, ay + barh, base, solid, solid, base);
    draw::tris(mesh);

    double mx = sx + svw * hsv[1], my = sy + svh * (1 - hsv[2]);
    circle(mx, my, S(5.5), rgba(0, 0, 0, 0.6f));
    circle(mx, my, S(4), rgba(1, 1, 1));
    rrect(sx + svw * hsv[0] - S(2), hy - S(2), S(4), barh + S(4), S(2), rgba(1, 1, 1));
    rrect(sx + svw * popup_col->a - S(2), ay - S(2), S(4), barh + S(4), S(2), rgba(1, 1, 1));

    wchar_t buf[32];
    auto k = *popup_col;
    swprintf(buf, 32, L"%02x%02x%02x  %d%%", (int)std::lround(k.r * 255), (int)std::lround(k.g * 255), (int)std::lround(k.b * 255), (int)std::lround(k.a * 100));
    text(buf, sx, ay + barh + S(9), 9, dim());
}

void group_tabs() {
    salt = 0x51ed;
    segmented(L"", c.group, {L"enemies", L"team", L"zombies", L"specials"}, 11);
    col.y += S(8);
}

void tab_esp(Col c1, Col c2) {
    auto& g = c.g[c.group];
    salt = 0x100 + c.group;
    col = c1;
    checkbox(L"enabled", g.on);
    segmented(L"box", g.box, {L"off", L"full", L"corners"});
    checkbox(L"fill", g.fill);
    checkbox(L"skeleton", g.skeleton);
    checkbox(L"head", g.head);
    checkbox(L"health bar", g.health);
    checkbox(L"health number", g.health_text);
    checkbox(L"look direction", g.look);
    col = c2;
    checkbox(L"name", g.name);
    checkbox(L"distance", g.distance);
    checkbox(L"weapon", g.weapon);
    segmented(L"tracer", g.tracer, {L"off", L"low", L"mid", L"top"});
    checkbox(L"offscreen arrows", g.arrows);
    color(L"visible", g.vis);
    color(L"behind cover", g.hid);
}

void tab_chams(Col c1, Col c2) {
    auto& g = c.g[c.group];
    salt = 0x200 + c.group;
    col = c1;
    checkbox(L"chams", g.chams);
    color(L"visible", g.cham_vis);
    color(L"behind cover", g.cham_hid);
    salt = 0x2ff;
    col = c2;
    checkbox(L"chams enabled", c.chams_on);
    slider(L"opacity", c.cham_alpha, 0.05f, 1, L"%.0f%%", 100);
    segmented(L"quality", c.cham_res, {L"full", L"half", L"low"});
    checkbox(L"visible parts only", c.cham_parts);
}

void tab_world(Col c1, Col c2) {
    salt = 0x300;
    col = c1;
    checkbox(L"enabled", c.world);
    checkbox(L"objectives", c.w_objectives);
    checkbox(L"items", c.w_items);
    checkbox(L"weapons", c.w_weapons);
    slider(L"max distance", c.world_dist, 10, 300, L"%.0fm");
    col = c2;
    color(L"objectives", c.world_col);
    color(L"items", c.item_col);
}

void tab_radar(Col c1, Col c2) {
    salt = 0x400;
    col = c1;
    checkbox(L"radar", c.radar);
    slider(L"size", c.radar_size, 120, 380, L"%.0f");
    slider(L"range", c.radar_range, 10, 200, L"%.0fm");
    slider(L"background", c.radar_alpha, 0, 1, L"%.0f%%", 100);
    col = c2;
    checkbox(L"show items", c.radar_items);
    note(L"drag the radar to move it");
}

void tab_view(Col c1, Col c2) {
    salt = 0x500;
    col = c1;
    section(L"crosshair");
    segmented(L"style", c.crosshair, {L"off", L"dot", L"cross", L"circle"});
    checkbox(L"follow the bullet", c.cross_bullet);
    slider(L"size", c.cross_size, 1, 24, L"%.0f");
    slider(L"gap", c.cross_gap, 0, 16, L"%.0f");
    slider(L"thickness", c.cross_thick, 0.5f, 6, L"%.1f");
    color(L"color", c.cross_col);
    col = c2;
    section(L"screen");
    checkbox(L"no lens", c.no_lens);
    slider(L"brightness", c.bright, 0, 4, L"+%.1f");
    checkbox(L"no vignette", c.no_vignette);
    checkbox(L"no film grain", c.no_grain);
    checkbox(L"no motion blur", c.no_blur);
    checkbox(L"target counter", c.counter);
}

void tab_settings(Col c1, Col c2) {
    salt = 0x600;
    col = c1;
    section(L"keybinds");
    keybind(L"menu", c.k_menu);
    keybind(L"visuals", c.k_master);
    keybind(L"chams", c.k_chams);
    keybind(L"radar", c.k_radar);
    keybind(L"unload", c.k_unload);
    note(L"backspace clears, esc cancels");
    col = c2;
    section(L"general");
    checkbox(L"visibility check", c.vis_check);
    checkbox(L"distance fade", c.fade);
    slider(L"max distance", c.max_dist, 25, 500, L"%.0fm");
    slider(L"text size", c.text, 7, 14, L"%.1f");
    color(L"accent", c.accent);
    if (button(L"reset to defaults")) {
        float mx = c.menu_x, my = c.menu_y;
        int tab = c.tab;
        cfg::defaults();
        c.menu_x = mx, c.menu_y = my, c.tab = tab;
    }
    if (button(L"unload")) unload_ = true;
}

void window() {
    double W = S(window_w), H = S(window_h), side = S(156);
    double wx = c.menu_x * ui, wy = c.menu_y * ui;
    auto& m = input::s;

    bool in_side = over(wx, wy, side, H);
    static const wchar_t* tabs[] = {L"esp", L"chams", L"world", L"radar", L"view", L"settings"};
    const int tab_count = (int)std::size(tabs);
    double ty0 = wy + S(84), th = S(32);
    int hovered_tab = -1;
    for (int i = 0; i < tab_count; i++)
        if (inputs && over(wx + S(10), ty0 + th * i, side - S(20), th)) hovered_tab = i;
    if (inputs && m.pressed && hovered_tab >= 0) c.tab = hovered_tab;
    else if (inputs && m.pressed && in_side) {
        active = window_id;
        drag_dx = m.mx - wx, drag_dy = m.my - wy;
    }
    if (active == window_id) {
        if (m.down) {
            wx = std::clamp(m.mx - drag_dx, 0.0, sw - W);
            wy = std::clamp(m.my - drag_dy, 0.0, sh - H);
            c.menu_x = (float)(wx / ui), c.menu_y = (float)(wy / ui);
        } else {
            active = 0;
        }
    }

    shadow(wx, wy, W, H, S(30), rgba(0, 0, 0, 0.5f));
    rrect(wx, wy, W, H, S(12), bg());

    text(L"bodycam", wx + S(24), wy + S(24), 12.5, fg(), Left, Plain, Bold);
    text(L"visuals", wx + S(24), wy + S(46), 9.5, faint());

    for (int i = 0; i < tab_count; i++) {
        double y = ty0 + th * i;
        float sel = ease(0x700 + i, c.tab == i ? 1.f : 0.f), hv = ease(0x710 + i, hovered_tab == i ? 1.f : 0.f);
        float bg_a = 0.05f * sel + 0.025f * hv * (1 - sel);
        if (bg_a > 0.003f) rrect(wx + S(12), y + S(2), side - S(24), th - S(4), S(7), rgba(1, 1, 1, bg_a));
        text(tabs[i], wx + S(26), centered(y, th, 10.5), 10.5, mix(mix(dim(), fg(), hv), c.accent, sel));
    }
    std::wstring foot = input::key_name(c.k_menu) + L" to close";
    text(foot.c_str(), wx + S(24), wy + H - S(34), 9, faint());
    double cx = wx + side + S(14), cy = wy + S(24), cw = W - side - S(14) - S(28);
    col = {cx, cy, cw};
    bool groups = c.tab == 0 || c.tab == 1;
    if (groups) group_tabs();
    double colw = (cw - S(30)) / 2;
    Col c1{cx, col.y, colw}, c2{cx + colw + S(30), col.y, colw};
    switch (c.tab) {
    case 0: tab_esp(c1, c2); break;
    case 1: tab_chams(c1, c2); break;
    case 2: tab_world(c1, c2); break;
    case 3: tab_radar(c1, c2); break;
    case 4: tab_view(c1, c2); break;
    default: tab_settings(c1, c2); break;
    }
}

void update_listening() {
    if (!listening) return;
    if (listen_wait) {
        if (!input::held(VK_LBUTTON)) listen_wait = false;
        return;
    }
    int vk = input::first_pressed();
    if (!vk) return;
    if (vk == VK_ESCAPE) listening = 0;
    else if (vk == VK_BACK) *listen_target = 0, listening = 0;
    else *listen_target = vk, listening = 0;
}

void cursor() {
    if (!input::s.virtual_cursor) return;
    double x = input::s.mx, y = input::s.my;
    circle(x + S(1), y + S(1.5), S(5.5), rgba(0, 0, 0, 0.45f));
    circle(x, y, S(4.5), rgba(1, 1, 1));
    circle(x, y, S(2.5), c.accent);
}

}

bool is_open() { return open_; }
bool visible() { return open_ || shown >= 0.01f; }
bool capturing() { return listening != 0; }

bool blocks_mouse(double x, double y) {
    if (!open_) return true;
    if (popup || active) return true;
    double wx = c.menu_x * ui, wy = c.menu_y * ui;
    return x >= wx && x < wx + S(window_w) && y >= wy && y < wy + S(window_h);
}
bool unload_requested() { return unload_; }

void toggle() {
    if (open_) return close();
    open_ = true;
    input::set_block(true);
}

void close() {
    if (!open_) return;
    open_ = false;
    active = listening = popup = 0;
    input::set_block(false);
    cfg::save();
}

void frame(double frame_dt) {
    dt = frame_dt;
    shown += ((open_ ? 1.f : 0.f) - shown) * (float)std::min(1.0, dt * 18);
    if (!open_ && shown < 0.01f) return;
    update_listening();
    inputs = open_ && !popup && !listening;
    fade = shown;
    window();
    fade = shown;
    picker();
    fade = 1;
    if (open_) cursor();
}

}
