#include "ue.hpp"
#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace ue {
namespace {

// ue 5.5.4 layouts, checked live against build 25505592
constexpr ptrdiff_t item_size = 0x18, item_flags = 8, item_serial = 0x10;
constexpr int32_t chunk_items = 64 * 1024;
constexpr int32_t ef_garbage = 1 << 21, ef_unreachable = 1 << 28, ef_root = 1 << 30;
constexpr ptrdiff_t st_super = 0x40, st_children = 0x48, st_props = 0x50;
constexpr ptrdiff_t fd_next = 0x28;
constexpr ptrdiff_t ff_next = 0x18, ff_name = 0x20;
constexpr ptrdiff_t pr_offset = 0x44, pr_extra = 0x70;

struct ObjArray {
    uint8_t** chunks;
    uint8_t* prealloc;
    int32_t max, num, max_chunks, num_chunks;
};

using ProcessEventFn = void(__fastcall*)(UObject*, UFunction*, void*);
using RootFn = bool(__fastcall*)(void* item, int32_t flags);

uint8_t** name_blocks;
ObjArray* objects;
ProcessEventFn process_event;
RootFn root_add, root_remove;

uint8_t* text_start;
size_t text_size;

std::vector<int> parse(const char* pat) {
    std::vector<int> out;
    for (const char* s = pat; *s;) {
        if (*s == ' ') { s++; continue; }
        if (*s == '?') { out.push_back(-1); while (*s == '?') s++; continue; }
        out.push_back((int)strtoul(s, (char**)&s, 16));
    }
    return out;
}

bool match(const uint8_t* p, const std::vector<int>& sig) {
    for (size_t j = 0; j < sig.size(); j++)
        if (sig[j] >= 0 && p[j] != sig[j]) return false;
    return true;
}

std::vector<const uint8_t*> hits(const char* pat) {
    auto sig = parse(pat);
    std::vector<const uint8_t*> out;
    const uint8_t* end = text_start + text_size - sig.size();
    for (const uint8_t* p = text_start; p < end; p++) {
        p = (const uint8_t*)memchr(p, sig[0], end - p);
        if (!p) break;
        if (match(p, sig)) out.push_back(p);
    }
    return out;
}

// null unless the pattern hits exactly once
const uint8_t* unique(const char* pat) {
    auto found = hits(pat);
    return found.size() == 1 ? found[0] : nullptr;
}

// every hit is a 7 byte rip relative load, most common target wins
uintptr_t vote(std::initializer_list<const char*> pats) {
    std::map<uintptr_t, int> votes;
    for (auto pat : pats)
        for (auto p : hits(pat)) {
            int32_t disp;
            memcpy(&disp, p + 3, 4);
            votes[(uintptr_t)p + 7 + disp]++;
        }
    uintptr_t best = 0;
    int n = 0;
    for (auto& [target, count] : votes)
        if (count > n) best = target, n = count;
    log("  vote -> %p (%d hits, %zu candidates)", (void*)best, n, votes.size());
    return best;
}

const uint8_t* name_entry(uint32_t idx) {
    return name_blocks[idx >> 16] + (idx & 0xFFFF) * 2;
}

bool in_text(const void* p) {
    return p >= text_start && p < text_start + text_size;
}

bool find_text_section() {
    auto base = (uint8_t*)GetModuleHandleW(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (!memcmp(sec->Name, ".text", 5)) {
            text_start = base + sec->VirtualAddress;
            text_size = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

bool find_process_event() {
    auto cdo = find("/Script/CoreUObject.Default__Object");
    if (!cdo) return false;
    auto native = parse("F7 ? B0 00 00 00 00 04 00 00");
    auto outparms = parse("F7 ? B0 00 00 00 00 00 40 00");
    auto has = [](const uint8_t* fn, size_t range, const std::vector<int>& sig) {
        for (size_t i = 0; i < range && in_text(fn + i + sig.size()); i++)
            if (match(fn + i, sig)) return true;
        return false;
    };
    for (int i = 0; i < 0x80; i++) {
        auto fn = (const uint8_t*)cdo->vt[i];
        if (!in_text(fn)) break;
        if (has(fn, 0x400, native) && has(fn, 0xF00, outparms)) {
            process_event = (ProcessEventFn)fn;
            log("  ProcessEvent vtable[%#x] = %p", i, fn);
            return true;
        }
    }
    return false;
}

uint8_t* item_at(int32_t i) {
    if (i < 0 || i >= objects->num) return nullptr;
    auto chunk = objects->chunks[i / chunk_items];
    return chunk ? chunk + (i % chunk_items) * item_size : nullptr;
}

UClass* super_of(const UClass* c) { return at<UClass*>(c, st_super); }

uint8_t* find_prop(const UClass* c, std::string_view n) {
    for (; c; c = super_of(c))
        for (auto f = at<uint8_t*>(c, st_props); f; f = at<uint8_t*>(f, ff_next))
            if (name_is(at<FName>(f, ff_name), n)) return f;
    return nullptr;
}

}

bool init(std::string& err) {
    if (!find_text_section()) return err = "no .text section", false;
    log(".text %p size %#zx", text_start, text_size);

    log("GUObjectArray:");
    objects = (ObjArray*)vote({"48 8B 05 ? ? ? ? 48 8B 0C C8 48 8D 04 D1", "48 8B 05 ? ? ? ? 48 8B 0C C8 4C 8D 04 D1"});
    if (!objects || !objects->chunks || !object_at(1) || object_at(1)->index != 1) return err = "GUObjectArray sig or sanity check failed", false;

    log("FNamePool:");
    auto pool = vote({"4C 8D 05 ? ? ? ? EB ? 48 8D 0D ? ? ? ? E8", "48 8D 05 ? ? ? ? EB ? 48 8D 0D ? ? ? ? E8", "48 8D 0D ? ? ? ? E8 ? ? ? ? C6 05 ? ? ? ? 01"});
    if (!pool) return err = "FNamePool sig failed", false;
    name_blocks = (uint8_t**)(pool + 0x10);
    if (!name_is({0, 0}, "None") || !name_is({3, 0}, "ByteProperty")) return err = "FNamePool sanity check failed", false;
    log("  %d objects, first name ok", objects->num);

    if (!find_process_event()) return err = "ProcessEvent not found", false;

    // AddToRoot and RemoveFromRoot, the flag alone doesnt make a gc root
    root_add = (RootFn)unique("40 53 56 48 83 EC 48 48 8B D9 48 89 7C 24 70 48 8D 0D ? ? ? ? 8B FA FF 15 ? ? ? ? 8B 43 08 A9 00 00 50 6E");
    root_remove = (RootFn)unique("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B D9 8B FA 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 8B 4B 08 0F BA E7 1D");
    log("  root set add %p remove %p", (void*)root_add, (void*)root_remove);
    if (!root_add || !root_remove) {
        root_add = root_remove = nullptr;
        log("  rooting falls back to the flag, gc will eat runtime textures");
    }
    return true;
}

void call(UObject* obj, UFunction* fn, void* parms) { process_event(obj, fn, parms); }

std::string name(FName n) {
    auto e = name_entry(n.idx);
    uint16_t hdr = *(const uint16_t*)e;
    int len = hdr >> 6;
    std::string s;
    if (hdr & 1) {
        auto w = (const wchar_t*)(e + 2);
        for (int i = 0; i < len; i++) s += w[i] < 0x80 ? (char)w[i] : '?';
    } else {
        s.assign((const char*)e + 2, len);
    }
    if (n.num) s += "_" + std::to_string(n.num - 1);
    return s;
}

std::string name(const UObject* o) { return o ? name(o->name) : "null"; }

bool name_is(FName n, std::string_view s) {
    if (n.num) return false;
    auto e = name_entry(n.idx);
    uint16_t hdr = *(const uint16_t*)e;
    if ((size_t)(hdr >> 6) != s.size()) return false;
    if (!(hdr & 1)) return !memcmp(e + 2, s.data(), s.size());
    auto w = (const wchar_t*)(e + 2);
    for (size_t i = 0; i < s.size(); i++)
        if (w[i] != (wchar_t)(uint8_t)s[i]) return false;
    return true;
}

int32_t object_count() { return objects->num; }

UObject* object_at(int32_t i) {
    auto item = item_at(i);
    return item ? *(UObject**)item : nullptr;
}

bool alive(const UObject* o) {
    if (!o) return false;
    auto item = item_at(o->index);
    return item && *(UObject**)item == o && !(at<int32_t>(item, item_flags) & (ef_garbage | ef_unreachable));
}

void set_rooted(UObject* o, bool rooted) {
    auto item = o ? item_at(o->index) : nullptr;
    if (!item || *(UObject**)item != o) return;
    if (auto fn = rooted ? root_add : root_remove) {
        fn(item, ef_root);
        return;
    }
    auto flags = (volatile long*)(item + item_flags);
    if (rooted) _InterlockedOr(flags, ef_root);
    else _InterlockedAnd(flags, ~ef_root);
}

UObject* Weak::get() const {
    return ptr && object_at(index) == ptr && alive(ptr) ? ptr : nullptr;
}

// FWeakObjectPtr, the serial says if the slot still holds the same object
UObject* weak_get(const void* weak) {
    int32_t index = at<int32_t>(weak, 0), serial = at<int32_t>(weak, 4);
    auto item = serial ? item_at(index) : nullptr;
    if (!item || at<int32_t>(item, item_serial) != serial) return nullptr;
    auto o = *(UObject**)item;
    return alive(o) ? o : nullptr;
}

UObject* find(std::string_view path) {
    std::vector<std::string_view> seg;
    for (size_t a = 0, b; a <= path.size(); a = b + 1) {
        b = path.find('.', a);
        if (b == path.npos) b = path.size();
        seg.push_back(path.substr(a, b - a));
    }
    for (int32_t i = 0, n = object_count(); i < n; i++) {
        auto o = object_at(i);
        if (!o || !name_is(o->name, seg.back())) continue;
        auto cur = o->outer;
        bool ok = true;
        for (int k = (int)seg.size() - 2; k >= 0 && ok; k--, cur = cur ? cur->outer : nullptr)
            ok = cur && name_is(cur->name, seg[k]);
        if (ok && !cur) return o;
    }
    return nullptr;
}

UClass* find_class(std::string_view path) { return (UClass*)find(path); }

bool is_a(const UObject* o, const UClass* c) {
    if (!o || !c) return false;
    for (auto k = o->cls; k; k = super_of(k))
        if (k == c) return true;
    return false;
}

bool class_chain_has(const UClass* c, std::string_view n) {
    for (; c; c = super_of(c))
        if (name_is(c->name, n)) return true;
    return false;
}

int vtable_len(const UObject* o) {
    int n = 0;
    while (in_text(o->vt[n])) n++;
    return n;
}

bool code_has_disp(const void* fn, size_t window, int32_t disp) {
    auto p = (const uint8_t*)fn;
    for (size_t i = 0; i < window && in_text(p + i + 4); i++)
        if (!memcmp(p + i, &disp, 4)) return true;
    return false;
}

int32_t prop_offset(const UClass* c, std::string_view n) {
    auto p = find_prop(c, n);
    return p ? at<int32_t>(p, pr_offset) : -1;
}

BoolProp bool_prop(const UClass* c, std::string_view n) {
    BoolProp b;
    if (auto p = find_prop(c, n)) {
        b.offset = at<int32_t>(p, pr_offset) + at<uint8_t>(p, pr_extra + 1);
        b.mask = at<uint8_t>(p, pr_extra + 2);
    }
    return b;
}

UFunction* find_fn(const UClass* c, std::string_view n) {
    for (; c; c = super_of(c))
        for (auto f = at<UObject*>(c, st_children); f; f = at<UObject*>(f, fd_next))
            if (name_is(f->name, n)) return (UFunction*)f;
    return nullptr;
}

}
