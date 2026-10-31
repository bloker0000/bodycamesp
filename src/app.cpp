#include "app.hpp"
#include "chams.hpp"
#include "config.hpp"
#include "draw.hpp"
#include "esp.hpp"
#include "input.hpp"
#include "log.hpp"
#include "menu.hpp"
#include "overlay.hpp"
#include "sdk.hpp"
#include "view.hpp"
#include "visuals.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <string>

using namespace sdk;
using cfg::c;

namespace app {
namespace {

std::wstring dir;
bool ready, failed;
std::atomic<bool> unload_wanted, done;
UObject* last_hud;
const char* stage = "";
int faults, streak;
double last_frame, last_fault, paused_until;

double seconds() {
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return double(now.QuadPart) / freq.QuadPart;
}

double tick() {
    double now = seconds(), dt = last_frame > 0 ? now - last_frame : 0.016;
    last_frame = now;
    return std::min(dt, 0.1);
}

bool init() {
    std::string missing;
    if (!resolve(missing)) {
        log("resolve failed, missing: %s", missing.c_str());
        return false;
    }
    cfg::load(dir.c_str());
    log("resolved everything");
    return true;
}

void shutdown() {
    menu::close();
    view::shutdown();
    chams::shutdown();
    overlay::shutdown();
    draw::end();
    draw::shutdown();
    if (!input::uninstall()) log("window proc was replaced after ours, it stays installed");
    cfg::save();
    log("cleaned up");
}

void hotkeys() {
    if (menu::capturing()) return;
    if (input::pressed(c.k_menu)) menu::toggle();
    if (input::pressed(c.k_master)) c.master = !c.master;
    if (input::pressed(c.k_chams)) c.chams_on = !c.chams_on;
    if (input::pressed(c.k_radar)) c.radar = !c.radar;
    if (input::pressed(c.k_unload)) unload_wanted = true;
}

void perf(double dt, double start) {
    static double since, frames, total;
    total += seconds() - start;
    frames++;
    since += dt;
    if (since >= 10) {
        log("perf: %.0f fps, %.3f ms per frame in the dll, %zu targets, lens %.2f", frames / since, total / frames * 1000, esp::frame.targets.size(),
            esp::lens_on() ? esp::lens_strength() : 0.0);
        since = frames = total = 0;
    }
}

void run(UObject* hud) {
    if (done) return;
    double start = seconds();
    stage = "init";
    if (!ready) {
        if (failed) return;
        if (!(ready = init())) {
            failed = true;
            return;
        }
    }
    if (!alive(hud)) return;
    auto canvas = at<UObject*>(hud, off.hud_canvas);
    if (!canvas || !alive(canvas)) return;
    int w = at<int32_t>(canvas, off.canvas_w), h = at<int32_t>(canvas, off.canvas_h);
    if (w <= 0 || h <= 0) return;
    draw::begin(canvas, w, h);
    if (!draw::init()) return;
    double dt = tick();
    input::poll(w, h);

    auto pc = at<UObject*>(hud, off.hud_owner);
    if (pc && !alive(pc)) pc = nullptr;
    if (unload_wanted || menu::unload_requested()) {
        stage = "shutdown";
        shutdown();
        done = true;
        return;
    }
    if (hud != last_hud) {
        last_hud = hud;
        esp::reset();
        visuals::world_reset();
        chams::reset();
        overlay::reset();
        view::reset();
        log("new hud %s", name(hud).c_str());
    }
    hotkeys();

    // ctrl+f12 calibration capture, overlays hide for a bit for a clean screenshot
    static int calib;
    if (input::held(VK_CONTROL) && input::pressed(VK_F12)) calib = 60;
    if (calib > 0 && --calib == 50) {
        chams::calibrate(hud, pc ? at<UObject*>(pc, off.pc_pawn) : nullptr, dir.c_str());
        esp::dump_scope(hud, dir.c_str());
        overlay::dump(hud, dir.c_str());
    }

    stage = "esp";
    bool paused = start < paused_until || calib > 0;
    if (paused) esp::frame.targets.clear();
    if (!paused && pc && esp::update(hud, w, h)) {
        stage = "view";
        view::frame(hud, esp::frame.pawn, c.master);
        if (c.master) {
            stage = "world";
            visuals::world_update(hud);
            stage = "chams";
            chams::frame(hud);
            stage = "visuals";
            visuals::draw();
        }
    }
    stage = "menu";
    bool layered = menu::visible() && overlay::begin(hud, w, h);
    menu::frame(dt);
    if (layered) overlay::end(hud);
    else if (!menu::visible()) overlay::hide();
    draw::end();
    perf(dt, start);
    stage = "";
}

int filter(EXCEPTION_POINTERS* ep) {
    auto rec = ep->ExceptionRecord;
    auto addr = (uintptr_t)rec->ExceptionAddress;
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod);
    wchar_t path[MAX_PATH] = L"?";
    if (mod) GetModuleFileNameW(mod, path, MAX_PATH);
    const wchar_t* base = wcsrchr(path, L'\\');
    if (faults++ < 16)
        log("fault %08lx at %ls+%#llx (data %#llx) during %s", rec->ExceptionCode, base ? base + 1 : path,
            (unsigned long long)(addr - (uintptr_t)mod), rec->NumberParameters > 1 ? (unsigned long long)rec->ExceptionInformation[1] : 0ull, stage);
    return EXCEPTION_EXECUTE_HANDLER;
}

// engine state can be half done after a fault, dont hit it every frame
void recover() {
    esp::reset();
    visuals::world_reset();
    chams::reset();
    draw::end();
    double now = seconds();
    streak = now - last_fault < 2.0 ? streak + 1 : 1;
    last_fault = now;
    if (streak >= 3) {
        paused_until = now + 5;
        streak = 0;
        log("repeated faults, visuals paused for 5s");
    }
}

}

void set_dir(const wchar_t* d) { dir = d; }

void frame(UObject* hud) {
    __try {
        run(hud);
    } __except (filter(GetExceptionInformation())) {
        recover();
    }
}

void request_unload() { unload_wanted = true; }
bool wants_unload() { return unload_wanted || menu::unload_requested(); }
bool finished() { return done; }

}
