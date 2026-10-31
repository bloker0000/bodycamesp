#include "app.hpp"
#include "input.hpp"
#include "log.hpp"
#include "ue.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cwchar>
#include <string>
#include <vector>

using PostRenderFn = void(__fastcall*)(ue::UObject*);

struct Slot {
    void** vtable;
    PostRenderFn original;
};

static HMODULE self;
static std::vector<Slot> slots;
static int slot_index = -1;
static std::atomic<int> inflight;
static std::atomic<bool> stopping;

static void __fastcall post_render(ue::UObject* hud) {
    inflight++;
    for (auto& s : slots)
        if (s.vtable == hud->vt) {
            s.original(hud);
            break;
        }
    if (!stopping) app::frame(hud);
    inflight--;
}

static bool write_slot(void** slot, void* value) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, value);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// only hud virtual touching Canvas, DebugCanvas and the bShowHUD byte
static int find_post_render(std::string& err) {
    using namespace ue;
    auto hud = find_class("/Script/Engine.HUD");
    auto hud_cdo = find("/Script/Engine.Default__HUD");
    auto actor_cdo = find("/Script/Engine.Default__Actor");
    if (!hud || !hud_cdo || !actor_cdo) return err = "HUD/Actor CDOs not found", -1;
    int32_t canvas_off = prop_offset(hud, "Canvas"), debug_off = prop_offset(hud, "DebugCanvas");
    auto show = bool_prop(hud, "bShowHUD");
    if (canvas_off < 0 || debug_off < 0 || show.offset < 0) return err = "HUD canvas props not found", -1;
    int found = -1;
    for (int i = vtable_len(actor_cdo), n = vtable_len(hud_cdo); i < n; i++) {
        auto f = hud_cdo->vt[i];
        if (code_has_disp(f, 0x140, canvas_off) && code_has_disp(f, 0x140, debug_off) && code_has_disp(f, 0x140, show.offset)) {
            if (found >= 0) return err = "PostRender match is ambiguous", -1;
            found = i;
        }
    }
    if (found < 0) err = "AHUD::PostRender not found";
    return found;
}

// blueprint huds run on their native parent's vtable, so this catches all of them
static bool hook(std::string& err) {
    slot_index = find_post_render(err);
    if (slot_index < 0) return false;
    auto hud = ue::find_class("/Script/Engine.HUD");
    for (int32_t i = 0, n = ue::object_count(); i < n; i++) {
        auto o = ue::object_at(i);
        if (!o || !(o->flags & ue::rf_class_default) || !ue::is_a(o, hud)) continue;
        bool known = false;
        for (auto& s : slots) known |= s.vtable == o->vt;
        if (!known) slots.push_back({o->vt, (PostRenderFn)o->vt[slot_index]});
    }
    if (slots.empty()) return err = "no hud vtables found", false;
    for (auto& s : slots)
        if (!write_slot(&s.vtable[slot_index], (void*)&post_render)) return err = "VirtualProtect on a hud vtable failed", false;
    log("PostRender slot %#x hooked in %zu hud vtables", slot_index, slots.size());
    return true;
}

static void unhook() {
    for (auto& s : slots) write_slot(&s.vtable[slot_index], (void*)s.original);
}

// unhooks and frees the dll, unless the game's window proc is stuck on us
static DWORD leave(DWORD code) {
    stopping = true;
    unhook();
    input::uninstall();
    while (inflight) Sleep(10);
    Sleep(100);
    if (input::installed()) {
        log("window proc still points at us, staying loaded");
        log_close();
        return code;
    }
    log("unloaded");
    log_close();
    FreeLibraryAndExitThread(self, code);
}

static DWORD WINAPI run(LPVOID) {
    log_open(self);
    log("bcesp loading");
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring dir = path;
    app::set_dir(dir.substr(0, dir.find_last_of(L"\\/") + 1).c_str());

    std::string err;
    if (!ue::init(err)) return log("init failed: %s", err.c_str()), leave(1);
    if (!input::install()) log("game window not found, no mouse wheel");
    if (!hook(err)) return log("hook failed: %s", err.c_str()), leave(1);

    // inject.exe sets this to swap in a fresh build
    wchar_t ev_name[64];
    swprintf(ev_name, 64, L"Local\\bcesp_unload_%lu", GetCurrentProcessId());
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, ev_name);

    while (!app::wants_unload() && WaitForSingleObject(ev, 50) == WAIT_TIMEOUT) {}
    app::request_unload();
    // cleanup has to run on the game thread, give it a few frames
    for (int i = 0; i < 60 && !app::finished(); i++) Sleep(50);
    log(app::finished() ? "game thread cleanup done" : "cleanup timed out");
    CloseHandle(ev);
    return leave(0);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        DisableThreadLibraryCalls(module);
        if (auto t = CreateThread(nullptr, 0, run, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
