#pragma once
#include "ue.hpp"

namespace chams {

void frame(ue::UObject* hud);
void calibrate(ue::UObject* hud, ue::UObject* pawn, const wchar_t* dir);
void reset();
void shutdown();

}
