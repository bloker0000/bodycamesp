#pragma once
#include <cstdint>
#include <vector>

namespace tex {

struct Image {
    int w, h;
    std::vector<float> alpha;
};

std::vector<uint8_t> png(const Image& img);

Image circle(int size);
Image line();
Image blob(int size);
Image arrow(int size);
Image vgrad(int h);

}
