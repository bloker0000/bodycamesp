#include "chams.hpp"
#include "config.hpp"
#include "draw.hpp"
#include "esp.hpp"
#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <utility>

using namespace sdk;
using cfg::c;

namespace chams {
namespace {

ue::Weak capture_actor, scene_actor;
UObject *capture, *scene;
UObject *rt_cap, *rt_warp, *rt_mask, *rt_depth, *rt_scene;
int rt_w, rt_h, logged;
std::vector<UVTri> mesh;
double mesh_w, mesh_h, mesh_i;
// ctrl+f12 sets this, the next visible parts frame writes its mask here
const wchar_t* probe_dir;

bool ensure_targets(UObject* hud, int w, int h, bool parts) {
    static int retry;
    int div = c.cham_res == 0 ? 1 : c.cham_res == 1 ? 2 : 4;
    int tw = std::max(64, w / div), th = std::max(64, h / div);
    static bool built_parts;
    bool all = alive(rt_cap) && alive(rt_warp) && alive(rt_mask) && (!built_parts || (alive(rt_depth) && alive(rt_scene)));
    if (all && parts == built_parts && tw == rt_w && th == rt_h) return true;
    if (retry-- > 0) return false;
    if (rt_cap && !all) log("a chams target died, rebuilding them");
    drop_rt(rt_cap), drop_rt(rt_warp), drop_rt(rt_mask), drop_rt(rt_depth), drop_rt(rt_scene);
    auto gi = game_instance(hud);
    if (!gi) return false;
    // half floats lose the depth difference at range
    auto fmt = parts ? RGBA32f : RGBA16f;
    rt_cap = make_rt(gi, tw, th, fmt);
    rt_warp = make_rt(gi, tw, th, fmt);
    rt_mask = make_rt(gi, tw, th, RGBA8);
    if (parts) {
        rt_depth = make_rt(gi, tw, th, RGBA32f);
        rt_scene = make_rt(gi, tw, th, RGBA32f);
    }
    for (auto rt : {rt_cap, rt_warp, rt_mask, rt_depth, rt_scene})
        if (rt) set_rooted(rt, true);
    rt_w = tw, rt_h = th;
    built_parts = parts;
    mesh.clear();
    if (capture && alive(capture)) at<UObject*>(capture, off.cap_target) = rt_cap;
    if (scene && alive(scene)) at<UObject*>(scene, off.cap_target) = rt_scene;
    bool ok = rt_cap && rt_warp && rt_mask && (!parts || (rt_depth && rt_scene));
    log(ok ? "chams targets %dx%d%s" : "chams targets %dx%d%s failed, retrying in a bit", tw, th, parts ? " with depth" : "");
    if (!ok) retry = 120;
    return ok;
}

// capture is plain but the screen goes through the lens, so bend it with a mesh
void warp(UObject* hud, UObject* src, UObject* dst) {
    if (mesh.empty() || mesh_w != draw::sw || mesh_h != draw::sh || std::abs(mesh_i - esp::lens_strength()) > 0.002) {
        mesh.clear();
        mesh_w = draw::sw, mesh_h = draw::sh, mesh_i = esp::lens_strength();
        const int nx = 40, ny = 24;
        double kx = rt_w / draw::sw, ky = rt_h / draw::sh;
        auto vert = [&](int i, int j, FVector2D& pos, FVector2D& uv) {
            uv = {(double)i / nx, (double)j / ny};
            auto d = esp::distort({uv.x * draw::sw, uv.y * draw::sh});
            pos = {d.x * kx, d.y * ky};
        };
        FLinearColor white{1, 1, 1, 1};
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                FVector2D p[4], uv[4];
                vert(i, j, p[0], uv[0]), vert(i + 1, j, p[1], uv[1]), vert(i + 1, j + 1, p[2], uv[2]), vert(i, j + 1, p[3], uv[3]);
                mesh.push_back({p[0], uv[0], white, p[1], uv[1], white, p[2], uv[2], white});
                mesh.push_back({p[0], uv[0], white, p[2], uv[2], white, p[3], uv[3], white});
            }
    }
    clear_rt(hud, dst, {0, 0, 0, 1});
    UObject* ctx = nullptr;
    if (auto cv = rt_begin(hud, dst, ctx)) {
        draw_tris(cv, src, mesh);
        rt_end(hud, ctx);
    }
}

UObject* spawn_capture(UObject* hud, ue::Weak& out, CaptureMode mode, CaptureSource source, UObject* target) {
    auto actor = spawn_begin(hud, cls.capture_actor);
    if (!actor) return nullptr;
    auto comp = at<UObject*>(actor, off.cap_comp);
    if (!comp) return nullptr;
    at<uint8_t>(comp, off.cap_mode) = mode;
    at<uint8_t>(comp, off.cap_source) = source;
    at<bool>(comp, off.cap_persist) = true;
    off.cap_every_frame.set(comp, false);
    off.cap_on_move.set(comp, false);
    at<UObject*>(comp, off.cap_target) = target;
    spawn_finish(actor);
    out = ue::Weak::of(actor);
    log("capture actor %s spawned", name(actor).c_str());
    return comp;
}

bool ensure_capture(UObject* hud) {
    if (capture_actor.get() && capture && alive(capture)) return true;
    capture = spawn_capture(hud, capture_actor, PrmShowOnly, ScsColor, rt_cap);
    return capture;
}

// only its depth gets read, so the expensive passes are off
bool ensure_scene(UObject* hud) {
    if (scene_actor.get() && scene && alive(scene)) return true;
    scene = spawn_capture(hud, scene_actor, PrmScene, ScsColorDepth, rt_scene);
    if (!scene) return false;
    show_flags_off(scene, {L"Lighting", L"PostProcessing", L"DynamicShadows", L"GlobalIllumination", L"LumenGlobalIllumination",
                           L"LumenReflections", L"ScreenSpaceReflections", L"AmbientOcclusion", L"DistanceFieldAO", L"ReflectionEnvironment",
                           L"SkyLighting", L"Fog", L"VolumetricFog", L"Atmosphere", L"Cloud", L"Translucency", L"Particles", L"Decals",
                           L"ContactShadows", L"CapsuleShadows", L"AntiAliasing", L"TemporalAA", L"MotionBlur", L"Bloom", L"EyeAdaptation",
                           L"LightShafts", L"LocalExposure", L"VolumetricLightmap"});
    return true;
}

void hide_with_attached(UObject* cap, UObject* actor) {
    if (!actor || !alive(actor)) return;
    hide(cap, actor);
    auto list = attached(actor, true);
    for (int32_t i = 0; i < list.num; i++)
        if (list.data[i]) hide(cap, list.data[i]);
}

bool padded(const esp::Target* t, double& x0, double& y0, double& x1, double& y1) {
    double pad = 6 * draw::ui;
    x0 = std::max(0.0, t->x0 - pad), y0 = std::max(0.0, t->y0 - pad);
    x1 = std::min(draw::sw, t->x1 + pad), y1 = std::min(draw::sh, t->y1 + pad);
    return x1 > x0 && y1 > y0;
}

void draw_whole(UObject* hud, const std::vector<const esp::Target*>& list) {
    UObject* source = rt_cap;
    if (esp::lens_on()) {
        warp(hud, rt_cap, rt_warp);
        source = rt_warp;
    }
    UObject* ctx = nullptr;
    clear_rt(hud, rt_mask, {1, 1, 1, 1});
    if (auto cv = rt_begin(hud, rt_mask, ctx)) {
        blit(cv, source, 0, 0, rt_w, rt_h, {1, 1, 1, 1}, AlphaHoldout);
        rt_end(hud, ctx);
    }
    for (auto t : list) {
        auto& g = c.g[t->group];
        double x0, y0, x1, y1;
        if (!padded(t, x0, y0, x1, y1)) continue;
        double u = x0 / draw::sw, v = y0 / draw::sh, uw = (x1 - x0) / draw::sw, vh = (y1 - y0) / draw::sh;
        auto col = draw::linear(t->visible || !c.vis_check ? g.cham_vis : g.cham_hid);
        float a = col.a * c.cham_alpha;
        blit(draw::canvas(), rt_mask, x0, y0, x1 - x0, y1 - y0, {col.r * a, col.g * a, col.b * a, a}, AlphaComposite, u, v, uw, vh);
    }
}

// canvas blends cant get a depth compare into alpha, so the screen gets a multiply and an add
void draw_parts(UObject* hud, const std::vector<const esp::Target*>& list) {
    clear_hidden(scene);
    hide_with_attached(scene, esp::frame.pawn);
    for (auto& t : esp::frame.targets) hide_with_attached(scene, t.actor);
    at<float>(scene, off.cap_fov) = (float)esp::view.fov;
    set_world_loc_rot(scene, esp::view.loc, esp::view.rot);
    capture_scene(scene);

    // depth is in alpha in cm, a translucent blit leaves 1 - depth in rgb
    UObject* ctx = nullptr;
    clear_rt(hud, rt_cap, {1, 1, 1, 1});
    clear_rt(hud, rt_warp, {1, 1, 1, 1});
    for (auto [dst, src] : {std::pair{rt_cap, rt_depth}, std::pair{rt_warp, rt_scene}})
        if (auto cv = rt_begin(hud, dst, ctx)) {
            blit(cv, src, 0, 0, rt_w, rt_h, {0, 0, 0, 1}, Translucent);
            rt_end(hud, ctx);
        }
    // scene minus targets, above 0 where a target is in front
    if (auto cv = rt_begin(hud, rt_cap, ctx)) {
        blit(cv, rt_warp, 0, 0, rt_w, rt_h, {-1, -1, -1, 1}, Additive);
        rt_end(hud, ctx);
    }
    // the 8 bit mask clamps that into a step
    if (esp::lens_on()) {
        warp(hud, rt_cap, rt_mask);
    } else if (auto cv = rt_begin(hud, rt_mask, ctx)) {
        blit(cv, rt_cap, 0, 0, rt_w, rt_h, {1, 1, 1, 1}, Opaque);
        rt_end(hud, ctx);
    }
    if (probe_dir) {
        export_rt(hud, rt_mask, probe_dir, L"parts_mask.png");
        probe_dir = nullptr;
    }

    // rt_cap gets 1 - alpha * mask, rt_warp color * alpha * mask. nearest target goes last
    clear_rt(hud, rt_cap, {1, 1, 1, 1});
    clear_rt(hud, rt_warp, {0, 0, 0, 0});
    double kx = rt_w / draw::sw, ky = rt_h / draw::sh;
    for (auto rt : {rt_cap, rt_warp}) {
        auto cv = rt_begin(hud, rt, ctx);
        if (!cv) continue;
        bool mul = rt == rt_cap;
        for (auto t : list) {
            double x0, y0, x1, y1;
            if (!padded(t, x0, y0, x1, y1)) continue;
            auto col = draw::linear(c.g[t->group].cham_vis);
            float a = col.a * c.cham_alpha;
            double x = x0 * kx, y = y0 * ky, w = (x1 - x0) * kx, h = (y1 - y0) * ky;
            blit(cv, nullptr, x, y, w, h, mul ? FLinearColor{1, 1, 1, 1} : FLinearColor{0, 0, 0, 0}, Opaque);
            FLinearColor k = mul ? FLinearColor{-a, -a, -a, 1} : FLinearColor{col.r * a, col.g * a, col.b * a, 1};
            blit(cv, rt_mask, x, y, w, h, k, Additive, x0 / draw::sw, y0 / draw::sh, (x1 - x0) / draw::sw, (y1 - y0) / draw::sh);
        }
        rt_end(hud, ctx);
    }
    blit(draw::canvas(), rt_cap, 0, 0, draw::sw, draw::sh, {1, 1, 1, 1}, Modulate);
    blit(draw::canvas(), rt_warp, 0, 0, draw::sw, draw::sh, {1, 1, 1, 0}, Additive);
}

}

