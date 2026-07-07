#ifndef E16_COMMON_H
#define E16_COMMON_H

#include <cstdint>
#include <string>
#include <vector>

namespace e16 {

constexpr std::uint32_t AddressMask = 0xFFFFFF;
constexpr std::uint32_t MemorySize = 0x1000000;
constexpr std::uint32_t DefaultLoadAddress = 0x200000;
constexpr std::uint32_t DefaultStackPointer = 0x03FFFE;

constexpr std::uint16_t FlagZ = 1u << 0;
constexpr std::uint16_t FlagN = 1u << 1;
constexpr std::uint16_t FlagC = 1u << 2;
constexpr std::uint16_t FlagV = 1u << 3;
constexpr std::uint16_t FlagI = 1u << 4;

constexpr std::uint32_t FlameVramBase = 0x040000;
constexpr std::uint32_t FlameVramSize = 0x020000;
constexpr std::uint32_t FlameVramEnd = FlameVramBase + FlameVramSize - 1;
constexpr std::uint32_t FlameMmioBase = 0xFF1000;
constexpr std::uint32_t FlameMmioEnd = 0xFF1FFF;

constexpr std::uint32_t DmaBase = 0xFF2000;
constexpr std::uint32_t DmaEnd = 0xFF201F;
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
