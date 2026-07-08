#ifndef E16_COMMON_H
#define E16_COMMON_H

#include <cstdint>
#include <string>
#include <vector>

namespace e16 {

constexpr std::uint32_t AddressMask = 0xFFFFFF;
constexpr std::uint32_t MemorySize = 0x1000000;
constexpr std::uint32_t DefaultLoadAddress = 0x200000;
constexpr std::uint32_t DefaultStackPointer = 0x040000;

constexpr std::uint16_t FlagZ = 1u << 0;
constexpr std::uint16_t FlagN = 1u << 1;
constexpr std::uint16_t FlagC = 1u << 2;
constexpr std::uint16_t FlagV = 1u << 3;
constexpr std::uint16_t FlagI = 1u << 4;

constexpr std::uint32_t FlameVramBase = 0x040000;
constexpr std::uint32_t FlameVramSize = 0x020000;
constexpr std::uint32_t FlameVramEnd = FlameVramBase + FlameVramSize - 1;
constexpr std::uint32_t AudioRamBase = 0x060000;
constexpr std::uint32_t AudioRamEnd = 0x067FFF;
constexpr std::uint32_t SaveRamBase = 0x100000;
constexpr std::uint32_t SaveRamEnd = 0x10FFFF;
constexpr std::uint32_t CartridgeRomBase = 0x200000;
constexpr std::uint32_t CartridgeRomEnd = 0x9FFFFF;
constexpr std::uint32_t BiosRomBase = 0xF00000;
constexpr std::uint32_t BiosRomEnd = 0xF0FFFF;
constexpr std::uint32_t MmioBase = 0xFF0000;
constexpr std::uint32_t MmioEnd = 0xFFFFFF;
constexpr std::uint32_t FlameMmioBase = 0xFF1000;
constexpr std::uint32_t FlameMmioEnd = 0xFF1FFF;

constexpr std::uint32_t DmaBase = 0xFF0300;
constexpr std::uint32_t DmaEnd = 0xFF03FF;
constexpr std::uint32_t ApuBase = 0xFF2000;
constexpr std::uint32_t ApuEnd = 0xFF2FFF;
constexpr std::uint32_t InputBase = 0xFF3000;
constexpr std::uint32_t InputEnd = 0xFF30FF;

constexpr int ScreenWidth = 320;
constexpr int ScreenHeight = 180;
constexpr int RefreshRate = 60;
constexpr int InstructionsPerFrame = 24000;

inline std::uint32_t mask24(std::uint32_t value) {
    return value & AddressMask;
}

inline std::uint16_t low16(std::uint32_t value) {
    return static_cast<std::uint16_t>(value & 0xFFFF);
}

inline std::int16_t asSigned16(std::uint16_t value) {
    return static_cast<std::int16_t>(value);
}

std::uint32_t parseNumber(const std::string &text);
std::string hex(std::uint32_t value, int width);
std::vector<std::uint8_t> readFile(const std::string &path);

}

#endif
