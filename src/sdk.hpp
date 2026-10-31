#pragma once
#include "ue.hpp"

#include <string>
#include <vector>

namespace sdk {
using namespace ue;

enum Blend : uint8_t { Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite, AlphaHoldout };
enum RtFormat : uint8_t { RGBA8 = 2, RGBA16f = 6, RGBA32f = 9 };
enum CaptureMode : uint8_t { PrmScene = 1, PrmShowOnly = 2 };
enum CaptureSource : uint8_t { ScsColor = 0, ScsFinalLdr = 2, ScsColorDepth = 3 };

struct UVTri {
    FVector2D p0, uv0;
    FLinearColor c0;
    FVector2D p1, uv1;
    FLinearColor c1;
    FVector2D p2, uv2;
    FLinearColor c2;
};
static_assert(sizeof(UVTri) == 0x90);

struct TextureP {
    UObject* tex;
    FVector2D pos, size, uv, uvsize;
    FLinearColor col;
    uint8_t blend;
    float rot;
    FVector2D pivot;
};
static_assert(offsetof(TextureP, col) == 0x48 && offsetof(TextureP, blend) == 0x58 && offsetof(TextureP, rot) == 0x5C);
static_assert(offsetof(TextureP, pivot) == 0x60 && sizeof(TextureP) == 0x70);

struct Blendable {
    float weight;
    UObject* object;
};

struct HitResult {
    uint64_t raw[0x20];
    const FVector& impact() const { return at<FVector>(raw, 0x28); }
};
static_assert(sizeof(HitResult) == 0x100);

struct Cls {
    UClass *character, *camera, *capture_actor, *ppv, *bc_char, *bc_ps, *scope, *common_widget, *image;
};

struct Fns {
    UFunction *text, *texture, *triangle, *text_size;
    UFunction *get_all, *spawn_begin, *spawn_finish, *destroy, *components, *attached;
    UFunction *create_rt, *clear_rt, *rt_begin, *rt_end, *import_tex, *export_rt;
    UFunction *to_name, *game_instance, *mpc_get, *mpc_set;
    UFunction *socket, *comp_loc, *comp_rot, *set_world_loc_rot;
    UFunction *bone_name, *num_bones, *los, *aim_rot, *line_trace, *sphere_trace, *implements, *has_tag;
    UFunction *show_only, *clear_show_only, *hide_actor, *clear_hidden, *show_flags, *capture_scene, *scope_active;
    UFunction *create_widget, *spawn_object, *add_viewport, *remove_parent, *in_viewport, *set_visibility, *set_brush;
};

struct Offs {
    int32_t hud_owner, hud_canvas, canvas_w, canvas_h;
    int32_t pc_cam, pc_pawn, ctrl_ps, cam_cache, cache_pov, pov_loc, pov_rot, pov_fov;
    int32_t actor_root, scene_loc, char_mesh, char_capsule, capsule_half;
    int32_t pawn_ps, bc_cached_ps, bc_set, set_hp, set_maxhp, attr_cur, ps_team, ps_name;
    int32_t font_size, font_face;
    int32_t cap_mode, cap_source, cap_persist, cap_fov, cap_target, cap_comp, cap_pps, cap_pp_weight, scope_mag;
    int32_t ppv_settings, ppv_priority, ppv_weight;
    int32_t widget_tree, tree_root;
    int32_t pps_bias, pps_vignette, pps_grain, pps_blur, pps_blendables, cam_pps;
    BoolProp cap_every_frame, cap_on_move, cap_unrendered, ppv_enabled, ppv_unbound;
    BoolProp ov_bias, ov_vignette, ov_grain, ov_blur;
};

extern Cls cls;
extern Fns fn;
extern Offs off;
extern UObject* font;

bool resolve(std::string& missing);

FString fstr(const wchar_t* s);
FName to_name(const wchar_t* s);

UObject* import_png(const std::vector<uint8_t>& png);
UObject* game_instance(UObject* ctx);
UObject* make_rt(UObject* outer, int w, int h, RtFormat fmt);
void drop_rt(UObject*& rt);
void clear_rt(UObject* ctx, UObject* rt, FLinearColor c);
void export_rt(UObject* ctx, UObject* rt, const wchar_t* dir, const wchar_t* file);
UObject* rt_begin(UObject* ctx, UObject* rt, UObject*& context);
void rt_end(UObject* ctx, UObject* context);

void blit(UObject* canvas, UObject* tex, double x, double y, double w, double h, FLinearColor col, Blend b, double u = 0, double v = 0, double uw = 1, double vh = 1);
void draw_tris(UObject* canvas, UObject* tex, const std::vector<UVTri>& t);

UObject* spawn_begin(UObject* ctx, UClass* c);
void spawn_finish(UObject* actor);
void destroy(UObject* actor);
TArray<UObject*> all_actors(UObject* ctx, UClass* c);
TArray<UObject*> components(UObject* actor, UClass* c);
TArray<UObject*> attached(UObject* actor, bool recursive = false);

FVector socket(UObject* comp, FName n);
bool line_trace(UObject* ctx, const FVector& from, const FVector& to, uint8_t channel, UObject* ignore, FVector& hit);
TArray<HitResult> sphere_trace(UObject* ctx, const FVector& from, const FVector& to, float radius, uint8_t channel, UObject* ignore);
bool has_tag(UObject* o, FName tag);
int num_bones(UObject* mesh);
FName bone_name(UObject* mesh, int i);
bool line_of_sight(UObject* ctrl, UObject* target, const FVector& from);
FRotator aim_rotation(UObject* pawn);
void set_world_loc_rot(UObject* comp, const FVector& loc, const FRotator& rot);
FVector comp_location(UObject* comp);
FRotator comp_rotation(UObject* comp);
bool scope_active(UObject* scope);

float mpc_get(UObject* ctx, UObject* collection, FName n);
void mpc_set(UObject* ctx, UObject* collection, FName n, float v);

inline TArray<Blendable>& blendables(UObject* camera) { return at<TArray<Blendable>>(camera, off.cam_pps + off.pps_blendables); }

void show_only(UObject* capture, UObject* actor);
void clear_show_only(UObject* capture);
void hide(UObject* capture, UObject* actor);
void clear_hidden(UObject* capture);
void show_flags_off(UObject* capture, const std::vector<const wchar_t*>& flags);
void capture_scene(UObject* capture);

UObject* create_widget(UObject* ctx, UClass* c);
UObject* spawn_object(UClass* c, UObject* outer);
void add_to_viewport(UObject* widget, int z);
void remove_from_parent(UObject* widget);
bool in_viewport(UObject* widget);
void set_visibility(UObject* widget, uint8_t v);
void set_brush(UObject* image, UObject* resource);

}
