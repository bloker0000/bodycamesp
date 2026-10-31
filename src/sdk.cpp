#include "sdk.hpp"

#include <cstring>
#include <cwchar>

namespace sdk {

Cls cls;
Fns fn;
Offs off;
UObject* font;

namespace {

UObject *transient, *statics, *render_lib, *sys_lib, *mat_lib, *str_lib, *widget_lib;
UClass* tag_iface;
std::string* missing_out;

UClass* C(const char* path) {
    auto c = find_class(path);
    if (!c) *missing_out += std::string(path) + " ";
    return c;
}

UObject* O(const char* path) {
    auto o = find(path);
    if (!o) *missing_out += std::string(path) + " ";
    return o;
}

UFunction* F(UClass* c, const char* n) {
    auto f = c ? find_fn(c, n) : nullptr;
    if (!f) *missing_out += std::string(n) + "() ";
    return f;
}

int32_t P(UClass* c, const char* n) {
    auto o = c ? prop_offset(c, n) : -1;
    if (o < 0) *missing_out += std::string(n) + " ";
    return o;
}

BoolProp B(UClass* c, const char* n) {
    auto b = c ? bool_prop(c, n) : BoolProp{};
    if (b.offset < 0) *missing_out += std::string(n) + " ";
    return b;
}

struct Transform {
    double qx, qy, qz, qw, tx, ty, tz, tpad, sx, sy, sz, spad;
};
static_assert(sizeof(Transform) == 0x60);
constexpr Transform identity{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0};

}

bool resolve(std::string& missing) {
    missing_out = &missing;
    cls.character = C("/Script/Engine.Character");
    cls.camera = C("/Script/Engine.CameraComponent");
    cls.capture_actor = C("/Script/Engine.SceneCapture2D");
    cls.ppv = C("/Script/Engine.PostProcessVolume");
    cls.bc_char = C("/Script/Bodycam.BodycamCharacter");
    cls.bc_ps = C("/Script/Bodycam.BodycamPlayerState");
    cls.scope = C("/Script/Bodycam.BodycamScopeCaptureComponent");
    cls.common_widget = C("/Script/CommonUI.CommonUserWidget");
    cls.image = C("/Script/UMG.Image");
    tag_iface = C("/Script/GameplayTags.GameplayTagAssetInterface");
    auto actor = C("/Script/Engine.Actor");
    auto pawn = C("/Script/Engine.Pawn");
    auto controller = C("/Script/Engine.Controller");
    auto player_controller = C("/Script/Engine.PlayerController");
    auto player_state = C("/Script/Engine.PlayerState");
    auto hud = C("/Script/Engine.HUD");
    auto canvas = C("/Script/Engine.Canvas");
    auto scene = C("/Script/Engine.SceneComponent");
    auto skinned = C("/Script/Engine.SkinnedMeshComponent");
    auto capsule = C("/Script/Engine.CapsuleComponent");
    auto cam_mgr = C("/Script/Engine.PlayerCameraManager");
    auto engine_cls = C("/Script/Engine.Engine");
    auto font_cls = C("/Script/Engine.Font");
    auto capture = C("/Script/Engine.SceneCaptureComponent2D");
    auto capture_base = C("/Script/Engine.SceneCaptureComponent");
    auto widget_cls = C("/Script/UMG.Widget");
    auto user_widget = C("/Script/UMG.UserWidget");
    auto tree_cls = C("/Script/UMG.WidgetTree");
    auto widget_lib_cls = C("/Script/UMG.WidgetBlueprintLibrary");
    auto cache = C("/Script/Engine.CameraCacheEntry");
    auto pov = C("/Script/Engine.MinimalViewInfo");
    auto attrs = C("/Script/Bodycam.CharacterAttributeSet");
    auto attr = C("/Script/GameplayAbilities.GameplayAttributeData");
    auto pps = C("/Script/Engine.PostProcessSettings");
    auto statics_cls = C("/Script/Engine.GameplayStatics");
    auto render_cls = C("/Script/Engine.KismetRenderingLibrary");
    auto sys_cls = C("/Script/Engine.KismetSystemLibrary");
    auto mat_cls = C("/Script/Engine.KismetMaterialLibrary");
    auto str_cls = C("/Script/Engine.KismetStringLibrary");
    if (!missing.empty()) return false;

    statics = O("/Script/Engine.Default__GameplayStatics");
    render_lib = O("/Script/Engine.Default__KismetRenderingLibrary");
    sys_lib = O("/Script/Engine.Default__KismetSystemLibrary");
    mat_lib = O("/Script/Engine.Default__KismetMaterialLibrary");
    str_lib = O("/Script/Engine.Default__KismetStringLibrary");
    widget_lib = O("/Script/UMG.Default__WidgetBlueprintLibrary");
    transient = O("/Engine/Transient");

    fn.text = F(canvas, "K2_DrawText");
    fn.texture = F(canvas, "K2_DrawTexture");
    fn.triangle = F(canvas, "K2_DrawTriangle");
    fn.text_size = F(canvas, "K2_TextSize");
    fn.get_all = F(statics_cls, "GetAllActorsOfClass");
    fn.spawn_begin = F(statics_cls, "BeginDeferredActorSpawnFromClass");
    fn.spawn_finish = F(statics_cls, "FinishSpawningActor");
    fn.destroy = F(actor, "K2_DestroyActor");
    fn.components = F(actor, "K2_GetComponentsByClass");
    fn.attached = F(actor, "GetAttachedActors");
    fn.create_rt = F(render_cls, "CreateRenderTarget2D");
    fn.clear_rt = F(render_cls, "ClearRenderTarget2D");
    fn.rt_begin = F(render_cls, "BeginDrawCanvasToRenderTarget");
    fn.rt_end = F(render_cls, "EndDrawCanvasToRenderTarget");
    fn.import_tex = F(render_cls, "ImportBufferAsTexture2D");
    fn.export_rt = F(render_cls, "ExportRenderTarget");
    fn.to_name = F(str_cls, "Conv_StringToName");
    fn.game_instance = F(statics_cls, "GetGameInstance");
    fn.mpc_get = F(mat_cls, "GetScalarParameterValue");
    fn.mpc_set = F(mat_cls, "SetScalarParameterValue");
    fn.socket = F(scene, "GetSocketLocation");
    fn.comp_loc = F(scene, "K2_GetComponentLocation");
    fn.comp_rot = F(scene, "K2_GetComponentRotation");
    fn.set_world_loc_rot = F(scene, "K2_SetWorldLocationAndRotation");
    fn.bone_name = F(skinned, "GetBoneName");
    fn.num_bones = F(skinned, "GetNumBones");
    fn.los = F(controller, "LineOfSightTo");
    fn.aim_rot = F(pawn, "GetBaseAimRotation");
    fn.line_trace = F(sys_cls, "LineTraceSingle");
    fn.sphere_trace = F(sys_cls, "SphereTraceMulti");
    fn.implements = F(sys_cls, "DoesImplementInterface");
    fn.has_tag = F(tag_iface, "HasMatchingGameplayTag");
    fn.show_only = F(capture_base, "ShowOnlyActorComponents");
    fn.clear_show_only = F(capture_base, "ClearShowOnlyComponents");
    fn.hide_actor = F(capture_base, "HideActorComponents");
    fn.clear_hidden = F(capture_base, "ClearHiddenComponents");
    fn.show_flags = F(capture_base, "SetShowFlagSettings");
    fn.capture_scene = F(capture, "CaptureScene");
    fn.scope_active = F(cls.scope, "IsScopeCaptureActive");
    fn.create_widget = F(widget_lib_cls, "Create");
    fn.spawn_object = F(statics_cls, "SpawnObject");
    fn.add_viewport = F(user_widget, "AddToViewport");
    fn.remove_parent = F(widget_cls, "RemoveFromParent");
    fn.in_viewport = F(widget_cls, "IsInViewport");
    fn.set_visibility = F(widget_cls, "SetVisibility");
    fn.set_brush = F(cls.image, "SetBrushResourceObject");

    off.hud_owner = P(hud, "PlayerOwner");
    off.hud_canvas = P(hud, "Canvas");
    off.canvas_w = P(canvas, "SizeX");
    off.canvas_h = P(canvas, "SizeY");
    off.pc_cam = P(player_controller, "PlayerCameraManager");
    off.pc_pawn = P(player_controller, "AcknowledgedPawn");
    off.ctrl_ps = P(controller, "PlayerState");
    off.cam_cache = P(cam_mgr, "CameraCachePrivate");
    off.cache_pov = P(cache, "POV");
    off.pov_loc = P(pov, "Location");
    off.pov_rot = P(pov, "Rotation");
    off.pov_fov = P(pov, "FOV");
    off.actor_root = P(actor, "RootComponent");
    off.scene_loc = P(scene, "RelativeLocation");
    off.char_mesh = P(cls.character, "Mesh");
    off.char_capsule = P(cls.character, "CapsuleComponent");
    off.capsule_half = P(capsule, "CapsuleHalfHeight");
    off.pawn_ps = P(pawn, "PlayerState");
    off.bc_cached_ps = P(cls.bc_char, "CachedPlayerState");
    off.bc_set = P(cls.bc_char, "CharacterSet");
    off.set_hp = P(attrs, "Health");
    off.set_maxhp = P(attrs, "MaxHealth");
    off.attr_cur = P(attr, "CurrentValue");
    off.ps_team = P(cls.bc_ps, "TeamID");
    off.ps_name = P(player_state, "PlayerNamePrivate");
    off.font_size = P(font_cls, "LegacyFontSize");
    off.font_face = P(font_cls, "LegacyFontName");
    off.cap_mode = P(capture_base, "PrimitiveRenderMode");
    off.cap_source = P(capture_base, "CaptureSource");
    off.cap_persist = P(capture_base, "bAlwaysPersistRenderingState");
    off.cap_every_frame = B(capture_base, "bCaptureEveryFrame");
    off.cap_on_move = B(capture_base, "bCaptureOnMovement");
    off.cap_fov = P(capture, "FOVAngle");
    off.cap_target = P(capture, "TextureTarget");
    off.cap_unrendered = B(capture, "bConsiderUnrenderedOpaquePixelAsFullyTranslucent");
    off.cap_comp = P(cls.capture_actor, "CaptureComponent2D");
    off.cap_pps = P(capture, "PostProcessSettings");
    off.cap_pp_weight = P(capture, "PostProcessBlendWeight");
    off.scope_mag = P(cls.scope, "Magnification");
    off.widget_tree = P(user_widget, "WidgetTree");
    off.tree_root = P(tree_cls, "RootWidget");
    off.ppv_settings = P(cls.ppv, "Settings");
    off.ppv_priority = P(cls.ppv, "Priority");
    off.ppv_weight = P(cls.ppv, "BlendWeight");
    off.ppv_enabled = B(cls.ppv, "bEnabled");
    off.ppv_unbound = B(cls.ppv, "bUnbound");
    off.pps_bias = P(pps, "AutoExposureBias");
    off.pps_vignette = P(pps, "VignetteIntensity");
    off.pps_grain = P(pps, "FilmGrainIntensity");
    off.pps_blur = P(pps, "MotionBlurAmount");
    off.pps_blendables = P(pps, "WeightedBlendables");
    off.ov_bias = B(pps, "bOverride_AutoExposureBias");
    off.ov_vignette = B(pps, "bOverride_VignetteIntensity");
    off.ov_grain = B(pps, "bOverride_FilmGrainIntensity");
    off.ov_blur = B(pps, "bOverride_MotionBlurAmount");
    off.cam_pps = P(cls.camera, "PostProcessSettings");

    UObject* engine = nullptr;
    for (int32_t i = 0, n = object_count(); i < n && !engine; i++) {
        auto o = object_at(i);
        if (o && !(o->flags & rf_class_default) && is_a(o, engine_cls)) engine = o;
    }
    if (!engine) missing += "engine ";
    int32_t small_font = P(engine_cls, "SmallFont");
    font = engine && small_font >= 0 ? at<UObject*>(engine, small_font) : nullptr;
    if (!font) missing += "font ";
    return missing.empty();
}

FString fstr(const wchar_t* s) {
    int n = (int)wcslen(s) + 1;
    return {s, n, n};
}

FName to_name(const wchar_t* s) {
    struct { FString in; FName out; } p{fstr(s)};
    static_assert(offsetof(decltype(p), out) == 0x10);
    call(str_lib, fn.to_name, &p);
    return p.out;
}

UObject* import_png(const std::vector<uint8_t>& png) {
    struct { UObject* ctx; TArray<uint8_t> buf; UObject* out; } p{transient, {(uint8_t*)png.data(), (int32_t)png.size(), (int32_t)png.size()}};
    static_assert(offsetof(decltype(p), out) == 0x18);
    call(render_lib, fn.import_tex, &p);
    return p.out;
}

UObject* game_instance(UObject* ctx) {
    struct { UObject* ctx; UObject* out; } p{ctx};
    call(statics, fn.game_instance, &p);
    return p.out;
}

// pass the game instance as outer, it has a world but outlives maps
UObject* make_rt(UObject* outer, int w, int h, RtFormat fmt) {
    struct {
        UObject* ctx;
        int32_t w, h;
        uint8_t fmt;
        FLinearColor clear;
        bool mips, uav;
        UObject* out;
    } p{outer, w, h, fmt, {0, 0, 0, 0}};
    static_assert(offsetof(decltype(p), clear) == 0x14 && offsetof(decltype(p), out) == 0x28);
    call(render_lib, fn.create_rt, &p);
    return p.out;
}

void drop_rt(UObject*& rt) {
    if (rt) set_rooted(rt, false);
    rt = nullptr;
}

void export_rt(UObject* ctx, UObject* rt, const wchar_t* dir, const wchar_t* file) {
    struct { UObject* ctx; UObject* rt; FString dir, file; } p{ctx, rt, fstr(dir), fstr(file)};
    static_assert(offsetof(decltype(p), file) == 0x20);
    call(render_lib, fn.export_rt, &p);
}

void clear_rt(UObject* ctx, UObject* rt, FLinearColor c) {
    struct { UObject* ctx; UObject* rt; FLinearColor c; } p{ctx, rt, c};
    call(render_lib, fn.clear_rt, &p);
}

UObject* rt_begin(UObject* ctx, UObject* rt, UObject*& context) {
    struct { UObject* ctx; UObject* rt; UObject* canvas; FVector2D size; UObject* context; } p{ctx, rt};
    static_assert(offsetof(decltype(p), context) == 0x28);
    call(render_lib, fn.rt_begin, &p);
    context = p.context;
    return p.canvas;
}

void rt_end(UObject* ctx, UObject* context) {
    struct { UObject* ctx; UObject* context; } p{ctx, context};
    call(render_lib, fn.rt_end, &p);
}

void blit(UObject* canvas, UObject* tex, double x, double y, double w, double h, FLinearColor col, Blend b, double u, double v, double uw, double vh) {
    TextureP p{tex, {x, y}, {w, h}, {u, v}, {uw, vh}, col, (uint8_t)b, 0, {0.5, 0.5}};
    call(canvas, fn.texture, &p);
}

void draw_tris(UObject* canvas, UObject* tex, const std::vector<UVTri>& t) {
    struct { UObject* tex; TArray<UVTri> t; } p{tex, {(UVTri*)t.data(), (int32_t)t.size(), (int32_t)t.size()}};
    call(canvas, fn.triangle, &p);
}

UObject* spawn_begin(UObject* ctx, UClass* c) {
    struct {
        UObject* ctx;
        UClass* cls;
        Transform t;
        uint8_t collision;
        UObject* owner;
        uint8_t scale;
        UObject* out;
    } p{ctx, c, identity, 1, nullptr, 2};
    static_assert(offsetof(decltype(p), collision) == 0x70 && offsetof(decltype(p), out) == 0x88);
    call(statics, fn.spawn_begin, &p);
    return p.out;
}

void spawn_finish(UObject* actor) {
    struct { UObject* actor; uint8_t pad[8]; Transform t; uint8_t scale; UObject* out; } p{actor, {}, identity, 2};
    static_assert(offsetof(decltype(p), t) == 0x10 && offsetof(decltype(p), out) == 0x78);
    call(statics, fn.spawn_finish, &p);
}

void destroy(UObject* actor) {
    if (alive(actor)) call(actor, fn.destroy, nullptr);
}

// the engine reallocates the out array, a static param block stops that leaking
TArray<UObject*> all_actors(UObject* ctx, UClass* c) {
    static struct { UObject* ctx; UClass* cls; TArray<UObject*> out; } p{};
    p.ctx = ctx;
    p.cls = c;
    call(statics, fn.get_all, &p);
    return p.out;
}

TArray<UObject*> components(UObject* actor, UClass* c) {
    static struct { UClass* cls; TArray<UObject*> out; } p{};
    p.cls = c;
    call(actor, fn.components, &p);
    return p.out;
}

TArray<UObject*> attached(UObject* actor, bool recursive) {
    static struct { TArray<UObject*> out; bool reset, recursive; } p{};
    p.reset = true;
    p.recursive = recursive;
    call(actor, fn.attached, &p);
    return p.out;
}

FVector socket(UObject* comp, FName n) {
    struct { FName n; FVector out; } p{n};
    call(comp, fn.socket, &p);
    return p.out;
}

// channel is an ETraceTypeQuery, hit is left alone on a miss
bool line_trace(UObject* ctx, const FVector& from, const FVector& to, uint8_t channel, UObject* ignore, FVector& hit) {
    static struct {
        UObject* ctx;
        FVector from, to;
        uint8_t channel;
        bool complex;
        TArray<UObject*> ignore;
        uint8_t debug;
        HitResult hit;
        bool self;
        FLinearColor color, hit_color;
        float time;
        bool out;
    } p{};
    static_assert(offsetof(decltype(p), ignore) == 0x40 && offsetof(decltype(p), hit) == 0x58);
    static_assert(offsetof(decltype(p), self) == 0x158 && offsetof(decltype(p), out) == 0x180);
    p.ctx = ctx;
    p.from = from;
    p.to = to;
    p.channel = channel;
    p.complex = true;
    p.ignore = {&ignore, 1, 1};
    p.self = true;
    call(sys_lib, fn.line_trace, &p);
    p.ignore = {};
    if (p.out) hit = p.hit.impact();
    return p.out;
}

// the hits stay valid until the next call
TArray<HitResult> sphere_trace(UObject* ctx, const FVector& from, const FVector& to, float radius, uint8_t channel, UObject* ignore) {
    static struct {
        UObject* ctx;
        FVector from, to;
        float radius;
        uint8_t channel;
        bool complex;
        TArray<UObject*> ignore;
        uint8_t debug;
        TArray<HitResult> hits;
        bool self;
        FLinearColor color, hit_color;
        float time;
        bool out;
    } p{};
    static_assert(offsetof(decltype(p), ignore) == 0x40 && offsetof(decltype(p), hits) == 0x58);
    static_assert(offsetof(decltype(p), self) == 0x68 && offsetof(decltype(p), out) == 0x90);
    p.ctx = ctx;
    p.from = from;
    p.to = to;
    p.radius = radius;
    p.channel = channel;
    p.complex = true;
    p.ignore = {&ignore, 1, 1};
    p.self = true;
    call(sys_lib, fn.sphere_trace, &p);
    p.ignore = {};
    return p.hits;
}

bool has_tag(UObject* o, FName tag) {
    struct { UObject* obj; UClass* iface; bool out; } check{o, tag_iface};
    call(sys_lib, fn.implements, &check);
    if (!check.out) return false;
    struct { FName tag; bool out; } p{tag};
    static_assert(offsetof(decltype(p), out) == 8);
    call(o, fn.has_tag, &p);
    return p.out;
}

int num_bones(UObject* mesh) {
    int32_t n = 0;
    call(mesh, fn.num_bones, &n);
    return n;
}

FName bone_name(UObject* mesh, int i) {
    struct { int32_t i; FName out; } p{i};
    call(mesh, fn.bone_name, &p);
    return p.out;
}

bool line_of_sight(UObject* ctrl, UObject* target, const FVector& from) {
    struct { UObject* other; FVector view; bool alt, out; } p{target, from, true};
    static_assert(offsetof(decltype(p), alt) == 0x20 && offsetof(decltype(p), out) == 0x21);
    call(ctrl, fn.los, &p);
    return p.out;
}

FRotator aim_rotation(UObject* pawn) {
    FRotator r{};
    call(pawn, fn.aim_rot, &r);
    return r;
}

void set_world_loc_rot(UObject* comp, const FVector& loc, const FRotator& rot) {
    static struct { FVector loc; FRotator rot; bool sweep; uint8_t pad[7]; uint8_t hit[0x100]; bool teleport; } p{};
    static_assert(offsetof(decltype(p), hit) == 0x38 && offsetof(decltype(p), teleport) == 0x138);
    p.loc = loc;
    p.rot = rot;
    p.sweep = false;
    p.teleport = true;
    call(comp, fn.set_world_loc_rot, &p);
}

FVector comp_location(UObject* comp) {
    FVector out{};
    call(comp, fn.comp_loc, &out);
    return out;
}

FRotator comp_rotation(UObject* comp) {
    FRotator out{};
    call(comp, fn.comp_rot, &out);
    return out;
}

bool scope_active(UObject* scope) {
    bool out = false;
    call(scope, fn.scope_active, &out);
    return out;
}

float mpc_get(UObject* ctx, UObject* collection, FName n) {
    struct { UObject* ctx; UObject* coll; FName n; float out; } p{ctx, collection, n};
    static_assert(offsetof(decltype(p), out) == 0x18);
    call(mat_lib, fn.mpc_get, &p);
    return p.out;
}

void mpc_set(UObject* ctx, UObject* collection, FName n, float v) {
    struct { UObject* ctx; UObject* coll; FName n; float v; } p{ctx, collection, n, v};
    call(mat_lib, fn.mpc_set, &p);
}

void show_only(UObject* capture, UObject* actor) {
    struct { UObject* actor; bool children; } p{actor, false};
    call(capture, fn.show_only, &p);
}

void clear_show_only(UObject* capture) { call(capture, fn.clear_show_only, nullptr); }

void hide(UObject* capture, UObject* actor) {
    struct { UObject* actor; bool children; } p{actor, true};
    call(capture, fn.hide_actor, &p);
}

void clear_hidden(UObject* capture) { call(capture, fn.clear_hidden, nullptr); }

// unknown names get skipped
void show_flags_off(UObject* capture, const std::vector<const wchar_t*>& flags) {
    struct Setting { FString name; bool on; };
    static_assert(sizeof(Setting) == 0x18);
    std::vector<Setting> list;
    for (auto f : flags) list.push_back({fstr(f), false});
    TArray<Setting> p{list.data(), (int32_t)list.size(), (int32_t)list.size()};
    call(capture, fn.show_flags, &p);
}

void capture_scene(UObject* capture) { call(capture, fn.capture_scene, nullptr); }

UObject* create_widget(UObject* ctx, UClass* c) {
    struct { UObject* ctx; UClass* cls; UObject* owner; UObject* out; } p{ctx, c};
    call(widget_lib, fn.create_widget, &p);
    return p.out;
}

UObject* spawn_object(UClass* c, UObject* outer) {
    struct { UClass* cls; UObject* outer; UObject* out; } p{c, outer};
    call(statics, fn.spawn_object, &p);
    return p.out;
}

void add_to_viewport(UObject* widget, int z) {
    int32_t p = z;
    call(widget, fn.add_viewport, &p);
}

void remove_from_parent(UObject* widget) { call(widget, fn.remove_parent, nullptr); }

bool in_viewport(UObject* widget) {
    bool out = false;
    call(widget, fn.in_viewport, &out);
    return out;
}

void set_visibility(UObject* widget, uint8_t v) { call(widget, fn.set_visibility, &v); }
void set_brush(UObject* image, UObject* resource) { call(image, fn.set_brush, &resource); }

}
