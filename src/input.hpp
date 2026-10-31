#pragma once
#include <string>

namespace input {

struct State {
    double mx, my, wheel;
    bool down, pressed, released, focused, virtual_cursor;
};

extern State s;

bool install();
bool uninstall();
bool installed();
void set_block(bool on);
void poll(double canvas_w, double canvas_h);
bool pressed(int vk);
bool held(int vk);
int first_pressed();
std::wstring key_name(int vk);

}
