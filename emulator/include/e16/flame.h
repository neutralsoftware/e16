#ifndef E16_FLAME_H
#define E16_FLAME_H

#include "e16/common.h"

#include <array>
#include <cstdint>
#include <vector>

namespace e16 {

struct BackgroundLayer {
    std::uint16_t control = 0;
    std::uint32_t tilemapBase = 0;
    std::uint32_t tileBase = 0;
    std::uint16_t scrollX = 0;
    std::uint16_t scrollY = 0;
    std::uint16_t width = 64;
    std::uint16_t height = 32;
    std::uint8_t priority = 0;
    std::uint8_t paletteBase = 0;
};

struct Sprite {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint16_t tileIndex = 0;
    std::uint8_t palette = 0;
    std::uint8_t priority = 0;
    bool flipX = false;
    bool flipY = false;
    std::uint8_t size = 0;
    bool enabled = false;
};

class Flame {
  public:
    Flame();

    void reset();
    std::uint8_t readVram(std::uint32_t address) const;
    void writeVram(std::uint32_t address, std::uint8_t value);
    std::uint8_t readMmio(std::uint32_t address) const;
    void writeMmio(std::uint32_t address, std::uint8_t value);
    void renderFrame();
    bool consumeVblankInterrupt();

    const std::vector<std::uint8_t> &vram() const;
    const std::vector<std::uint32_t> &framebuffer() const;
    std::uint16_t control() const;
    std::uint16_t status() const;
    std::uint16_t frameCounter() const;
    std::uint16_t irqEnable() const;
    std::uint16_t irqStatus() const;
    Sprite sprite(std::size_t index) const;
    BackgroundLayer layer(std::size_t index) const;
    std::uint16_t paletteEntry(std::size_t index) const;

  private:
    std::array<std::uint8_t, 0x1000> mmio{};
    std::vector<std::uint8_t> vramBytes;
    std::vector<std::uint32_t> pixels;
    bool vblankPending = false;

    std::uint16_t mmio16(std::uint32_t offset) const;
    std::uint32_t mmio24(std::uint32_t offset) const;
    void writeMmio16(std::uint32_t offset, std::uint16_t value);
    void writeMmio24(std::uint32_t offset, std::uint32_t value);
    std::uint32_t rgb555(std::uint16_t value) const;
    BackgroundLayer readLayer(std::uint32_t base, std::size_t index) const;
    Sprite readSprite(std::size_t index) const;
    std::uint8_t tilePixel(std::uint32_t base, std::uint16_t tileIndex, int x,
                           int y) const;
    void putPixel(int x, int y, std::uint8_t palette, std::uint8_t color);
    void renderLayer(const BackgroundLayer &layer);
    void renderSprites(std::uint8_t priority);
    void renderBitmap();
};

}

#endif
