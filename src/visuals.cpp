#include "visuals.hpp"
#include "config.hpp"
#include "draw.hpp"
#include "esp.hpp"
#include "input.hpp"
#include "log.hpp"
#include "menu.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

using namespace draw;
using namespace esp;
using cfg::c;

namespace visuals {
namespace {

enum ItemKind { Objective, Item, Weapon };

struct ItemClass {
    UClass* cls;
    ItemKind kind;
    std::wstring label;
};

struct WorldItem {
    UObject* actor;
    ItemKind kind;
    const std::wstring* label;
    FVector loc;
    double dist;
};

std::vector<ItemClass> item_classes;
std::vector<WorldItem> items;
int32_t scan_pos;
bool scan_done;
UClass* weapon_base;
int32_t weapon_owner = -1;
int frames_since_query;

double radar_drag_dx, radar_drag_dy;
bool radar_dragging;

UClass* gun_cls;
UFunction *origin_fn, *direction_fn;

// what GA_Fire_Base traces with
constexpr uint8_t bullet_channel = 4, impact_channel = 1;
constexpr double fire_range = 500000;

// exact classes, substring matching labeled props in deathmatch maps
struct Known {
    const char* cls;
    ItemKind kind;
};
const Known known[] = {
    {"BP_Gear_C", Item}, {"BP_WellGear_C", Item}, {"BP_RavenGear_C", Item},
    {"BP_Key_C", Item}, {"BP_CabinKey_C", Item},
    {"BP_SpecialGrave_C", Objective}, {"BP_GeneratorGateDoor_C", Objective},
};

std::wstring pretty(std::string n) {
    if (n.ends_with("_C")) n.resize(n.size() - 2);
    if (n.starts_with("BP_")) n = n.substr(3);
    std::wstring out;
    for (size_t i = 0; i < n.size(); i++) {
        char ch = n[i];
        if (ch == '_') { out += L' '; continue; }
        if (i && isupper((unsigned char)ch) && islower((unsigned char)n[i - 1])) out += L' ';
        out += (wchar_t)towlower((unsigned char)ch);
    }
    return out;
}

void scan_classes(int budget) {
    for (int32_t n = ue::object_count(); budget-- > 0 && scan_pos < n; scan_pos++) {
        auto o = ue::object_at(scan_pos);
        if (!o || !o->cls || !ue::class_chain_has(o->cls, "Class")) continue;
        auto cls = (UClass*)o;
        if (ue::name_is(o->name, "WEP_C")) weapon_base = cls, weapon_owner = ue::prop_offset(cls, "CharacterOwner");
        for (auto& k : known) {
            if (!ue::name_is(o->name, k.cls)) continue;
            item_classes.push_back({cls, k.kind, pretty(k.cls)});
            log("world class %s", k.cls);
        }
    }
    if (scan_pos >= ue::object_count()) scan_done = true;
}

void query(UObject* hud) {
    items.clear();
    auto add = [&](UClass* cls, ItemKind kind, const std::wstring* label) {
        auto all = sdk::all_actors(hud, cls);
        for (int32_t i = 0; i < all.num; i++)
            if (auto a = all.data[i]) items.push_back({a, kind, label});
    };
    std::erase_if(item_classes, [](const ItemClass& ic) { return !ue::alive(ic.cls); });
    for (auto& ic : item_classes) add(ic.cls, ic.kind, &ic.label);
    if (weapon_base && !ue::alive(weapon_base)) weapon_base = nullptr;
    if (weapon_base && c.w_weapons) {
        static std::wstring placeholder;
        size_t before = items.size();
        add(weapon_base, Weapon, &placeholder);
        items.erase(std::remove_if(items.begin() + before, items.end(), [](const WorldItem& it) {
            return weapon_owner < 0 || ue::at<UObject*>(it.actor, weapon_owner) != nullptr;
        }), items.end());
    }
}

FLinearColor group_color(const Target& t) {
    auto& g = c.g[t.group];
    auto col = t.visible || !c.vis_check ? g.vis : g.hid;
    if (c.fade) col.a *= (float)std::clamp(1.0 - t.dist / c.max_dist * 0.6, 0.35, 1.0);
    return col;
}

void crisp_box(double x0, double y0, double x1, double y1, FLinearColor col, int style) {
    double t = std::max(1.0, std::round(ui)), o = t;
    x0 = std::round(x0), y0 = std::round(y0), x1 = std::round(x1), y1 = std::round(y1);
    double w = x1 - x0, h = y1 - y0;
    auto shade = rgba(0, 0, 0, 0.75f * col.a);
    if (style == 1) {
        rect(x0 - o, y0 - o, w + 2 * o, t + 2 * o, shade);
        rect(x0 - o, y1 - t - o, w + 2 * o, t + 2 * o, shade);
        rect(x0 - o, y0 - o, t + 2 * o, h + 2 * o, shade);
        rect(x1 - t - o, y0 - o, t + 2 * o, h + 2 * o, shade);
        rect(x0, y0, w, t, col);
        rect(x0, y1 - t, w, t, col);
        rect(x0, y0 + t, t, h - 2 * t, col);
        rect(x1 - t, y0 + t, t, h - 2 * t, col);
        return;
    }
    double lx = std::max(t * 3, std::round(w * 0.28)), ly = std::max(t * 3, std::round(h * 0.2));
    struct Seg { double x, y, w, h; } segs[] = {
        {x0, y0, lx, t}, {x0, y0, t, ly}, {x1 - lx, y0, lx, t}, {x1 - t, y0, t, ly},
        {x0, y1 - t, lx, t}, {x0, y1 - ly, t, ly}, {x1 - lx, y1 - t, lx, t}, {x1 - t, y1 - ly, t, ly},
    };
    for (auto& s : segs) rect(s.x - o, s.y - o, s.w + 2 * o, s.h + 2 * o, shade);
    for (auto& s : segs) rect(s.x, s.y, s.w, s.h, col);
}

FLinearColor health_color(float f) {
    return f > 0.5f ? mix(rgba(1, 0.85f, 0.25f), rgba(0.35f, 0.95f, 0.45f), (f - 0.5f) * 2) : mix(rgba(1, 0.3f, 0.25f), rgba(1, 0.85f, 0.25f), f * 2);
}

void draw_target(const Target& t) {
    auto& g = c.g[t.group];
    if (!g.on || !t.on_screen) return;
    auto col = group_color(t);
    double x0 = t.x0, y0 = t.y0, x1 = t.x1, y1 = t.y1, w = x1 - x0, h = y1 - y0;
    double pt = c.text;

    if (g.fill) gradient(x0, y0, w, h, alpha(col, 0.03f), alpha(col, 0.2f));
    if (g.box) crisp_box(x0, y0, x1, y1, col, g.box);

    if (g.skeleton && t.has_bones) {
        double thick = std::max(1.0, 1.15 * ui);
        auto bone = c.chams_on && g.chams ? mix(col, rgba(1, 1, 1, col.a), 0.6f) : alpha(col, 0.95f);
        for (auto& l : links)
            if (t.on[l[0]] && t.on[l[1]]) line(t.pts[l[0]], t.pts[l[1]], bone, thick);
    }
    if (g.head && t.has_bones && t.on[Head]) {
        double d = std::max(1.0, (t.head - view.loc).dot(view.fwd));
        double r = std::clamp(12.0 * view.focal / d, 2 * ui, 60 * ui);
        ring(t.pts[Head].x, t.pts[Head].y, r, col, std::max(1.0, 1.1 * ui), 28);
    }
    if (g.look) {
        FVector fwd, right, up;
        axes(sdk::aim_rotation(t.actor), fwd, right, up);
        FVector2D a, b;
        if (w2s(t.head, a) && w2s(t.head + fwd * 150, b)) line(a, b, alpha(col, 0.8f), std::max(1.0, ui));
    }
    if (g.health && t.has_hp) {
        float f = std::clamp(t.hp / t.max_hp, 0.f, 1.f);
        double bw = std::max(2.0, std::round(2.5 * ui)), bx = std::round(x0 - 4 * ui - bw), o = std::max(1.0, std::round(ui));
        rect(bx - o, std::round(y0) - o, bw + 2 * o, std::round(h) + 2 * o, rgba(0, 0, 0, 0.7f * col.a));
        double fh = std::round(h * f);
        rect(bx, std::round(y0 + h - fh), bw, fh, alpha(health_color(f), col.a));
        if (g.health_text && f < 0.995f) {
            wchar_t buf[16];
            swprintf(buf, 16, L"%.0f", t.hp);
            text(buf, bx + bw / 2, y0 + h - fh - pt * ui * 0.7, pt * 0.85, rgba(1, 1, 1, col.a), Center, Shadow);
        }
    }

    double top = y0 - (pt * 1.45 + 3) * ui;
    if (g.name) {
        const wchar_t* label = t.group == cfg::Special ? L"special" : t.group == cfg::Zombie ? L"zombie" : t.name.c_str();
        text(label, (x0 + x1) / 2, top, pt, rgba(0.97f, 0.97f, 0.97f, col.a), Center, Shadow);
    }
    double below = y1 + 3 * ui;
    if (g.weapon && !t.weapon.empty()) {
        text(t.weapon.c_str(), (x0 + x1) / 2, below, pt * 0.9, rgba(0.85f, 0.86f, 0.9f, col.a), Center, Shadow);
        below += pt * 1.35 * ui;
    }
    if (g.distance) {
        wchar_t buf[16];
        swprintf(buf, 16, L"%.0fm", t.dist);
        text(buf, (x0 + x1) / 2, below, pt * 0.9, alpha(rgba(0.75f, 0.77f, 0.82f), col.a), Center, Shadow);
    }
    if (g.tracer) {
        FVector2D from{view.cx, g.tracer == 1 ? sh : g.tracer == 2 ? view.cy : 0};
        FVector2D to{(x0 + x1) / 2, g.tracer == 3 ? y0 : y1};
        line(from, to, alpha(col, 0.55f), std::max(1.0, ui));
    }
}

void draw_arrow(const Target& t) {
    auto& g = c.g[t.group];
    if (!g.on || !g.arrows || t.on_screen) return;
    auto d = t.loc - view.loc;
    double rx = d.dot(view.right), ux = d.dot(view.up);
    double ang = std::atan2(rx, d.dot(view.fwd) > 0 ? ux : -std::max(std::abs(ux), 1.0));
    double radius = std::min(sw, sh) * 0.32;
    double ax = view.cx + std::sin(ang) * radius, ay = view.cy - std::cos(ang) * radius;
    double size = std::clamp(20 - t.dist * 0.08, 11.0, 20.0) * ui;
    arrow(ax, ay, size, ang * rad2deg, group_color(t));
}

void draw_world() {
    if (!c.world) return;
    for (auto& it : items) {
        if (it.kind == Objective && !c.w_objectives) continue;
        if (it.kind == Item && !c.w_items) continue;
        if (it.kind == Weapon && !c.w_weapons) continue;
        if (it.dist > c.world_dist) continue;
        FVector2D p;
        if (!w2s(it.loc, p)) continue;
        auto col = it.kind == Objective ? c.world_col : c.item_col;
        if (c.fade) col.a *= (float)std::clamp(1.0 - it.dist / c.world_dist * 0.6, 0.4, 1.0);
        double r = 3 * ui;
        circle(p.x, p.y, r + ui, rgba(0, 0, 0, 0.6f * col.a));
        circle(p.x, p.y, r, col);
        std::wstring label = it.kind == Weapon ? pretty(ue::name(it.actor->cls)) : *it.label;
        wchar_t buf[96];
        swprintf(buf, 96, L"%ls  %.0fm", label.c_str(), it.dist);
        text(buf, p.x, p.y + r + 3 * ui, c.text * 0.9, col, Center, Shadow);
    }
}

void draw_radar() {
    if (!c.radar) return;
    double S = c.radar_size * ui, x = c.radar_x * ui, y = c.radar_y * ui;
    auto& m = input::s;
    if (menu::is_open()) {
        if (m.pressed && m.mx >= x && m.mx < x + S && m.my >= y && m.my < y + S && !menu::blocks_mouse(m.mx, m.my)) {
            radar_dragging = true;
            radar_drag_dx = m.mx - x, radar_drag_dy = m.my - y;
        }
        if (radar_dragging && m.down) {
            x = std::clamp(m.mx - radar_drag_dx, 0.0, sw - S);
            y = std::clamp(m.my - radar_drag_dy, 0.0, sh - S);
            c.radar_x = (float)(x / ui), c.radar_y = (float)(y / ui);
        } else {
            radar_dragging = false;
        }
    }
    shadow(x, y, S, S, 14 * ui, rgba(0, 0, 0, 0.3f * c.radar_alpha));
    rrect(x, y, S, S, 10 * ui, rgba(0.06f, 0.065f, 0.075f, c.radar_alpha));
    double cx = x + S / 2, cy = y + S / 2, R = S / 2 - 8 * ui;
    double sy = sin(view.rot.yaw * deg2rad), cyaw = cos(view.rot.yaw * deg2rad), range = c.radar_range * 100;
    auto place = [&](const FVector& p, double& px, double& py) {
        double dx = p.x - view.loc.x, dy = p.y - view.loc.y;
        double fwd = dx * cyaw + dy * sy, right = -dx * sy + dy * cyaw;
        double k = R / range;
        px = right * k, py = -fwd * k;
        double len = std::hypot(px, py);
        bool clamped = len > R;
        if (clamped) px *= R / len, py *= R / len;
        px += cx, py += cy;
        return clamped;
    };
    if (c.radar_items)
        for (auto& it : items) {
            double px, py;
            if (place(it.loc, px, py)) continue;
            auto col = it.kind == Objective ? c.world_col : c.item_col;
            double s = 3 * ui;
            rrect(px - s, py - s, s * 2, s * 2, ui, col);
        }
    for (auto& t : frame.targets) {
        if (!c.g[t.group].on) continue;
        double px, py;
        bool clamped = place(t.loc, px, py);
        auto col = t.visible || !c.vis_check ? c.g[t.group].vis : c.g[t.group].hid;
        double r = (t.group <= cfg::Team ? 3.6 : 3.0) * ui;
        circle(px, py, r + ui, rgba(0, 0, 0, 0.55f));
        circle(px, py, r, alpha(col, clamped ? 0.45f : 1));
    }
    arrow(cx, cy, 11 * ui, 0, rgba(1, 1, 1, 0.92f));
}

// GA_Fire_Base.FireLogic without the random spread
bool bullet_point(FVector2D& out) {
    auto pawn = frame.pawn;
    auto gun = pawn ? esp::held_item(pawn) : nullptr;
    if (!gun) return false;
    bool new_gun = gun->cls != gun_cls;
    if (new_gun) {
        gun_cls = gun->cls;
        origin_fn = ue::find_fn(gun_cls, "GetBulletSpawnOrigin");
        direction_fn = ue::find_fn(gun_cls, "GetInitialFireDirection");
    }
    if (!origin_fn || !direction_fn) return false;
    UObject* origin = nullptr;
    call(gun, origin_fn, &origin);
    if (!origin || !ue::alive(origin)) return false;

    static const FName aiming_tag = sdk::to_name(L"Character.State.Aiming");
    struct { bool aiming; FVector dir; } p{sdk::has_tag(pawn, aiming_tag)};
    static_assert(offsetof(decltype(p), dir) == 8);
    call(gun, direction_fn, &p);

    FVector start = sdk::comp_location(origin), land = start + p.dir * fire_range;
    auto hits = sdk::sphere_trace(pawn, start, land, p.aiming ? 2.f : 10.f, bullet_channel, pawn);
    for (int32_t i = 0; i < hits.num; i++) {
        auto impact = hits.data[i].impact();
        if (sdk::line_trace(pawn, impact - p.dir * 8, impact + p.dir * 40, impact_channel, pawn, land)) break;
    }
    if (new_gun) log("%s fires from %.0f %.0f %.0f along %.3f %.3f %.3f, aiming %d, %d hits", ue::name(gun).c_str(), start.x, start.y, start.z, p.dir.x, p.dir.y, p.dir.z, p.aiming, hits.num);
    return w2s_scoped(land, out);
}

void draw_crosshair() {
    if (!c.crosshair) return;
    double cx = std::round(view.cx), cy = std::round(view.cy);
    FVector2D aim;
    if (c.cross_bullet && bullet_point(aim)) cx = std::round(std::clamp(aim.x, 0.0, sw)), cy = std::round(std::clamp(aim.y, 0.0, sh));
    double s = c.cross_size * ui, gap = c.cross_gap * ui, t = std::max(1.0, std::round(c.cross_thick * ui));
    auto col = c.cross_col, shade = rgba(0, 0, 0, 0.65f * col.a);
    if (c.crosshair == 1) {
        circle(cx, cy, s / 2 + ui, shade);
        circle(cx, cy, s / 2, col);
    } else if (c.crosshair == 2) {
        double h = std::floor(t / 2);
        struct { double x, y, w, h; } arms[] = {
            {cx - gap - s, cy - h, s, t}, {cx + gap, cy - h, s, t}, {cx - h, cy - gap - s, t, s}, {cx - h, cy + gap, t, s},
        };
        for (auto& a : arms) rect(a.x - ui, a.y - ui, a.w + 2 * ui, a.h + 2 * ui, shade);
        for (auto& a : arms) rect(a.x, a.y, a.w, a.h, col);
    } else {
        ring(cx, cy, s + gap, shade, t + 2 * ui, 40);
        ring(cx, cy, s + gap, col, t, 40);
    }
}

void draw_counter() {
    if (!c.counter) return;
    int n[cfg::GroupCount] = {};
    for (auto& t : frame.targets) n[t.group]++;
    std::wstring s;
    auto add = [&](int count, const wchar_t* one, const wchar_t* many) {
        if (!count) return;
        if (!s.empty()) s += L"    ";
        s += std::to_wstring(count) + L" " + (count == 1 ? one : many);
    };
    add(n[cfg::Enemy], L"enemy", L"enemies");
    add(n[cfg::Zombie], L"zombie", L"zombies");
    add(n[cfg::Special], L"special", L"specials");
    if (s.empty()) return;
    double w = measure(s.c_str(), 9.5).x, h = 14 * ui;
    shadow(sw / 2 - w / 2, 18 * ui, w, h, 16 * ui, rgba(0, 0, 0, 0.22f));
    text(s.c_str(), sw / 2, 18 * ui, 9.5, rgba(0.95f, 0.96f, 0.98f, 0.92f), Center, Shadow);
}

}

void world_reset() {
    item_classes.clear();
    items.clear();
    scan_pos = 0;
    scan_done = false;
    weapon_base = nullptr;
    weapon_owner = -1;
    frames_since_query = 0;
    gun_cls = nullptr;
}

void world_update(UObject* hud) {
    if (!c.world && !(c.radar && c.radar_items)) return;
    if (!scan_done) scan_classes(12000);
    if (items.empty() || ++frames_since_query >= 30) {
        frames_since_query = 0;
        query(hud);
    }
    for (auto& it : items) {
        if (!ue::alive(it.actor)) { it.dist = 1e9; continue; }
        auto root = at<UObject*>(it.actor, sdk::off.actor_root);
        if (!root) { it.dist = 1e9; continue; }
        it.loc = at<FVector>(root, sdk::off.scene_loc);
        it.dist = (it.loc - view.loc).length() / 100;
    }
}

void draw() {
    draw_world();
    for (auto& t : frame.targets) draw_target(t);
    for (auto& t : frame.targets) draw_arrow(t);
    draw_radar();
    draw_crosshair();
    draw_counter();
}

}
