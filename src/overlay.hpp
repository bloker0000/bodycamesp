#pragma once
#include "ue.hpp"

namespace overlay {

// draws in between land above the game's umg, false means they stay on the hud
bool begin(ue::UObject* hud, int w, int h);
void end(ue::UObject* hud);
void hide();
void dump(ue::UObject* hud, const wchar_t* dir);
void reset();
void shutdown();

}
