#ifndef E16_DISASSEMBLER_H
#define E16_DISASSEMBLER_H

#include "e16/memory.h"

#include <cstdint>
#include <string>
#include <vector>

namespace e16 {

struct DecodedInstruction {
    std::uint32_t address = 0;
    std::uint8_t size = 1;
    std::vector<std::uint8_t> bytes;
    std::string text;
    bool valid = true;
};

class Disassembler {
  public:
    explicit Disassembler(const Memory &memory);

    DecodedInstruction decode(std::uint32_t address) const;

  private:
    const Memory &memory;

    std::uint8_t read8(std::uint32_t address) const;
    std::uint16_t read16(std::uint32_t address) const;
    std::int16_t readS16(std::uint32_t address) const;
    std::uint32_t read24(std::uint32_t address) const;
    std::vector<std::uint8_t> bytes(std::uint32_t address,
                                    std::uint8_t count) const;
    std::string reg(std::uint8_t id) const;
    std::string special(std::uint8_t id) const;
    std::string byteList(const std::vector<std::uint8_t> &bytes) const;
    std::string memoryWithRegister(std::uint32_t address, std::uint8_t mode,
                                   std::uint8_t &regId,
                                   std::uint8_t &size) const;
    std::string unaryMemory(std::uint32_t address, std::uint8_t mode,
                            std::uint8_t &size) const;
};

}

#endif
