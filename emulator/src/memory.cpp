#include "e16/memory.h"

#include <algorithm>
#include <stdexcept>

namespace e16 {

namespace {
bool inRange(std::uint32_t address, std::uint32_t start, std::uint32_t end) {
    return address >= start && address <= end;
}

bool isBackedMemory(std::uint32_t address) {
    return inRange(address, 0x000000, 0x03FFFF) ||
           inRange(address, AudioRamBase, AudioRamEnd) ||
           inRange(address, CartridgeRomBase, CartridgeRomEnd) ||
           inRange(address, BiosRomBase, BiosRomEnd);
}
}

Memory::Memory(Flame &flame, Apu &apu)
    : flameDevice(flame), apuDevice(apu), bytes(MemorySize, 0) {}

void Memory::reset() {
    std::fill(bytes.begin(), bytes.end(), 0);
    dma.fill(0);
    inputPad0 = 0;
    inputPad1 = 0;
    inputPad1Read = false;
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
    if (inRange(address, FlameVramBase, FlameVramEnd)) {
        return flameDevice.readVram(address);
    }
    if (inRange(address, AudioRamBase, AudioRamEnd)) {
        return apuDevice.readAudioRam(address);
    }
    if (inRange(address, DmaBase, DmaEnd)) {
        return readDma(address);
    }
    if (inRange(address, FlameMmioBase, FlameMmioEnd)) {
        return flameDevice.readMmio(address);
    }
    if (inRange(address, ApuBase, ApuEnd)) {
        return apuDevice.readMmio(address);
    }
    if (inRange(address, InputBase, InputEnd)) {
        return readInput(address);
    }
    if (inRange(address, MmioBase, MmioEnd)) {
        return 0;
    }
    if (!isBackedMemory(address)) {
        return 0;
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
    if (inRange(address, FlameVramBase, FlameVramEnd)) {
        flameDevice.writeVram(address, value);
        return;
    }
    if (inRange(address, AudioRamBase, AudioRamEnd)) {
        apuDevice.writeAudioRam(address, value);
        return;
    }
    if (inRange(address, DmaBase, DmaEnd)) {
        writeDma(address, value);
        return;
    }
    if (inRange(address, FlameMmioBase, FlameMmioEnd)) {
        flameDevice.writeMmio(address, value);
        return;
    }
    if (inRange(address, ApuBase, ApuEnd)) {
        apuDevice.writeMmio(address, value);
        return;
    }
    if (inRange(address, InputBase, InputEnd)) {
        return;
    }
    if (inRange(address, MmioBase, MmioEnd)) {
        return;
    }
    if (inRange(address, SaveRamBase, SaveRamEnd) ||
        inRange(address, CartridgeRomBase, CartridgeRomEnd) ||
        inRange(address, BiosRomBase, BiosRomEnd) ||
        !isBackedMemory(address)) {
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
    dma[9] |= 0x01;
    for (std::uint32_t i = 0; i < length; i++) {
        write8(dest + i, read8(source + i));
    }
    dma[8] = 0;
    dma[9] &= static_cast<std::uint8_t>(~0x01);
    dma[9] |= 0x02;
}

bool Memory::consumeInputPad1Read() {
    bool wasRead = inputPad1Read;
    inputPad1Read = false;
    return wasRead;
}

std::uint8_t Memory::readDma(std::uint32_t address) const {
    return dma[address - DmaBase];
}

void Memory::writeDma(std::uint32_t address, std::uint8_t value) {
    std::uint32_t offset = address - DmaBase;
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
        inputPad1Read = true;
        return static_cast<std::uint8_t>(inputPad1 & 0xFF);
    }
    if (offset == 3) {
        inputPad1Read = true;
        return static_cast<std::uint8_t>((inputPad1 >> 8) & 0xFF);
    }
    return 0;
}

}
