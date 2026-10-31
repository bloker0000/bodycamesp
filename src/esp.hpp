#pragma once
#include "config.hpp"
#include "sdk.hpp"

#include <string>
#include <vector>

namespace esp {
using namespace ue;

enum Bone { Head, Neck, Chest, Pelvis, LUp, LFore, LHand, RUp, RFore, RHand, LThigh, LCalf, LFoot, RThigh, RCalf, RFoot, BoneCount };
extern const Bone links[15][2];

struct View {
    FVector loc, fwd, right, up;
    FRotator rot;
    double fov, focal, cx, cy;
};

struct Target {
    UObject *actor, *mesh;
    int group;
    bool visible, has_hp, has_bones, on_screen;
    float hp, max_hp;
    double dist;
    FVector loc, head;
    FVector2D pts[BoneCount];
    bool on[BoneCount];
    double x0, y0, x1, y1;
    bool in_scope;
    std::wstring name, weapon;
};

struct Frame {
    UObject *hud, *pawn;
    std::vector<Target> targets;
};

extern View view;
extern Frame frame;

void axes(const FRotator& r, FVector& fwd, FVector& right, FVector& up);
bool w2s(const FVector& p, FVector2D& out);
bool w2s_scoped(const FVector& p, FVector2D& out);
bool lens_on();
double lens_strength();
FVector2D distort(FVector2D p);
UObject* held_item(UObject* actor);
bool update(UObject* hud, int w, int h);
void dump_scope(UObject* hud, const wchar_t* dir);
void reset();

}
