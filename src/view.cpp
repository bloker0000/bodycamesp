#include "view.hpp"
#include "config.hpp"
#include "log.hpp"
#include "sdk.hpp"

#include <vector>

using namespace sdk;
using cfg::c;

namespace view {
namespace {

struct Touched {
    ue::Weak camera;
    int index;
    float weight;
};

std::vector<Touched> touched;
ue::Weak volume;
UObject* last_pawn;
std::vector<UObject*> cameras;

bool is_lens(UObject* o) {
    return o && (name_is(o->name, "M_PP_BodycamMaster") || name_is(o->name, "MI_PP_DiskBlur"));
}

void restore_lens() {
    for (auto& t : touched)
        if (auto cam = t.camera.get()) {
            auto& list = blendables(cam);
            if (t.index < list.num) list.data[t.index].weight = t.weight;
        }
    touched.clear();
}

void apply_lens(UObject* pawn, bool remove) {
    if (!remove) return restore_lens();
    if (pawn != last_pawn) {
        restore_lens();
        last_pawn = pawn;
        cameras.clear();
        if (pawn) {
            auto list = components(pawn, cls.camera);
            for (int i = 0; i < list.num; i++) cameras.push_back(list.data[i]);
        }
    }
    for (auto cam : cameras) {
        if (!alive(cam)) continue;
        auto& list = blendables(cam);
        for (int i = 0; i < list.num; i++) {
            auto& b = list.data[i];
            if (!is_lens(b.object) || b.weight == 0) continue;
            touched.push_back({ue::Weak::of(cam), i, b.weight});
            b.weight = 0;
        }
    }
}

void apply_volume(UObject* hud, bool active) {
    bool want = active && (c.bright > 0.01f || c.no_vignette || c.no_grain || c.no_blur);
    auto ppv = volume.get();
    if (!want) {
        if (ppv) off.ppv_enabled.set(ppv, false);
        return;
    }
    if (!ppv) {
        ppv = spawn_begin(hud, cls.ppv);
        if (!ppv) return;
        off.ppv_unbound.set(ppv, true);
        off.ppv_enabled.set(ppv, true);
        at<float>(ppv, off.ppv_priority) = 100000.f;
        at<float>(ppv, off.ppv_weight) = 1.f;
        spawn_finish(ppv);
        volume = ue::Weak::of(ppv);
        log("post process volume spawned");
    }
    off.ppv_enabled.set(ppv, true);
    auto s = (uint8_t*)ppv + off.ppv_settings;
    off.ov_bias.set(s, c.bright > 0.01f);
    at<float>(s, off.pps_bias) = c.bright;
    off.ov_vignette.set(s, c.no_vignette);
    at<float>(s, off.pps_vignette) = 0;
    off.ov_grain.set(s, c.no_grain);
    at<float>(s, off.pps_grain) = 0;
    off.ov_blur.set(s, c.no_blur);
    at<float>(s, off.pps_blur) = 0;
}

}

void frame(UObject* hud, UObject* pawn, bool active) {
    apply_lens(pawn, active && c.no_lens);
    apply_volume(hud, active);
}

void reset() {
    touched.clear();
    cameras.clear();
    last_pawn = nullptr;
    volume = {};
}

void shutdown() {
    restore_lens();
    if (auto v = volume.get()) destroy(v);
    reset();
}

}
