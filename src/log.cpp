#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <string>

static FILE* out;

void log_open(void* module) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW((HMODULE)module, path, MAX_PATH);
    std::wstring p = path;
    p = p.substr(0, p.find_last_of(L'.')) + L".log";
    out = _wfsopen(p.c_str(), L"w", _SH_DENYNO);
}

void log_close() {
    if (out) fclose(out);
    out = nullptr;
}

void log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    if (out) {
        fprintf(out, "%s\n", buf);
        fflush(out);
    }
}
