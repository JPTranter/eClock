// png_dump.cpp — 1-bit packed framebuffer -> grayscale PNG.

#include "png_dump.h"

#include <vector>
#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

bool dump_png(const char* path, const uint8_t* packed, int w, int h) {
    // Ensure the parent directory exists (tests may write to output/...).
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    const int stride = (w + 7) / 8;
    std::vector<uint8_t> gray(static_cast<size_t>(w) * h);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t bit = packed[y * stride + (x / 8)] & (0x80 >> (x & 7));
            gray[static_cast<size_t>(y) * w + x] = bit ? 255 : 0;
        }
    }

    return stbi_write_png(path, w, h, 1, gray.data(), w) != 0;
}
