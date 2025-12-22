#include "lodepng.h"
#include <cstdint>
#include <cstdio>
#include <vector>

// Declare the render function from hand_png_helper.cpp
namespace {
std::vector<uint8_t> render_i32_png(uint32_t value);
}

// Include the implementation directly to access the render function
#include "hand_png_helper.cpp"

int main() {
    uint32_t values[] = {
        0xDAF51234,  // The magic license value
        0x00000000,
        0xFFFFFFFF,
        0xDEADBEEF,
        0x12345678,
    };

    for (uint32_t val : values) {
        std::vector<uint8_t> png = render_i32_png(val);
        char filename[64];
        snprintf(filename, sizeof(filename), "png/sample_%08X.png", val);
        lodepng::save_file(png, filename);
        printf("Generated %s (%zu bytes)\n", filename, png.size());
    }

    return 0;
}
