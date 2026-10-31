#include "esp.hpp"
#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <unordered_map>

using namespace sdk;

namespace esp {

const Bone links[15][2] = {
    {Head, Neck}, {Neck, Chest}, {Chest, Pelvis},
    {Neck, LUp}, {LUp, LFore}, {LFore, LHand}, {Neck, RUp}, {RUp, RFore}, {RFore, RHand},
    {Pelvis, LThigh}, {LThigh, LCalf}, {LCalf, LFoot}, {Pelvis, RThigh}, {RThigh, RCalf}, {RCalf, RFoot},
};

View view;
Frame frame;

namespace {

const char* aliases[BoneCount] = {
    "head", "neck_01|neck", "spine_03|spine2|spine_02", "pelvis|hips",
    "upperarm_l|leftarm", "lowerarm_l|leftforearm", "hand_l|lefthand",
    "upperarm_r|rightarm", "lowerarm_r|rightforearm", "hand_r|righthand",
    "thigh_l|leftupleg", "calf_l|leftleg", "foot_l|leftfoot",
    "thigh_r|rightupleg", "calf_r|rightleg", "foot_r|rightfoot",
};

enum class Kind { Skip, Human, Zombie, Mob };
struct ClassInfo {
    Kind kind;
    BoolProp special;
};
std::unordered_map<UClass*, ClassInfo> kinds;

struct Bones {
    int num = -1;
    bool ok = false;
    FName names[BoneCount];
};
std::unordered_map<UObject*, Bones> bone_cache;
std::unordered_map<UObject*, UObject*> inventories;
std::unordered_map<UClass*, std::wstring> weapon_names;

UClass* inventory_cls;
int32_t inv_hand = -1;
bool inventory_looked;
bool lens_active, lens_looked;
double lens_i;
UObject* lens_mpc;
FName lens_param;

struct {
    bool on;
    UObject* comp;
    FVector loc, fwd, right, up;
    double tan_half, edge;
} sc;

// lit picture and scope tube radius in units of sc.edge, measured on the 4x
constexpr double scope_lit = 0.66, scope_tube = 0.82;

bool camera_lens(UObject* pawn) {
    static UObject* cached;
    static std::vector<UObject*> cams;
    if (pawn != cached) {
        cached = pawn;
        cams.clear();
        if (pawn) {
            auto list = components(pawn, cls.camera);
            for (int32_t i = 0; i < list.num; i++) cams.push_back(list.data[i]);
        }
    }
    for (auto cam : cams) {
        if (!alive(cam)) continue;
        auto& list = blendables(cam);
        for (int32_t k = 0; k < list.num; k++)
            if (list.data[k].object && list.data[k].weight > 0 && name_is(list.data[k].object->name, "M_PP_BodycamMaster")) return true;
    }
    return false;
}

ClassInfo& classify(UClass* c) {
    if (auto it = kinds.find(c); it != kinds.end()) return it->second;
    ClassInfo ci{Kind::Skip};
    if (class_chain_has(c, "Zombie_C")) {
        ci.kind = Kind::Zombie;
        ci.special = bool_prop(c, "SpecialZombie");
    } else if (class_chain_has(c, "BP_LoadoutShowcaseCharacter_C")) {
        ci.kind = Kind::Skip;
    } else if (class_chain_has(c, "BodycamWeaponCharacter")) {
        ci.kind = Kind::Human;
    } else if (class_chain_has(c, "HorrorMob_C")) {
        ci.kind = Kind::Mob;
    }
    log("class %s -> %d", name(c).c_str(), (int)ci.kind);
    return kinds[c] = ci;
}

std::string bone_key(std::string s) {
    for (auto& ch : s) ch = (char)tolower((unsigned char)ch);
    if (auto p = s.rfind(':'); p != s.npos) s = s.substr(p + 1);
    return s;
}

Bones& bones_for(UObject* mesh) {
    int n = num_bones(mesh);
    auto& b = bone_cache[mesh];
    if (b.num == n) return b;
    b = {};
    b.num = n;
    std::vector<std::pair<std::string, FName>> all;
    for (int i = 0; i < n; i++) {
        FName f = bone_name(mesh, i);
        all.push_back({bone_key(name(f)), f});
    }
    b.ok = n > 0;
    for (int k = 0; k < BoneCount; k++) {
        bool found = false;
        std::string_view list = aliases[k];
        while (!found && !list.empty()) {
            auto bar = list.find('|');
            auto alias = list.substr(0, bar);
            list = bar == list.npos ? std::string_view{} : list.substr(bar + 1);
            for (auto& [s, f] : all)
                if (s == alias) { b.names[k] = f; found = true; break; }
        }
        b.ok &= found;
    }
    if (!b.ok && n > 0) {
        std::string dump;
        for (auto& [s, f] : all) dump += s + " ";
        log("skeleton not matched (%d bones): %s", n, dump.c_str());
    }
    return b;
}

int team_of(UObject* ps) {
    return ps && is_a(ps, cls.bc_ps) ? at<int32_t>(ps, off.ps_team) : -1;
}

std::wstring fstring(const FString& s, int max) {
    if (!s.data || s.num <= 1) return {};
    return std::wstring(s.data, std::min(s.num - 1, max));
}

std::wstring wide_lower(std::string_view s) {
    std::wstring w;
    for (char ch : s) w += (wchar_t)towlower((unsigned char)ch);
    return w;
}

const std::wstring& weapon_name(UClass* c) {
    auto it = weapon_names.find(c);
    if (it != weapon_names.end()) return it->second;
    auto n = name(c);
    if (n.size() > 2 && n.ends_with("_C")) n.resize(n.size() - 2);
    if (n.starts_with("BP_")) n = n.substr(3);
    return weapon_names[c] = wide_lower(n);
}

std::wstring held_weapon(UObject* actor) {
    auto item = held_item(actor);
    if (!item) return {};
    static int32_t def = -2;
    if (def == -2) def = prop_offset(item->cls, "LoadoutItemDefinition");
    // the class is just WEP_C for players, the loadout row has the real gun
    if (def >= 0) {
        auto row = at<FName>(item, def + 0x18);
        if (row.idx) {
            auto n = name(row);
            return wide_lower(n.substr(0, n.find_first_of("_ ")));
        }
    }
    return weapon_name(item->cls);
}

void setup_view(UObject* cam, int w, int h) {
    auto pov = (uint8_t*)cam + off.cam_cache + off.cache_pov;
    view.loc = at<FVector>(pov, off.pov_loc);
    view.rot = at<FRotator>(pov, off.pov_rot);
    view.fov = at<float>(pov, off.pov_fov);
    axes(view.rot, view.fwd, view.right, view.up);
    view.cx = w / 2.0;
    view.cy = h / 2.0;
    view.focal = view.cx / tan(view.fov * deg2rad / 2.0);
}

bool needs_bones(const cfg::GroupCfg& g) {
    return g.skeleton || g.head || g.box || g.chams || g.name || g.distance || g.weapon || g.health || g.tracer || g.fill;
}

// d is camera relative, min_z throws out what's behind it
bool to_screen(const FVector& d, double min_z, FVector2D& out) {
    double z = d.dot(view.fwd);
    if (z < min_z) return false;
    out = distort({view.cx + d.dot(view.right) * view.focal / z, view.cy - d.dot(view.up) * view.focal / z});
    return true;
}

// angle off the scope axis grows linearly up to sc.edge at the target edge
bool scope_dir(double nx, double ny, FVector2D& out) {
    double u = std::hypot(nx, ny), a = u * sc.edge;
    double k = u > 1e-9 ? sin(a) / u : sc.edge;
    return to_screen(sc.fwd * cos(a) + (sc.right * nx + sc.up * ny) * k, 0.01, out);
}

bool scope_point(const FVector& p, FVector2D& out, bool& inside) {
    auto d = p - sc.loc;
    double z = d.dot(sc.fwd);
    inside = false;
    if (z < 1.0) return false;
    double nx = d.dot(sc.right) / z / sc.tan_half, ny = d.dot(sc.up) / z / sc.tan_half;
    inside = std::hypot(nx, ny) <= scope_lit;
    return scope_dir(nx, ny, out);
}

bool behind_scope(const FVector& p) {
    auto d = p - view.loc;
    double len = d.length();
    return len > 1 && d.dot(sc.fwd) > len * cos(scope_tube * sc.edge);
}

void update_scope() {
    sc.on = false;
    auto gun = held_item(frame.pawn);
    static UObject* cached;
    static std::vector<UObject*> found;
    static int refresh;
    if (!gun) return;
    if (gun != cached || refresh-- <= 0) {
        cached = gun;
        refresh = 60;
        found.clear();
        std::vector<UObject*> owners{gun};
        auto list = attached(gun);
        for (int32_t i = 0; i < list.num; i++) owners.push_back(list.data[i]);
        for (auto o : owners) {
            auto comps = components(o, cls.scope);
            for (int32_t i = 0; i < comps.num; i++) found.push_back(comps.data[i]);
        }
    }
    for (auto s : found) {
        if (!alive(s) || !scope_active(s)) continue;
        sc.comp = s;
        sc.loc = comp_location(s);
        axes(comp_rotation(s), sc.fwd, sc.right, sc.up);
        sc.tan_half = tan(at<float>(s, off.cap_fov) * deg2rad / 2);
        sc.edge = atan(at<float>(s, off.scope_mag) * sc.tan_half);
        sc.on = sc.tan_half > 0 && sc.edge > 0;
        return;
    }
}

void update_lens(UObject* hud) {
    lens_active = !cfg::c.no_lens && camera_lens(frame.pawn);
    if (!lens_active) return;
    if (!lens_looked) {
        lens_looked = true;
        lens_mpc = find("/Game/PostProcess/Materials/MPC_PostProcessMaterialSettings.MPC_PostProcessMaterialSettings");
        lens_param = to_name(L"LensDistortionIntensity");
        log("lens collection %p", lens_mpc);
    }
    lens_i = lens_mpc && alive(lens_mpc) ? mpc_get(hud, lens_mpc, lens_param) : 0.33;
    lens_active = lens_i > 0.001;
}

void read_health(Target& t) {
    if (!is_a(t.actor, cls.bc_char)) return;
    auto set = at<UObject*>(t.actor, off.bc_set);
    if (!set) return;
    t.hp = at<float>(set, off.set_hp + off.attr_cur);
    t.max_hp = at<float>(set, off.set_maxhp + off.attr_cur);
    t.has_hp = t.max_hp > 0;
}

// a body left behind (player flying a drone) has no state but remembers it
UObject* player_state(UObject* actor) {
    auto ps = at<UObject*>(actor, off.pawn_ps);
    if (!ps && is_a(actor, cls.bc_char)) ps = weak_get((uint8_t*)actor + off.bc_cached_ps);
    return ps;
}

void read_bones(Target& t, FVector* world) {
    auto& b = bones_for(t.mesh);
    if (!b.ok) return;
    t.has_bones = true;
    for (int k = 0; k < BoneCount; k++) world[k] = socket(t.mesh, b.names[k]);
    t.head = world[Head];
}

bool project(const Target& t, const FVector& p, FVector2D& out) {
    bool inside;
    return t.in_scope ? scope_point(p, out, inside) : w2s(p, out);
}

// around the projected bones, capsule height without a skeleton. false if nothing projected
bool fit_box(Target& t, const FVector* world, double half) {
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    bool any = false;
    if (t.has_bones)
        for (int k = 0; k < BoneCount; k++) {
            if (!(t.on[k] = project(t, world[k], t.pts[k]))) continue;
            x0 = std::min(x0, t.pts[k].x), x1 = std::max(x1, t.pts[k].x);
            y0 = std::min(y0, t.pts[k].y), y1 = std::max(y1, t.pts[k].y);
            any = true;
        }
    if (any) {
        double bh = y1 - y0, bw = x1 - x0;
        y0 -= bh * 0.12 + 2;
        y1 += bh * 0.03;
        double pad = std::max(bh * 0.42 - bw, 0.0) / 2 + 2;
        x0 -= pad, x1 += pad;
    } else {
        FVector2D top, bot;
        if (project(t, {t.loc.x, t.loc.y, t.loc.z + half}, top) && project(t, {t.loc.x, t.loc.y, t.loc.z - half}, bot)) {
            double bh = bot.y - top.y;
            x0 = top.x - bh * 0.25, x1 = top.x + bh * 0.25, y0 = top.y, y1 = bot.y;
            any = true;
        }
    }
    t.x0 = x0, t.y0 = y0, t.x1 = x1, t.y1 = y1;
    return any;
}

}

void axes(const FRotator& r, FVector& fwd, FVector& right, FVector& up) {
    double sp = sin(r.pitch * deg2rad), cp = cos(r.pitch * deg2rad);
    double sy = sin(r.yaw * deg2rad), cy = cos(r.yaw * deg2rad);
    double sr = sin(r.roll * deg2rad), cr = cos(r.roll * deg2rad);
    fwd = {cp * cy, cp * sy, sp};
    right = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    up = {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
}

UObject* held_item(UObject* actor) {
    if (!inventory_looked) {
        inventory_looked = true;
        for (int32_t i = 0, n = object_count(); i < n && !inventory_cls; i++) {
            auto o = object_at(i);
            if (o && name_is(o->name, "Inventory_C") && class_chain_has(o->cls, "Class")) inventory_cls = (UClass*)o;
        }
        if (inventory_cls) inv_hand = prop_offset(inventory_cls, "HandleItem");
        log("inventory class %p, HandleItem at %#x", inventory_cls, inv_hand);
    }
    if (inventory_cls && !alive(inventory_cls)) inventory_cls = nullptr, inventory_looked = false;
    if (!actor) return nullptr;

    // bots carry the old blueprint inventory
    if (inventory_cls && inv_hand >= 0) {
        auto it = inventories.find(actor);
        if (it == inventories.end()) {
            auto list = components(actor, inventory_cls);
            it = inventories.emplace(actor, list.num > 0 ? list.data[0] : nullptr).first;
        }
        if (it->second && (!alive(it->second) || it->second->outer != actor)) {
            inventories.erase(it);
            return nullptr;
        }
        if (it->second) {
            auto item = at<UObject*>(it->second, inv_hand);
            return item && alive(item) && class_chain_has(item->cls, "WEP_C") ? item : nullptr;
        }
    }

    // players dont, the gun in hand is the visible WEP_C attached to them
    static BoolProp visible;
    auto list = attached(actor);
    for (int32_t i = 0; i < list.num; i++) {
        auto a = list.data[i];
        if (!a || !class_chain_has(a->cls, "WEP_C")) continue;
        if (visible.offset < 0) visible = bool_prop(a->cls, "bIsVisible");
        if (visible.offset < 0 || visible.get(a)) return a;
    }
    return nullptr;
}

bool lens_on() { return lens_active; }
double lens_strength() { return lens_i; }

// the lens samples the plain frame at R * (p0 + p1 R^2 + p2 R^4), this goes the other way
FVector2D distort(FVector2D p) {
    if (!lens_active) return p;
    double I = lens_i;
    double p0 = 1 - 0.54636 * I + 0.01177 * I * I;
    double p1 = 0.49952 * I - 0.02881 * I * I;
    double p2 = -0.24146 * I + 0.12043 * I * I;
    double x = (p.x - view.cx) / view.cx, y = (p.y - view.cy) / view.cy;
    double r = std::sqrt(x * x + y * y);
    if (r < 1e-6) return p;
    double R = r / p0;
    for (int i = 0; i < 8; i++) {
        double r2 = R * R, g = R * (p0 + p1 * r2 + p2 * r2 * r2) - r, dg = p0 + 3 * p1 * r2 + 5 * p2 * r2 * r2;
        if (dg < 0.1 || R > 1.6) break;
        R -= g / dg;
    }
    double k = std::clamp(R, 0.0, 1.6) / r;
    return {view.cx + x * k * view.cx, view.cy + y * k * view.cy};
}

bool w2s(const FVector& p, FVector2D& out) {
    return to_screen(p - view.loc, 1.0, out);
}

// through the scope picture when the point is inside it
bool w2s_scoped(const FVector& p, FVector2D& out) {
    bool inside = false;
    if (sc.on && scope_point(p, out, inside) && inside) return true;
    return w2s(p, out);
}

// ctrl+f12 grabs the scope picture too
void dump_scope(UObject* hud, const wchar_t* dir) {
    if (!sc.on || !alive(sc.comp)) return;
    auto rt = at<UObject*>(sc.comp, off.cap_target);
    if (!rt) return;
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    auto ms = ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000 - 11644473600000ull;
    export_rt(hud, rt, dir, L"scope_rt.hdr");
    log("scope picture written at %llu, fov %.3f, edge %.4f, lens %.3f", ms, 2 * atan(sc.tan_half) * rad2deg, sc.edge, lens_on() ? lens_i : 0.0);
}

void reset() {
    kinds.clear();
    bone_cache.clear();
    inventories.clear();
    weapon_names.clear();
    inventory_cls = nullptr;
    inventory_looked = false;
    lens_looked = false;
    lens_mpc = nullptr;
    frame.targets.clear();
}

bool update(UObject* hud, int w, int h) {
    auto& c = cfg::c;
    frame.targets.clear();
    frame.hud = hud;
    auto pc = at<UObject*>(hud, off.hud_owner);
    if (!pc) return false;
    auto cam = at<UObject*>(pc, off.pc_cam);
    if (!cam) return false;
    setup_view(cam, w, h);
    frame.pawn = at<UObject*>(pc, off.pc_pawn);
    auto own_state = at<UObject*>(pc, off.ctrl_ps);
    int own_team = team_of(own_state);
    update_lens(hud);
    update_scope();

    auto all = all_actors(hud, cls.character);
    for (int32_t i = 0; i < all.num; i++) {
        auto a = all.data[i];
        if (!a || a == frame.pawn) continue;
        auto& ci = classify(a->cls);
        if (ci.kind == Kind::Skip) continue;

        Target t{};
        t.actor = a;
        read_health(t);
        if (t.has_hp && t.hp <= 0) continue;

        auto ps = player_state(a);
        if (ps && ps == own_state) continue;
        if (ci.kind == Kind::Human) {
            bool mate = own_team >= 0 && team_of(ps) == own_team;
            t.group = mate ? cfg::Team : cfg::Enemy;
        } else {
            t.group = ci.kind == Kind::Zombie && ci.special.get(a) ? cfg::Special : cfg::Zombie;
        }
        auto& g = c.g[t.group];
        if (!g.on && !g.chams && !c.radar) continue;

        auto root = at<UObject*>(a, off.actor_root);
        if (!root) continue;
        t.loc = at<FVector>(root, off.scene_loc);
        t.dist = (t.loc - view.loc).length() / 100.0;
        if (t.dist > c.max_dist) continue;

        t.mesh = at<UObject*>(a, off.char_mesh);
        auto capsule = at<UObject*>(a, off.char_capsule);
        double half = capsule ? at<float>(capsule, off.capsule_half) : 90.0;
        t.head = {t.loc.x, t.loc.y, t.loc.z + half * 0.8};

        FVector world[BoneCount];
        if (t.mesh && (g.on || g.chams) && needs_bones(g)) read_bones(t, world);

        // inside the scope picture everything goes through the scope camera
        bool hidden = false;
        if (sc.on) {
            FVector2D unused;
            scope_point(t.loc, unused, t.in_scope);
            hidden = !t.in_scope && behind_scope(t.loc);
        }

        bool any = fit_box(t, world, half);
        t.on_screen = !hidden && any && t.x1 > 0 && t.y1 > 0 && t.x0 < w && t.y0 < h;

        t.visible = !c.vis_check || line_of_sight(pc, a, view.loc);
        if (ci.kind == Kind::Human && g.name) {
            if (ps) t.name = fstring(at<FString>(ps, off.ps_name), 24);
            if (t.name.empty()) t.name = L"bot";
        }
        if (ci.kind == Kind::Human && g.weapon) t.weapon = held_weapon(a);
        frame.targets.push_back(std::move(t));
    }

    std::sort(frame.targets.begin(), frame.targets.end(), [](const Target& a, const Target& b) { return a.dist > b.dist; });
    return true;
}

}
