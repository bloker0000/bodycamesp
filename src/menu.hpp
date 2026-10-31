#pragma once

namespace menu {

bool is_open();
bool visible();
bool capturing();
bool blocks_mouse(double x, double y);
void toggle();
void close();
void frame(double dt);
bool unload_requested();

}
