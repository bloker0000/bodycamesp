#include "tex.hpp"

#include <algorithm>
#include <cmath>

namespace tex {
namespace {

uint32_t crc_table[256];

uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFF) {
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320 ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

void be32(std::vector<uint8_t>& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back((uint8_t)(v >> s));
}

void chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
    be32(o, (uint32_t)data.size());
    size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), data.begin(), data.end());
    be32(o, crc(o.data() + start, o.size() - start) ^ 0xFFFFFFFF);
}

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

template <class F> Image shaded(int size, F alpha_at) {
    Image img{size, size, std::vector<float>(size * size)};
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) img.alpha[y * size + x] = alpha_at(x + 0.5f, y + 0.5f);
    return img;
}

}

// white rgba, alpha carries the shape. stored deflate keeps this tiny
std::vector<uint8_t> png(const Image& img) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < img.h; y++) {
        raw.push_back(0);
        for (int x = 0; x < img.w; x++) {
            raw.insert(raw.end(), {255, 255, 255});
            raw.push_back((uint8_t)std::lround(clamp01(img.alpha[y * img.w + x]) * 255));
        }
    }

    std::vector<uint8_t> z{0x78, 0x01};
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        size_t n = std::min<size_t>(raw.size() - pos, 65535);
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)n);
        z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)~n);
        z.push_back((uint8_t)(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
        if (!n) break;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) a = (a + v) % 65521, b = (b + a) % 65521;
    be32(z, b << 16 | a);

    std::vector<uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, img.w);
    be32(ihdr, img.h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

Image circle(int size) {
    float c = size / 2.f, r = c - 0.5f;
    return shaded(size, [=](float x, float y) { return clamp01(r - std::hypot(x - c, y - c) + 0.5f); });
}

// clear, solid, solid, clear. drawn at twice the thickness so the bilinear ramp is about 1px
Image line() {
    Image img{4, 4, std::vector<float>(16)};
    for (int x = 0; x < 4; x++) img.alpha[4 + x] = img.alpha[8 + x] = 1;
    return img;
}

Image blob(int size) {
    float c = size / 2.f;
    return shaded(size, [=](float x, float y) {
        float t = 1 - std::min(1.f, std::hypot(x - c, y - c) / c);
        return t * t * (3 - 2 * t);
    });
}

Image arrow(int size) {
    float s = (float)size;
    float ax = s / 2, ay = s * 0.1f, bx = s * 0.88f, by = s * 0.88f, cx = s * 0.12f, cy = s * 0.88f;
    auto edge = [](float px, float py, float x0, float y0, float x1, float y1) {
        float ex = x1 - x0, ey = y1 - y0, len = std::hypot(ex, ey);
        return ((px - x0) * ey - (py - y0) * ex) / len;
    };
    return shaded(size, [=](float x, float y) {
        return clamp01(0.5f - std::max({edge(x, y, ax, ay, bx, by), edge(x, y, bx, by, cx, cy), edge(x, y, cx, cy, ax, ay)}));
    });
}

Image vgrad(int h) {
    Image img{2, h, std::vector<float>(2 * h)};
    for (int y = 0; y < h; y++) img.alpha[y * 2] = img.alpha[y * 2 + 1] = (y + 0.5f) / h;
    return img;
}

}