void frame(UObject* hud) {
    if (!c.chams_on) return;
    std::vector<const esp::Target*> list;
    for (auto& t : esp::frame.targets)
        if (c.g[t.group].chams && t.on_screen && !t.in_scope) list.push_back(&t);
    if (list.empty()) return;
    bool parts = c.cham_parts;
    if (!ensure_targets(hud, (int)draw::sw, (int)draw::sh, parts) || !ensure_capture(hud)) return;
    if (parts && !ensure_scene(hud)) parts = false;

    clear_show_only(capture);
    for (auto t : list) show_only(capture, t->actor);
    at<uint8_t>(capture, off.cap_source) = parts ? ScsColorDepth : ScsColor;
    off.cap_unrendered.set(capture, !parts);
    at<UObject*>(capture, off.cap_target) = parts ? rt_depth : rt_cap;
    at<float>(capture, off.cap_fov) = (float)esp::view.fov;
    set_world_loc_rot(capture, esp::view.loc, esp::view.rot);
    capture_scene(capture);

    if (parts) draw_parts(hud, list);
    else draw_whole(hud, list);
    if (logged != parts + 1) {
        logged = parts + 1;
        log("chams first frame%s: %zu targets", parts ? " (visible parts)" : "", list.size());
    }
}

// same camera twice in one frame, plain and through the game's lens
void calibrate(UObject* hud, UObject* pawn, const wchar_t* dir) {
    if (c.cham_parts) probe_dir = dir;
    if (!ensure_capture(hud)) return log("calibration: no capture");
    TArray<Blendable>* lens = nullptr;
    if (pawn) {
        auto cams = components(pawn, cls.camera);
        for (int32_t i = 0; i < cams.num && !lens; i++) {
            auto& list = blendables(cams.data[i]);
            for (int32_t k = 0; k < list.num; k++)
                if (list.data[k].object && name_is(list.data[k].object->name, "M_PP_BodycamMaster")) lens = &list;
        }
    }
    if (!lens) return log("calibration: no lens on the player camera");
    auto gi = game_instance(hud);
    auto plain = make_rt(gi, (int)draw::sw, (int)draw::sh, RGBA8);
    auto warped = make_rt(gi, (int)draw::sw, (int)draw::sh, RGBA8);
    if (!plain || !warped) return log("calibration: no render target");

    auto pps = (uint8_t*)capture + off.cap_pps;
    // no eye adaptation history, comes out very dark without this
    off.ov_bias.set(pps, true);
    at<float>(pps, off.pps_bias) = 3.f;
    at<float>(capture, off.cap_pp_weight) = 1.f;
    at<uint8_t>(capture, off.cap_mode) = PrmScene;
    at<uint8_t>(capture, off.cap_source) = ScsFinalLdr;
    at<float>(capture, off.cap_fov) = (float)esp::view.fov;
    set_world_loc_rot(capture, esp::view.loc, esp::view.rot);

    at<UObject*>(capture, off.cap_target) = plain;
    capture_scene(capture);
    export_rt(hud, plain, dir, L"lens_plain.png");

    // sweeps the lens strength to see how the curve scales
    auto mpc = find("/Game/PostProcess/Materials/MPC_PostProcessMaterialSettings.MPC_PostProcessMaterialSettings");
    auto intensity = to_name(L"LensDistortionIntensity"), density = to_name(L"LensDistortionDensity");
    float was = mpc ? mpc_get(hud, mpc, intensity) : 0;
    log("calibration: lens intensity %.3f density %.3f", was, mpc ? mpc_get(hud, mpc, density) : 0);
    // borrowed from the camera, its only read while the view is set up
    auto& mine = at<TArray<Blendable>>(pps, off.pps_blendables);
    auto saved = mine;
    mine = *lens;
    at<UObject*>(capture, off.cap_target) = warped;
    for (float v : {0.1f, 0.2f, 0.33f, 0.5f}) {
        if (mpc) mpc_set(hud, mpc, intensity, v);
        capture_scene(capture);
        wchar_t file[64];
        swprintf(file, 64, L"lens_warped_%03d.png", (int)std::lround(v * 100));
        export_rt(hud, warped, dir, file);
        if (!mpc) break;
    }
    mine = saved;
    if (mpc) mpc_set(hud, mpc, intensity, was);

    off.ov_bias.set(pps, false);
    at<uint8_t>(capture, off.cap_mode) = PrmShowOnly;
    at<uint8_t>(capture, off.cap_source) = ScsColor;
    at<UObject*>(capture, off.cap_target) = rt_cap;
    log("calibration written, fov %.3f, %dx%d", esp::view.fov, (int)draw::sw, (int)draw::sh);
}

void reset() {
    capture = scene = nullptr;
    capture_actor = scene_actor = {};
}

void shutdown() {
    if (auto a = capture_actor.get()) destroy(a);
    if (auto a = scene_actor.get()) destroy(a);
    reset();
    drop_rt(rt_cap), drop_rt(rt_warp), drop_rt(rt_mask), drop_rt(rt_depth), drop_rt(rt_scene);
    rt_w = rt_h = 0;
    mesh.clear();
}

}
