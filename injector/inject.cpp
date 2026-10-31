#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static DWORD find_pid(const wchar_t* exe) {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{sizeof e};
    for (BOOL ok = Process32FirstW(snap, &e); ok && !pid; ok = Process32NextW(snap, &e))
        if (!_wcsicmp(e.szExeFile, exe)) pid = e.th32ProcessID;
    CloseHandle(snap);
    return pid;
}

static int loaded_copies(DWORD pid) {
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    MODULEENTRY32W m{sizeof m};
    for (BOOL ok = Module32FirstW(snap, &m); ok; ok = Module32NextW(snap, &m))
        if (!_wcsnicmp(m.szModule, L"bcesp_live_", 11)) n++;
    CloseHandle(snap);
    return n;
}

static void unload_previous(DWORD pid) {
    wchar_t name[64];
    swprintf(name, 64, L"Local\\bcesp_unload_%lu", pid);
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (!ev) return;
    int before = loaded_copies(pid);
    SetEvent(ev);
    CloseHandle(ev);
    for (int i = 0; i < 100 && loaded_copies(pid) >= before; i++) Sleep(50);
    wprintf(L"unloaded previous copy (%d -> %d loaded)\n", before, loaded_copies(pid));
}

int wmain(int argc, wchar_t** argv) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    fs::path dir = fs::path(self).parent_path();
    fs::path src = argc > 1 ? fs::path(argv[1]) : dir / L"bcesp.dll";
    if (!fs::exists(src)) return wprintf(L"missing %ls\n", src.c_str()), 1;

    DWORD pid = find_pid(L"Bodycam-Win64-Shipping.exe");
    if (!pid) return wprintf(L"game not running\n"), 1;
    unload_previous(pid);

    // load a copy so rebuilding works while the old one is in the game
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().filename().wstring().starts_with(L"bcesp_live_") && e.path().extension() == L".dll") fs::remove(e.path(), ec);
    fs::path live = dir / (L"bcesp_live_" + std::to_wstring(GetTickCount64()) + L".dll");
    fs::copy_file(src, live, ec);
    if (ec) return wprintf(L"copy failed: %hs\n", ec.message().c_str()), 1;

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc) return wprintf(L"OpenProcess failed: %lu\n", GetLastError()), 1;

    std::wstring path = live.wstring();
    size_t bytes = (path.size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(proc, mem, path.c_str(), bytes, nullptr)) return wprintf(L"writing the path failed: %lu\n", GetLastError()), 1;
    auto load = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE t = CreateRemoteThread(proc, nullptr, 0, load, mem, 0, nullptr);
    if (!t) return wprintf(L"CreateRemoteThread failed: %lu\n", GetLastError()), 1;
    if (WaitForSingleObject(t, 15000) != WAIT_OBJECT_0) return wprintf(L"LoadLibraryW timed out\n"), 1;
    DWORD code = 0;
    GetExitCodeThread(t, &code);
    VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
    CloseHandle(t);
    CloseHandle(proc);
    if (!code) return wprintf(L"LoadLibraryW failed in target\n"), 1;
    wprintf(L"injected into %lu, log at %ls\n", pid, fs::path(live).replace_extension(L".log").c_str());
    return 0;
}
