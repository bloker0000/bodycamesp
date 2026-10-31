#pragma once
#include "sdk.hpp"

namespace draw {
using ue::FLinearColor;
using ue::FVector2D;
using ue::UObject;

enum Align { Left, Center, Right };
enum Style { Plain, Shadow, Outline };
enum Face { Regular, Bold, Light };

extern double ui, sw, sh, fade;

bool init();
void shutdown();
void begin(UObject* canvas, int w, int h);
void end();
UObject* canvas();
void set_canvas(UObject* canvas);
void shapes_begin();
void shapes_end(UObject* canvas);

FLinearColor rgba(float r, float g, float b, float a = 1);
FLinearColor alpha(FLinearColor c, float a);
FLinearColor mix(FLinearColor a, FLinearColor b, float t);
FLinearColor linear(FLinearColor c);

void rect(double x, double y, double w, double h, FLinearColor c);
void rrect(double x, double y, double w, double h, double r, FLinearColor c);
void gradient(double x, double y, double w, double h, FLinearColor top, FLinearColor bottom);
void shadow(double x, double y, double w, double h, double spread, FLinearColor c);
void circle(double cx, double cy, double r, FLinearColor c);
void blob(double cx, double cy, double r, FLinearColor c);
void line(FVector2D a, FVector2D b, FLinearColor c, double thick);
void ring(double cx, double cy, double r, FLinearColor c, double thick, int segments = 32);
void arrow(double cx, double cy, double size, double deg, FLinearColor c);
void tris(const std::vector<sdk::UVTri>& t);

double text(const wchar_t* s, double x, double y, double pt, FLinearColor c, Align a = Left, Style st = Plain, Face f = Regular);
FVector2D measure(const wchar_t* s, double pt, Face f = Regular);

}
