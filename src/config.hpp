#pragma once
#include "ue.hpp"

namespace cfg {
using ue::FLinearColor;

enum Group { Enemy, Team, Zombie, Special, GroupCount };

struct GroupCfg {
    bool on, fill, skeleton, head, health, health_text, name, distance, weapon, arrows, look;
    int box, tracer;
    FLinearColor vis, hid;
    bool chams;
    FLinearColor cham_vis, cham_hid;
};

struct Config {
    bool master;
    GroupCfg g[GroupCount];
    bool vis_check, fade;
    float max_dist, text;

    bool chams_on, cham_parts;
    float cham_alpha;
    int cham_res;

    bool world, w_objectives, w_items, w_weapons;
    float world_dist;
    FLinearColor world_col, item_col;

    bool radar, radar_items;
    float radar_size, radar_range, radar_alpha, radar_x, radar_y;

    int crosshair;
    bool cross_bullet;
    float cross_size, cross_gap, cross_thick;
    FLinearColor cross_col;
    bool no_lens, no_vignette, no_grain, no_blur, counter;
    float bright;

    FLinearColor accent;
    float menu_x, menu_y;
    int tab, group;

    int k_menu, k_master, k_chams, k_radar, k_unload;
};

extern Config c;

void defaults();
void load(const wchar_t* dir);
void save();

}
