#include "config.hpp"
#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

namespace cfg {

Config c;

namespace {

std::wstring path;

struct Entry {
    std::string key;
    std::variant<bool*, int*, float*, FLinearColor*> value;
};

std::vector<Entry> entries() {
    std::vector<Entry> e;
    auto add = [&](std::string k, auto* v) { e.push_back({std::move(k), v}); };
    const char* groups[GroupCount] = {"enemy", "team", "zombie", "special"};
    for (int i = 0; i < GroupCount; i++) {
        auto& g = c.g[i];
        std::string p = std::string(groups[i]) + ".";
        add(p + "on", &g.on);
        add(p + "box", &g.box);
        add(p + "fill", &g.fill);
        add(p + "skeleton", &g.skeleton);
        add(p + "head", &g.head);
        add(p + "health", &g.health);
        add(p + "health_text", &g.health_text);
        add(p + "name", &g.name);
        add(p + "distance", &g.distance);
        add(p + "weapon", &g.weapon);
        add(p + "tracer", &g.tracer);
        add(p + "arrows", &g.arrows);
        add(p + "look", &g.look);
        add(p + "vis", &g.vis);
        add(p + "hid", &g.hid);
        add(p + "chams", &g.chams);
        add(p + "cham_vis", &g.cham_vis);
        add(p + "cham_hid", &g.cham_hid);
    }
    add("master", &c.master);
    add("vis_check", &c.vis_check);
    add("fade", &c.fade);
    add("max_dist", &c.max_dist);
    add("text", &c.text);
    add("chams_on", &c.chams_on);
    add("cham_alpha", &c.cham_alpha);
    add("cham_res", &c.cham_res);
    add("cham_parts", &c.cham_parts);
    add("world", &c.world);
    add("w_objectives", &c.w_objectives);
    add("w_items", &c.w_items);
    add("w_weapons", &c.w_weapons);
    add("world_dist", &c.world_dist);
    add("world_col", &c.world_col);
    add("item_col", &c.item_col);
    add("radar", &c.radar);
    add("radar_items", &c.radar_items);
    add("radar_size", &c.radar_size);
    add("radar_range", &c.radar_range);
    add("radar_alpha", &c.radar_alpha);
    add("radar_x", &c.radar_x);
    add("radar_y", &c.radar_y);
    add("crosshair", &c.crosshair);
    add("cross_bullet", &c.cross_bullet);
    add("cross_size", &c.cross_size);
    add("cross_gap", &c.cross_gap);
    add("cross_thick", &c.cross_thick);
    add("cross_col", &c.cross_col);
    add("no_lens", &c.no_lens);
    add("no_vignette", &c.no_vignette);
    add("no_grain", &c.no_grain);
    add("no_blur", &c.no_blur);
    add("counter", &c.counter);
    add("bright", &c.bright);
    add("accent", &c.accent);
    add("menu_x", &c.menu_x);
    add("menu_y", &c.menu_y);
    add("tab", &c.tab);
    add("group", &c.group);
    add("k_menu", &c.k_menu);
    add("k_master", &c.k_master);
    add("k_chams", &c.k_chams);
    add("k_radar", &c.k_radar);
    add("k_unload", &c.k_unload);
    return e;
}

void read(const char* s, bool* v) { *v = atoi(s) != 0; }
void read(const char* s, int* v) { *v = atoi(s); }
void read(const char* s, float* v) { *v = (float)atof(s); }
void read(const char* s, FLinearColor* v) { sscanf_s(s, "%f %f %f %f", &v->r, &v->g, &v->b, &v->a); }

void write(FILE* f, const char* key, const bool* v) { fprintf(f, "%s=%d\n", key, *v ? 1 : 0); }
void write(FILE* f, const char* key, const int* v) { fprintf(f, "%s=%d\n", key, *v); }
void write(FILE* f, const char* key, const float* v) { fprintf(f, "%s=%g\n", key, *v); }
void write(FILE* f, const char* key, const FLinearColor* v) { fprintf(f, "%s=%g %g %g %g\n", key, v->r, v->g, v->b, v->a); }

FLinearColor col(float r, float g, float b, float a = 1) { return {r, g, b, a}; }

}

void defaults() {
    c = {};
    c.master = true;
    FLinearColor vis[GroupCount] = {col(1, 0.36f, 0.36f), col(0.36f, 0.9f, 0.56f), col(1, 0.74f, 0.32f), col(0.82f, 0.45f, 1)};
    FLinearColor hid[GroupCount] = {col(0.95f, 0.6f, 0.28f), col(0.32f, 0.68f, 0.92f), col(0.86f, 0.5f, 0.26f), col(0.6f, 0.38f, 0.92f)};
    for (int i = 0; i < GroupCount; i++) {
        auto& g = c.g[i];
        g.on = i != Team;
        g.box = 2;
        g.skeleton = true;
        g.health = true;
        g.name = i <= Team;
        g.distance = true;
        g.weapon = i <= Team;
        g.arrows = i != Team;
        g.vis = vis[i];
        g.hid = hid[i];
        g.chams = i != Team;
        g.cham_vis = vis[i];
        g.cham_hid = hid[i];
    }
    c.vis_check = true;
    c.fade = true;
    c.max_dist = 300;
    c.text = 9.5f;
    c.cham_alpha = 0.55f;
    c.cham_res = 1;
    c.world = true;
    c.w_objectives = true;
    c.w_items = true;
    c.world_dist = 120;
    c.world_col = col(0.95f, 0.95f, 0.6f);
    c.item_col = col(0.6f, 0.9f, 1);
    c.radar_items = true;
    c.radar_size = 190;
    c.radar_range = 50;
    c.radar_alpha = 0.55f;
    c.radar_x = 40;
    c.radar_y = 40;
    c.cross_bullet = true;
    c.cross_size = 6;
    c.cross_gap = 3;
    c.cross_thick = 1.5f;
    c.cross_col = col(1, 1, 1, 0.9f);
    c.counter = true;
    c.accent = col(0.56f, 0.64f, 1);
    c.menu_x = 120;
    c.menu_y = 120;
    c.k_menu = VK_INSERT;
    c.k_master = VK_DELETE;
    c.k_chams = VK_HOME;
    c.k_radar = VK_NEXT;
    c.k_unload = VK_END;
}

void load(const wchar_t* dir) {
    defaults();
    path = std::wstring(dir) + L"bcesp.ini";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") || !f) return;
    auto list = entries();
    char line[256];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* val = eq + 1;
        for (auto& e : list) {
            if (e.key != line) continue;
            std::visit([&](auto* v) { read(val, v); }, e.value);
            n++;
            break;
        }
    }
    fclose(f);
    log("config loaded, %d values", n);
}

void save() {
    if (path.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w") || !f) return;
    for (auto& e : entries())
        std::visit([&](auto* v) { write(f, e.key.c_str(), v); }, e.value);
    fclose(f);
}

}
