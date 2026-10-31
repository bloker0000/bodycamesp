#include "draw.hpp"
#include "log.hpp"
#include "tex.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

namespace draw {
using namespace sdk;

double ui = 1, sw = 1920, sh = 1080, fade = 1;

namespace {

UObject* cv;
struct { UObject *circle, *line, *blob, *arrow, *vgrad; } t;
struct TextFont {
    UObject* obj;
    FName faces[3];
    int32_t base_size;
    FName base_face;
};
TextFont engine_font, game_font;
bool ready;

std::vector<TextureP> shapes;
bool recording;

struct TextP {
    UObject* font;
    FString text;
    FVector2D pos, scale;
    FLinearColor col;
    float kerning;
    FLinearColor shadow;
    FVector2D shadow_off;
    bool cx, cy, outlined;
    FLinearColor outline;
};
static_assert(offsetof(TextP, shadow_off) == 0x60 && offsetof(TextP, outline) == 0x74 && sizeof(TextP) == 0x88);

struct SizeP {
    UObject* font;
    FString text;
    FVector2D scale, out;
};
static_assert(offsetof(SizeP, out) == 0x28);

float to_linear(float v) {
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

void quad(UObject* tx, double x, double y, double w, double h, double u, double v, double uw, double vh, FLinearColor c, Blend b, float rot = 0) {
    c = linear(c);
    c.a *= (float)fade;
    if (c.a <= 0.002f || w <= 0 || h <= 0) return;
    TextureP p{tx, {x, y}, {w, h}, {u, v}, {uw, vh}, c, (uint8_t)b, rot, {0.5, 0.5}};
    call(cv, fn.texture, &p);
    if (recording) shapes.push_back(p);
}

auto textures() { return std::array{t.circle, t.line, t.blob, t.arrow, t.vgrad}; }

UObject* make(const tex::Image& img) {
    auto o = import_png(tex::png(img));
    if (o) set_rooted(o, true);
    return o;
}

TextFont make_font(UObject* o, const wchar_t* regular, const wchar_t* bold, const wchar_t* light) {
    return {o, {to_name(regular), to_name(bold), to_name(light)}, at<int32_t>(o, off.font_size), at<FName>(o, off.font_face)};
}

// names can be in any script, roboto covers more than the game font
UObject* set_font(const wchar_t* s, double pt, Face f) {
    auto k = game_font.obj ? &game_font : &engine_font;
    for (; *s; s++)
        if (*s >= 0x250) k = &engine_font;
    at<int32_t>(k->obj, off.font_size) = std::max(1, (int)std::lround(pt * ui));
    at<FName>(k->obj, off.font_face) = k->faces[f];
    return k->obj;
}

void restore(const TextFont& k) {
    if (!k.obj) return;
    at<int32_t>(k.obj, off.font_size) = k.base_size;
    at<FName>(k.obj, off.font_face) = k.base_face;
}

// rooted so a map change cant drop it, retried until its loaded
void find_game_font() {
    static int wait;
    if (game_font.obj && alive(game_font.obj)) return;
    game_font = {};
    if (wait-- > 0) return;
    wait = 300;
    auto o = find("/Game/UI/Fonts/ki/Ki_Font.Ki_Font");
    if (!o) return;
    set_rooted(o, true);
    game_font = make_font(o, L"Medium", L"Bold", L"Medium");
    log("text uses %s", name(o).c_str());
}

}

// checked every frame, rooting alone has let these die before
bool init() {
    static bool fonts;
    if (!fonts) {
        fonts = true;
        engine_font = make_font(font, L"Regular", L"Bold", L"Light");
    }
    find_game_font();
    auto all = textures();
    if (std::all_of(all.begin(), all.end(), [](UObject* o) { return alive(o); })) return true;
    if (ready) log("a texture died, rebuilding them");
    for (auto o : all)
        if (o) set_rooted(o, false);
    t.circle = make(tex::circle(128));
    t.line = make(tex::line());
    t.blob = make(tex::blob(64));
    t.arrow = make(tex::arrow(64));
    t.vgrad = make(tex::vgrad(64));
    ready = t.circle && t.line && t.blob && t.arrow && t.vgrad;
    log("textures %s", ready ? "ready" : "failed");
    return ready;
}

void shutdown() {
    for (auto o : textures())
        if (o) set_rooted(o, false);
    t = {};
    if (game_font.obj) set_rooted(game_font.obj, false);
    game_font = {};
}

void begin(UObject* canvas, int w, int h) {
    cv = canvas;
    sw = w;
    sh = h;
    ui = std::max(0.5, h / 1080.0);
    fade = 1;
    recording = false;
    shapes.clear();
}

void end() {
    restore(engine_font);
    restore(game_font);
}

UObject* canvas() { return cv; }
void set_canvas(UObject* canvas) { cv = canvas; }

void shapes_begin() {
    shapes.clear();
    recording = true;
}

// replays the shapes as holdouts, leaves alpha as what shows through
void shapes_end(UObject* canvas) {
    recording = false;
    for (auto& p : shapes) {
        p.blend = AlphaHoldout;
        call(canvas, fn.texture, &p);
    }
    shapes.clear();
}

FLinearColor rgba(float r, float g, float b, float a) { return {r, g, b, a}; }
FLinearColor alpha(FLinearColor c, float a) { return {c.r, c.g, c.b, c.a * a}; }
FLinearColor mix(FLinearColor a, FLinearColor b, float k) {
    return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, a.a + (b.a - a.a) * k};
}

// colors are picked as srgb, the canvas takes linear
FLinearColor linear(FLinearColor c) { return {to_linear(c.r), to_linear(c.g), to_linear(c.b), c.a}; }

void rect(double x, double y, double w, double h, FLinearColor c) {
    quad(nullptr, x, y, w, h, 0, 0, 1, 1, c, Translucent);
}

void rrect(double x, double y, double w, double h, double r, FLinearColor c) {
    r = std::min({r, w / 2, h / 2});
    if (r < 0.75) return rect(x, y, w, h, c);
    double x1 = x + r, x2 = x + w - r, y1 = y + r, y2 = y + h - r;
    rect(x1, y, x2 - x1, h, c);
    rect(x, y1, r, y2 - y1, c);
    rect(x2, y1, r, y2 - y1, c);
    quad(t.circle, x, y, r, r, 0, 0, 0.5, 0.5, c, Translucent);
    quad(t.circle, x2, y, r, r, 0.5, 0, 0.5, 0.5, c, Translucent);
    quad(t.circle, x, y2, r, r, 0, 0.5, 0.5, 0.5, c, Translucent);
    quad(t.circle, x2, y2, r, r, 0.5, 0.5, 0.5, 0.5, c, Translucent);
}

void gradient(double x, double y, double w, double h, FLinearColor top, FLinearColor bottom) {
    quad(t.vgrad, x, y, w, h, 0, 1, 1, -1, top, Translucent);
    quad(t.vgrad, x, y, w, h, 0, 0, 1, 1, bottom, Translucent);
}

// 9 slice of the blob, falloff runs outward from the rect edges
void shadow(double x, double y, double w, double h, double s, FLinearColor c) {
    double x2 = x + w, y2 = y + h;
    quad(t.blob, x - s, y - s, s, s, 0, 0, 0.5, 0.5, c, Translucent);
    quad(t.blob, x2, y - s, s, s, 0.5, 0, 0.5, 0.5, c, Translucent);
    quad(t.blob, x - s, y2, s, s, 0, 0.5, 0.5, 0.5, c, Translucent);
    quad(t.blob, x2, y2, s, s, 0.5, 0.5, 0.5, 0.5, c, Translucent);
    quad(t.blob, x, y - s, w, s, 0.5, 0, 0, 0.5, c, Translucent);
    quad(t.blob, x, y2, w, s, 0.5, 0.5, 0, 0.5, c, Translucent);
    quad(t.blob, x - s, y, s, h, 0, 0.5, 0.5, 0, c, Translucent);
    quad(t.blob, x2, y, s, h, 0.5, 0.5, 0.5, 0, c, Translucent);
    rect(x, y, w, h, c);
}

void circle(double cx, double cy, double r, FLinearColor c) {
    quad(t.circle, cx - r, cy - r, r * 2, r * 2, 0, 0, 1, 1, c, Translucent);
}

void blob(double cx, double cy, double r, FLinearColor c) {
    quad(t.blob, cx - r, cy - r, r * 2, r * 2, 0, 0, 1, 1, c, Translucent);
}

void line(FVector2D a, FVector2D b, FLinearColor c, double thick) {
    double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
    if (len < 0.01) return;
    double h = std::max(2.0, thick * 2);
    double deg = std::atan2(dy, dx) * rad2deg;
    quad(t.line, (a.x + b.x - len) / 2, (a.y + b.y - h) / 2, len, h, 0, 0, 1, 1, c, Translucent, (float)deg);
}

void ring(double cx, double cy, double r, FLinearColor c, double thick, int segments) {
    FVector2D prev{cx + r, cy};
    for (int i = 1; i <= segments; i++) {
        double a = i * 2 * pi / segments;
        FVector2D p{cx + std::cos(a) * r, cy + std::sin(a) * r};
        line(prev, p, c, thick);
        prev = p;
    }
}

void arrow(double cx, double cy, double size, double deg, FLinearColor c) {
    quad(t.arrow, cx - size / 2, cy - size / 2, size, size, 0, 0, 1, 1, c, Translucent, (float)deg);
}

void tris(const std::vector<UVTri>& v) {
    if (v.empty()) return;
    std::vector<UVTri> l(v);
    for (auto& tri : l) tri.c0 = linear(tri.c0), tri.c1 = linear(tri.c1), tri.c2 = linear(tri.c2);
    draw_tris(cv, nullptr, l);
}

FVector2D measure(const wchar_t* s, double pt, Face f) {
    auto obj = set_font(s, pt, f);
    SizeP p{obj, fstr(s), {1, 1}};
    call(cv, fn.text_size, &p);
    return p.out;
}

double text(const wchar_t* s, double x, double y, double pt, FLinearColor c, Align a, Style st, Face f) {
    if (!*s) return 0;
    double w = a == Left ? 0 : measure(s, pt, f).x;
    if (a == Center) x -= w / 2;
    if (a == Right) x -= w;
    auto obj = set_font(s, pt, f);
    c = linear(c);
    c.a *= (float)fade;
    if (c.a <= 0.002f) return w;
    TextP p{};
    p.font = obj;
    p.text = fstr(s);
    p.pos = {std::round(x), std::round(y)};
    p.scale = {1, 1};
    p.col = c;
    if (st == Shadow) {
        p.shadow = {0, 0, 0, 0.7f * c.a};
        p.shadow_off = {std::max(1.0, std::round(ui)), std::max(1.0, std::round(ui))};
    }
    if (st == Outline) {
        p.outlined = true;
        p.outline = {0, 0, 0, 0.9f * c.a};
    }
    call(cv, fn.text, &p);
    return w;
}

}
