#include "overlay.hpp"
#include "draw.hpp"
#include "log.hpp"
#include "sdk.hpp"

using namespace sdk;

namespace overlay {
namespace {

UObject *widget, *image, *rt_draw, *rt_alpha, *rt_show, *ctx, *hud_canvas;
int rt_w, rt_h;
bool showing;

constexpr int z_order = 100000;
constexpr uint8_t hit_test_invisible = 3;

bool ensure_targets(UObject* hud, int w, int h) {
    if (alive(rt_draw) && alive(rt_alpha) && alive(rt_show) && w == rt_w && h == rt_h) return true;
    drop_rt(rt_draw), drop_rt(rt_alpha), drop_rt(rt_show);
    auto gi = game_instance(hud);
    if (!gi) return false;
    rt_draw = make_rt(gi, w, h, RGBA16f);
    rt_alpha = make_rt(gi, w, h, RGBA8);
    rt_show = make_rt(gi, w, h, RGBA16f);
    for (auto rt : {rt_draw, rt_alpha, rt_show})
        if (rt) set_rooted(rt, true);
    rt_w = w, rt_h = h;
    if (alive(image)) set_brush(image, rt_show);
    return rt_draw && rt_alpha && rt_show;
}

// blank native widget so no game blueprint runs
bool ensure_widget(UObject* hud) {
    if (alive(widget)) return true;
    widget = create_widget(hud, cls.common_widget);
    auto tree = widget ? at<UObject*>(widget, off.widget_tree) : nullptr;
    image = tree ? spawn_object(cls.image, tree) : nullptr;
    if (!image) {
        widget = nullptr;
        return false;
    }
    at<UObject*>(tree, off.tree_root) = image;
    set_brush(image, rt_show);
    set_visibility(image, hit_test_invisible);
    set_visibility(widget, hit_test_invisible);
    set_rooted(widget, true);
    log("menu layer %s ready", name(widget).c_str());
    return true;
}

}

bool begin(UObject* hud, int w, int h) {
    static int retry;
    if (retry > 0) {
        retry--;
        return false;
    }
    if (!ensure_targets(hud, w, h) || !ensure_widget(hud)) {
        log("no menu layer, the menu stays on the hud for now");
        retry = 300;
        return false;
    }
    if (!in_viewport(widget)) add_to_viewport(widget, z_order);
    showing = true;
    clear_rt(hud, rt_draw, {0, 0, 0, 1});
    auto cv = rt_begin(hud, rt_draw, ctx);
    if (!cv) return false;
    hud_canvas = draw::canvas();
    draw::set_canvas(cv);
    draw::shapes_begin();
    return true;
}

void end(UObject* hud) {
    draw::set_canvas(hud_canvas);
    rt_end(hud, ctx);
    // translucent draws dont write alpha, so the shapes go again as holdouts
    UObject* other = nullptr;
    clear_rt(hud, rt_alpha, {0, 0, 0, 1});
    if (auto cv = rt_begin(hud, rt_alpha, other)) {
        draw::shapes_end(cv);
        rt_end(hud, other);
    }
    // slate wants what covers, a holdout flips it and the add only brings rgb
    clear_rt(hud, rt_show, {0, 0, 0, 1});
    if (auto cv = rt_begin(hud, rt_show, other)) {
        blit(cv, rt_alpha, 0, 0, rt_w, rt_h, {1, 1, 1, 1}, AlphaHoldout);
        blit(cv, rt_draw, 0, 0, rt_w, rt_h, {1, 1, 1, 0}, Additive);
        rt_end(hud, other);
    }
}

void hide() {
    if (!showing) return;
    showing = false;
    if (alive(widget) && in_viewport(widget)) remove_from_parent(widget);
}

void dump(UObject* hud, const wchar_t* dir) {
    if (alive(rt_show)) export_rt(hud, rt_show, dir, L"menu_layer.exr");
}

// the viewport drops widgets on world change, a fresh one is simpler
void reset() {
    if (alive(widget)) {
        remove_from_parent(widget);
        set_rooted(widget, false);
    }
    widget = image = nullptr;
    showing = false;
}

void shutdown() {
    reset();
    drop_rt(rt_draw), drop_rt(rt_alpha), drop_rt(rt_show);
    rt_w = rt_h = 0;
}

}
