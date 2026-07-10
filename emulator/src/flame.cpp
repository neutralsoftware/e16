#include "e16/flame.h"

#include <algorithm>

namespace e16 {

namespace {
constexpr std::uint32_t VpuControl = 0x000;
constexpr std::uint32_t VpuStatus = 0x002;
constexpr std::uint32_t VpuFrame = 0x004;
constexpr std::uint32_t VpuScanline = 0x006;
constexpr std::uint32_t VpuBackdrop = 0x008;
constexpr std::uint32_t VpuIrqEnable = 0x00A;
constexpr std::uint32_t VpuIrqStatus = 0x00C;
constexpr std::uint32_t Bg0Base = 0x100;
constexpr std::uint32_t Bg1Base = 0x120;
constexpr std::uint32_t Bg2Base = 0x140;
constexpr std::uint32_t WinBase = 0x160;
constexpr std::uint32_t OamBase = 0x200;
constexpr std::uint32_t PaletteBase = 0x800;
constexpr std::uint32_t BitmapBase = 0xA00;

int spriteSize(std::uint8_t id) {
    switch (id & 0x03) {
    case 0:
        return 8;
    case 1:
        return 16;
    case 2:
        return 32;
    default:
        return 64;
    }
}
}

Flame::Flame()
    : vramBytes(FlameVramSize, 0),
      pixels(ScreenWidth * ScreenHeight, 0xFF000000) {
    reset();
}

void Flame::reset() {
    mmio.fill(0);
    std::fill(vramBytes.begin(), vramBytes.end(), 0);
    std::fill(pixels.begin(), pixels.end(), 0xFF000000);
    vblankPending = false;
    writeMmio24(Bg0Base + 0x02, 0x04C000);
    writeMmio24(Bg0Base + 0x05, 0x040000);
    writeMmio16(Bg0Base + 0x0C, 64);
    writeMmio16(Bg0Base + 0x0E, 32);
    writeMmio24(Bg1Base + 0x02, 0x04E000);
    writeMmio24(Bg1Base + 0x05, 0x040000);
    writeMmio16(Bg1Base + 0x0C, 64);
    writeMmio16(Bg1Base + 0x0E, 32);
    writeMmio24(Bg2Base + 0x02, 0x050000);
    writeMmio24(Bg2Base + 0x05, 0x040000);
    writeMmio16(Bg2Base + 0x0C, 64);
    writeMmio16(Bg2Base + 0x0E, 32);
    writeMmio24(WinBase + 0x02, 0x052000);
    writeMmio24(WinBase + 0x05, 0x040000);
    writeMmio16(WinBase + 0x0C, 64);
    writeMmio16(WinBase + 0x0E, 32);
    writeMmio24(BitmapBase + 0x02, 0x054000);
    writeMmio16(BitmapBase + 0x05, 160);
    writeMmio16(BitmapBase + 0x07, 90);
    writeMmio16(BitmapBase + 0x09, 160);
    mmio[BitmapBase + 0x0B] = 1;
}

std::uint8_t Flame::readVram(std::uint32_t address) const {
    return vramBytes[(address - FlameVramBase) % FlameVramSize];
}

void Flame::writeVram(std::uint32_t address, std::uint8_t value) {
    vramBytes[(address - FlameVramBase) % FlameVramSize] = value;
}

std::uint8_t Flame::readMmio(std::uint32_t address) const {
    std::uint32_t offset = (address - FlameMmioBase) & 0x0FFF;
    return mmio[offset];
}

void Flame::writeMmio(std::uint32_t address, std::uint8_t value) {
    std::uint32_t offset = (address - FlameMmioBase) & 0x0FFF;
    if (offset == VpuFrame || offset == VpuFrame + 1 ||
        offset == VpuScanline || offset == VpuScanline + 1) {
        return;
    }
    if (offset == VpuStatus || offset == VpuStatus + 1) {
        std::uint16_t current = mmio16(VpuStatus);
        std::uint16_t clear = offset == VpuStatus
                                  ? value
                                  : static_cast<std::uint16_t>(value << 8);
        writeMmio16(VpuStatus, current & static_cast<std::uint16_t>(~clear));
        return;
    }
    if (offset == VpuIrqStatus || offset == VpuIrqStatus + 1) {
        std::uint16_t current = mmio16(VpuIrqStatus);
        std::uint16_t clear =
            offset == VpuIrqStatus ? value
                                   : static_cast<std::uint16_t>(value << 8);
        writeMmio16(VpuIrqStatus,
                    current & static_cast<std::uint16_t>(~clear));
        return;
    }
    mmio[offset] = value;
}

void Flame::renderFrame() {
    writeMmio16(VpuStatus, mmio16(VpuStatus) & static_cast<std::uint16_t>(~1u));
    std::uint32_t back = rgb555(mmio16(VpuBackdrop));
    std::fill(pixels.begin(), pixels.end(), back);

    std::uint16_t ctrl = mmio16(VpuControl);
    bool enabled = (ctrl & 0x01) != 0;
    bool display = (ctrl & 0x02) != 0;
    bool bgEnabled = (ctrl & 0x04) != 0;
    bool spriteEnabled = (ctrl & 0x08) != 0;
    bool bitmapEnabled = (ctrl & 0x10) != 0;
    bool windowEnabled = (ctrl & 0x20) != 0;
    bool forceBlank = (ctrl & 0x40) != 0;

    if (enabled && display && !forceBlank) {
        if (bgEnabled) {
            renderLayer(readLayer(Bg0Base, 0));
            renderLayer(readLayer(Bg1Base, 1));
        }
        if (spriteEnabled) {
            renderSprites(0);
        }
        if (bgEnabled) {
            renderLayer(readLayer(Bg2Base, 2));
        }
        if (spriteEnabled) {
            renderSprites(1);
        }
        if (windowEnabled) {
            renderLayer(readLayer(WinBase, 3));
        }
        if (bitmapEnabled) {
            renderBitmap();
        }
        if (spriteEnabled) {
            renderSprites(2);
            renderSprites(3);
        }
    }

    std::uint16_t frame = static_cast<std::uint16_t>(mmio16(VpuFrame) + 1);
    writeMmio16(VpuFrame, frame);
    writeMmio16(VpuScanline, ScreenHeight);
    writeMmio16(VpuStatus, mmio16(VpuStatus) | 0x0005);
    writeMmio16(VpuIrqStatus, mmio16(VpuIrqStatus) | 0x0009);
    if ((mmio16(VpuIrqEnable) & 0x0001) != 0) {
        vblankPending = true;
    }
}

bool Flame::consumeVblankInterrupt() {
    bool pending = vblankPending;
    vblankPending = false;
    return pending;
}

const std::vector<std::uint8_t> &Flame::vram() const {
    return vramBytes;
}

const std::vector<std::uint32_t> &Flame::framebuffer() const {
    return pixels;
}

std::uint16_t Flame::control() const {
    return mmio16(VpuControl);
}

std::uint16_t Flame::status() const {
    return mmio16(VpuStatus);
}

std::uint16_t Flame::frameCounter() const {
    return mmio16(VpuFrame);
}

std::uint16_t Flame::irqEnable() const {
    return mmio16(VpuIrqEnable);
}

std::uint16_t Flame::irqStatus() const {
    return mmio16(VpuIrqStatus);
}

Sprite Flame::sprite(std::size_t index) const {
    return readSprite(index);
}

BackgroundLayer Flame::layer(std::size_t index) const {
    if (index == 0) {
        return readLayer(Bg0Base, 0);
    }
    if (index == 1) {
        return readLayer(Bg1Base, 1);
    }
    if (index == 2) {
        return readLayer(Bg2Base, 2);
    }
    return readLayer(WinBase, 3);
}

std::uint16_t Flame::paletteEntry(std::size_t index) const {
    index %= 256;
    return static_cast<std::uint16_t>(mmio[PaletteBase + index * 2]) |
           static_cast<std::uint16_t>(mmio[PaletteBase + index * 2 + 1] << 8);
}

std::uint16_t Flame::mmio16(std::uint32_t offset) const {
    return static_cast<std::uint16_t>(mmio[offset]) |
           static_cast<std::uint16_t>(mmio[offset + 1] << 8);
}

std::uint32_t Flame::mmio24(std::uint32_t offset) const {
    return static_cast<std::uint32_t>(mmio[offset]) |
           (static_cast<std::uint32_t>(mmio[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(mmio[offset + 2]) << 16);
}

void Flame::writeMmio16(std::uint32_t offset, std::uint16_t value) {
    mmio[offset] = static_cast<std::uint8_t>(value & 0xFF);
    mmio[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void Flame::writeMmio24(std::uint32_t offset, std::uint32_t value) {
    mmio[offset] = static_cast<std::uint8_t>(value & 0xFF);
    mmio[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    mmio[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
}

std::uint32_t Flame::rgb555(std::uint16_t value) const {
    std::uint8_t b = static_cast<std::uint8_t>((value & 0x001F) << 3);
    std::uint8_t g = static_cast<std::uint8_t>(((value >> 5) & 0x001F) << 3);
    std::uint8_t r = static_cast<std::uint8_t>(((value >> 10) & 0x001F) << 3);
    r |= r >> 5;
    g |= g >> 5;
    b |= b >> 5;
    return 0xFF000000u | (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | b;
}

BackgroundLayer Flame::readLayer(std::uint32_t base, std::size_t index) const {
    BackgroundLayer layer;
    layer.control = mmio16(base + 0x00);
    layer.tilemapBase = mmio24(base + 0x02);
    layer.tileBase = mmio24(base + 0x05);
    layer.scrollX = mmio16(base + 0x08);
    layer.scrollY = mmio16(base + 0x0A);
    layer.width = mmio16(base + 0x0C);
    layer.height = mmio16(base + 0x0E);
    layer.priority = mmio[base + 0x10];
    layer.paletteBase = mmio[base + 0x11];
    if (layer.width == 0) {
        layer.width = 64;
    }
    if (layer.height == 0) {
        layer.height = 32;
    }
    if (layer.tilemapBase == 0) {
        layer.tilemapBase = 0x04C000 + static_cast<std::uint32_t>(index) * 0x2000;
    }
    if (layer.tileBase == 0) {
        layer.tileBase = 0x040000;
    }
    return layer;
}

Sprite Flame::readSprite(std::size_t index) const {
    Sprite sprite;
    index %= 128;
    std::uint32_t base = OamBase + static_cast<std::uint32_t>(index) * 8;
    sprite.x = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(mmio[base]) |
        static_cast<std::uint16_t>(mmio[base + 1] << 8));
    sprite.y = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(mmio[base + 2]) |
        static_cast<std::uint16_t>(mmio[base + 3] << 8));
    sprite.tileIndex = static_cast<std::uint16_t>(mmio[base + 4]) |
                       static_cast<std::uint16_t>(mmio[base + 5] << 8);
    std::uint8_t attr = mmio[base + 6];
    std::uint8_t ctrl = mmio[base + 7];
    sprite.palette = attr & 0x0F;
    sprite.flipX = (attr & 0x10) != 0;
    sprite.flipY = (attr & 0x20) != 0;
    sprite.priority = (attr >> 6) & 0x03;
    sprite.size = ctrl & 0x03;
    sprite.enabled = (ctrl & 0x04) != 0;
    return sprite;
}

std::uint8_t Flame::tilePixel(std::uint32_t base, std::uint16_t tileIndex,
                              int x, int y) const {
    std::uint32_t offset = (base - FlameVramBase) + tileIndex * 32u +
                           static_cast<std::uint32_t>(y * 4 + x / 2);
    std::uint8_t value = vramBytes[offset % FlameVramSize];
    return (x & 1) == 0 ? static_cast<std::uint8_t>(value >> 4)
                        : static_cast<std::uint8_t>(value & 0x0F);
}

void Flame::putPixel(int x, int y, std::uint8_t palette, std::uint8_t color) {
    if (x < 0 || y < 0 || x >= ScreenWidth || y >= ScreenHeight) {
        return;
    }
    std::uint16_t entry = paletteEntry((palette & 0x0F) * 16 + (color & 0x0F));
    pixels[static_cast<std::size_t>(y * ScreenWidth + x)] = rgb555(entry);
}

void Flame::renderLayer(const BackgroundLayer &layer) {
    if ((layer.control & 0x01) == 0) {
        return;
    }
    bool transparent = (layer.control & 0x02) != 0;
    bool wrapX = (layer.control & 0x04) != 0;
    bool wrapY = (layer.control & 0x08) != 0;
    int mapWidth = std::max<int>(1, layer.width);
    int mapHeight = std::max<int>(1, layer.height);

    for (int sy = 0; sy < ScreenHeight; sy++) {
        int worldY = sy + layer.scrollY;
        int tileY = worldY / 8;
        int pixelY = worldY % 8;
        if (wrapY) {
            tileY %= mapHeight;
        }
        if (tileY < 0 || tileY >= mapHeight) {
            continue;
        }
        for (int sx = 0; sx < ScreenWidth; sx++) {
            int worldX = sx + layer.scrollX;
            int tileX = worldX / 8;
            int pixelX = worldX % 8;
            if (wrapX) {
                tileX %= mapWidth;
            }
            if (tileX < 0 || tileX >= mapWidth) {
                continue;
            }
            std::uint32_t entryAddr =
                (layer.tilemapBase - FlameVramBase) +
                static_cast<std::uint32_t>((tileY * mapWidth + tileX) * 2);
            std::uint16_t entry =
                static_cast<std::uint16_t>(vramBytes[entryAddr % FlameVramSize]) |
                static_cast<std::uint16_t>(
                    vramBytes[(entryAddr + 1) % FlameVramSize] << 8);
            std::uint16_t tile = entry & 0x03FF;
            std::uint8_t palette =
                static_cast<std::uint8_t>((entry >> 10) & 0x0F);
            int sampleX = pixelX;
            int sampleY = pixelY;
            if ((entry & 0x4000) != 0) {
                sampleX = 7 - sampleX;
            }
            if ((entry & 0x8000) != 0) {
                sampleY = 7 - sampleY;
            }
            std::uint8_t color = tilePixel(layer.tileBase, tile, sampleX, sampleY);
            if (transparent && color == 0) {
                continue;
            }
            putPixel(sx, sy, palette + layer.paletteBase, color);
        }
    }
}

void Flame::renderSprites(std::uint8_t priority) {
    for (std::size_t i = 0; i < 128; i++) {
        Sprite spr = readSprite(i);
        if (!spr.enabled || spr.priority != priority) {
            continue;
        }
        int size = spriteSize(spr.size);
        for (int ly = 0; ly < size; ly++) {
            int sy = spr.y + ly;
            if (sy < 0 || sy >= ScreenHeight) {
                continue;
            }
            for (int lx = 0; lx < size; lx++) {
                int sx = spr.x + lx;
                if (sx < 0 || sx >= ScreenWidth) {
                    continue;
                }
                int px = spr.flipX ? size - 1 - lx : lx;
                int py = spr.flipY ? size - 1 - ly : ly;
                int tileX = px / 8;
                int tileY = py / 8;
                int localX = px % 8;
                int localY = py % 8;
                int tilesPerRow = size / 8;
                std::uint16_t tile =
                    static_cast<std::uint16_t>(spr.tileIndex +
                                               tileY * tilesPerRow + tileX);
                std::uint8_t color = tilePixel(0x048000, tile, localX, localY);
                if (color == 0) {
                    continue;
                }
                putPixel(sx, sy, spr.palette, color);
            }
        }
    }
}

void Flame::renderBitmap() {
    std::uint16_t control = mmio16(BitmapBase);
    if ((control & 0x01) == 0) {
        return;
    }
    bool transparent = (control & 0x08) != 0;
    std::uint32_t base = mmio24(BitmapBase + 0x02);
    int width = std::max<int>(1, mmio16(BitmapBase + 0x05));
    int height = std::max<int>(1, mmio16(BitmapBase + 0x07));
    int stride = std::max<int>(width, mmio16(BitmapBase + 0x09));
    int scale = std::max<int>(1, mmio[BitmapBase + 0x0B] + 1);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            std::uint32_t offset =
                (base - FlameVramBase) + static_cast<std::uint32_t>(y * stride + x);
            std::uint8_t color = vramBytes[offset % FlameVramSize];
            if (transparent && color == 0) {
                continue;
            }
            std::uint16_t entry = paletteEntry(color);
            std::uint32_t rgb = rgb555(entry);
            for (int dy = 0; dy < scale; dy++) {
                int sy = y * scale + dy;
                if (sy >= ScreenHeight) {
                    continue;
                }
                for (int dx = 0; dx < scale; dx++) {
                    int sx = x * scale + dx;
                    if (sx < ScreenWidth) {
                        pixels[static_cast<std::size_t>(sy * ScreenWidth + sx)] =
                            rgb;
                    }
                }
            }
        }
    }
}

}
