#pragma once

// ---------------------------------------------------------------------------
// png_dump.h — expand a GFXcanvas1 1-bit framebuffer to grayscale and write a
// PNG via stb_image_write. Bit set (1) = white = 255; bit clear (0) = black.
// ---------------------------------------------------------------------------

#include <cstdint>

// Write `w` x `h` 1-bit-packed (MSB-first, stride = (w+7)/8) framebuffer to
// `path` as an 8-bit grayscale PNG. Returns true on success.
bool dump_png(const char* path, const uint8_t* packed, int w, int h);
