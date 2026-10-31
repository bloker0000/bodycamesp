#pragma once
#include "ue.hpp"

namespace app {

void set_dir(const wchar_t* dir);
void frame(ue::UObject* hud);
void request_unload();
bool wants_unload();
bool finished();

}
