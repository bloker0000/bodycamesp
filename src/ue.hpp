#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ue {

constexpr double pi = 3.14159265358979323846, deg2rad = pi / 180, rad2deg = 180 / pi;
constexpr int32_t rf_class_default = 0x10;

struct FName {
    uint32_t idx, num;
    bool operator==(const FName&) const = default;
};

template <class T> struct TArray {
    T* data;
    int32_t num, max;
};

struct FString {
    const wchar_t* data;
    int32_t num, max;
};

struct FVector {
    double x, y, z;
    FVector operator+(const FVector& o) const { return {x + o.x, y + o.y, z + o.z}; }
    FVector operator-(const FVector& o) const { return {x - o.x, y - o.y, z - o.z}; }
    FVector operator*(double k) const { return {x * k, y * k, z * k}; }
    double dot(const FVector& o) const { return x * o.x + y * o.y + z * o.z; }
    double length() const { return std::sqrt(dot(*this)); }
};
struct FVector2D { double x, y; };
struct FRotator { double pitch, yaw, roll; };
struct FLinearColor { float r, g, b, a; };

struct UClass;

struct UObject {
    void** vt;
    int32_t flags;
    int32_t index;
    UClass* cls;
    FName name;
    UObject* outer;
};

struct UClass : UObject {};
struct UFunction : UObject {};

struct BoolProp {
    int32_t offset = -1;
    uint8_t mask = 0;
    bool get(const void* obj) const { return offset >= 0 && (((const uint8_t*)obj)[offset] & mask); }
    void set(void* obj, bool on) const {
        auto& byte = ((uint8_t*)obj)[offset];
        byte = on ? byte | mask : byte & ~mask;
    }
};

// survives the object dying, get() returns null once its gone
struct Weak {
    UObject* ptr = nullptr;
    int32_t index = -1;
    UObject* get() const;
    static Weak of(UObject* o) { return o ? Weak{o, o->index} : Weak{}; }
};

template <class T> inline T& at(const void* p, ptrdiff_t off) { return *(T*)((uint8_t*)p + off); }

bool init(std::string& err);
void call(UObject* obj, UFunction* fn, void* parms);

std::string name(FName n);
std::string name(const UObject* o);
bool name_is(FName n, std::string_view s);

int32_t object_count();
UObject* object_at(int32_t i);
bool alive(const UObject* o);
UObject* weak_get(const void* weak);
void set_rooted(UObject* o, bool rooted);
UObject* find(std::string_view path);
UClass* find_class(std::string_view path);
bool is_a(const UObject* o, const UClass* c);
bool class_chain_has(const UClass* c, std::string_view name);

int vtable_len(const UObject* o);
bool code_has_disp(const void* fn, size_t window, int32_t disp);

int32_t prop_offset(const UClass* c, std::string_view name);
BoolProp bool_prop(const UClass* c, std::string_view name);
UFunction* find_fn(const UClass* c, std::string_view name);

}
