#include "e16/memory.h"

#include <algorithm>
#include <stdexcept>

namespace e16 {

Memory::Memory(Flame &flame) : flameDevice(flame), bytes(MemorySize, 0) {}

void Memory::reset() {
    std::fill(bytes.begin(), bytes.end(), 0);
    dma.fill(0);
    inputPad0 = 0;
    inputPad1 = 0;
}

void Memory::load(std::uint32_t address,
                  const std::vector<std::uint8_t> &program) {
    address = mask24(address);
    if (program.size() > bytes.size() - address) {
        throw std::runtime_error("program does not fit in memory");
    }
    std::copy(program.begin(), program.end(), bytes.begin() + address);
}

std::uint8_t Memory::read8(std::uint32_t address) const {
    address = mask24(address);
    if (address >= FlameVramBase && address <= FlameVramEnd) {
        return flameDevice.readVram(address);
    }
    if (address >= FlameMmioBase && address <= FlameMmioEnd) {
        return flameDevice.readMmio(address);
    }
    if (address >= DmaBase && address <= DmaEnd) {
        return readDma(address);
    }
    if (address >= InputBase && address <= InputEnd) {
        return readInput(address);
    }
    return bytes[address];
}

std::uint16_t Memory::read16(std::uint32_t address) const {
    return static_cast<std::uint16_t>(read8(address)) |
           static_cast<std::uint16_t>(read8(address + 1) << 8);
}

std::uint32_t Memory::read24(std::uint32_t address) const {
    return static_cast<std::uint32_t>(read8(address)) |
           (static_cast<std::uint32_t>(read8(address + 1)) << 8) |
           (static_cast<std::uint32_t>(read8(address + 2)) << 16);
}

void Memory::write8(std::uint32_t address, std::uint8_t value) {
    address = mask24(address);
    if (address >= FlameVramBase && address <= FlameVramEnd) {
        flameDevice.writeVram(address, value);
        return;
    }
    if (address >= FlameMmioBase && address <= FlameMmioEnd) {
        flameDevice.writeMmio(address, value);
        return;
    }
    if (address >= DmaBase && address <= DmaEnd) {
        writeDma(address, value);
        return;
    }
    if (address >= InputBase && address <= InputEnd) {
        return;
    }
    bytes[address] = value;
}

void Memory::write16(std::uint32_t address, std::uint16_t value) {
    write8(address, static_cast<std::uint8_t>(value & 0xFF));
    write8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void Memory::write24(std::uint32_t address, std::uint32_t value) {
    write8(address, static_cast<std::uint8_t>(value & 0xFF));
    write8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFF));
    write8(address + 2, static_cast<std::uint8_t>((value >> 16) & 0xFF));
}

void Memory::requestDma() {
    std::uint32_t source = static_cast<std::uint32_t>(dma[0]) |
                           (static_cast<std::uint32_t>(dma[1]) << 8) |
                           (static_cast<std::uint32_t>(dma[2]) << 16);
    std::uint32_t dest = static_cast<std::uint32_t>(dma[3]) |
                         (static_cast<std::uint32_t>(dma[4]) << 8) |
                         (static_cast<std::uint32_t>(dma[5]) << 16);
    std::uint32_t length =
        static_cast<std::uint16_t>(dma[6]) |
        static_cast<std::uint16_t>(dma[7] << 8);
    if (length == 0) {
        length = 0x10000;
    }
    dma[10] |= 0x01;
    for (std::uint32_t i = 0; i < length; i++) {
        write8(dest + i, read8(source + i));
    }
    dma[8] = 0;
    dma[9] = 0;
    dma[10] &= static_cast<std::uint8_t>(~0x01);
    dma[10] |= 0x02;
}

std::uint8_t Memory::readDma(std::uint32_t address) const {
    return dma[(address - DmaBase) & 0x1F];
}

void Memory::writeDma(std::uint32_t address, std::uint8_t value) {
    std::uint32_t offset = (address - DmaBase) & 0x1F;
    dma[offset] = value;
    if (offset == 8 && (value & 0x01) != 0) {
        requestDma();
    }
}

std::uint8_t Memory::readInput(std::uint32_t address) const {
    std::uint32_t offset = address - InputBase;
    if (offset == 0) {
        return static_cast<std::uint8_t>(inputPad0 & 0xFF);
    }
    if (offset == 1) {
        return static_cast<std::uint8_t>((inputPad0 >> 8) & 0xFF);
    }
    if (offset == 2) {
        return static_cast<std::uint8_t>(inputPad1 & 0xFF);
    }
    if (offset == 3) {
        return static_cast<std::uint8_t>((inputPad1 >> 8) & 0xFF);
    }
    return 0;
}

}
