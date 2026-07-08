#ifndef E16_MEMORY_H
#define E16_MEMORY_H

#include "e16/common.h"
#include "e16/flame.h"

#include <array>
#include <cstdint>
#include <vector>

namespace e16 {

class Memory {
  public:
    explicit Memory(Flame &flame);

    void reset();
    void load(std::uint32_t address, const std::vector<std::uint8_t> &bytes);
    std::uint8_t read8(std::uint32_t address) const;
    std::uint16_t read16(std::uint32_t address) const;
    std::uint32_t read24(std::uint32_t address) const;
    void write8(std::uint32_t address, std::uint8_t value);
    void write16(std::uint32_t address, std::uint16_t value);
    void write24(std::uint32_t address, std::uint32_t value);
    void requestDma();

    std::uint16_t inputPad0 = 0;
    std::uint16_t inputPad1 = 0;

  private:
    Flame &flameDevice;
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 0x100> dma{};

    std::uint8_t readDma(std::uint32_t address) const;
    void writeDma(std::uint32_t address, std::uint8_t value);
    std::uint8_t readInput(std::uint32_t address) const;
};

}

#endif
