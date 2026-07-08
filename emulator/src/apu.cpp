#include "e16/apu.h"

#include <algorithm>
#include <cmath>

namespace e16 {

namespace {
constexpr std::uint32_t ApuControl = 0x000;
constexpr std::uint32_t ApuStatus = 0x002;
constexpr std::uint32_t ApuMasterVolume = 0x004;
constexpr std::uint32_t ApuMasterPan = 0x006;
constexpr std::uint32_t ApuFrameCounter = 0x008;
constexpr std::uint32_t ApuIrqEnable = 0x00A;
constexpr std::uint32_t ApuIrqStatus = 0x00C;
constexpr std::uint32_t ChannelBase = 0x100;
constexpr std::uint32_t ChannelSize = 0x20;

constexpr std::uint32_t ChannelControl = 0x00;
constexpr std::uint32_t ChannelStatus = 0x02;
constexpr std::uint32_t ChannelFrequency = 0x04;
constexpr std::uint32_t ChannelVolume = 0x06;
constexpr std::uint32_t ChannelPan = 0x08;
constexpr std::uint32_t ChannelLength = 0x0A;
constexpr std::uint32_t ChannelEnvelope = 0x0C;
constexpr std::uint32_t ChannelParam = 0x0E;
constexpr std::uint32_t ChannelBaseAddress = 0x10;

constexpr std::uint16_t ControlEnable = 1u << 0;
constexpr std::uint16_t ControlOutputEnable = 1u << 1;
constexpr std::uint16_t ControlMute = 1u << 2;
constexpr std::uint16_t ControlResetChannels = 1u << 3;
constexpr std::uint16_t ControlFrameSequencer = 1u << 4;
constexpr std::uint16_t ControlPcmEnable = 1u << 5;

constexpr std::uint16_t ChannelEnable = 1u << 0;
constexpr std::uint16_t ChannelTrigger = 1u << 1;
constexpr std::uint16_t ChannelLoop = 1u << 2;
constexpr std::uint16_t ChannelLengthEnable = 1u << 3;

constexpr std::uint16_t ChannelStatusActive = 1u << 0;
constexpr std::uint16_t ChannelStatusFinished = 1u << 1;

constexpr std::uint16_t IrqFrameTick = 1u << 0;
constexpr std::uint16_t IrqPcmComplete = 1u << 1;
constexpr std::uint16_t IrqInvalidAccess = 1u << 4;

std::uint16_t setByte(std::uint16_t value, std::uint32_t offset,
                      std::uint8_t byte) {
    if ((offset & 1u) == 0) {
        return static_cast<std::uint16_t>((value & 0xFF00u) | byte);
    }
    return static_cast<std::uint16_t>((value & 0x00FFu) |
                                      (static_cast<std::uint16_t>(byte) << 8));
}

std::uint8_t getByte(std::uint16_t value, std::uint32_t offset) {
    if ((offset & 1u) == 0) {
        return static_cast<std::uint8_t>(value & 0xFFu);
    }
    return static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

float normalizedByte(std::uint8_t value) {
    return (static_cast<float>(value) - 128.0f) / 128.0f;
}
}

Apu::Apu() : audioRam(AudioRamEnd - AudioRamBase + 1, 0) { reset(); }

void Apu::reset() {
    std::lock_guard lock(mutex);
    control = 0;
    status = 0;
    masterVolume = 0xFFFF;
    masterPan = 0xFFFF;
    frameCounter = 0;
    irqEnable = 0;
    irqStatus = 0;
    interruptPending = false;
    std::fill(audioRam.begin(), audioRam.end(), 0);
    resetChannels();
}

std::uint8_t Apu::readAudioRam(std::uint32_t address) const {
    std::lock_guard lock(mutex);
    std::uint32_t offset = 0;
    if (!audioRamOffset(address, offset)) {
        return 0;
    }
    return audioRam[offset];
}

void Apu::writeAudioRam(std::uint32_t address, std::uint8_t value) {
    std::lock_guard lock(mutex);
    std::uint32_t offset = 0;
    if (!audioRamOffset(address, offset)) {
        return;
    }
    audioRam[offset] = value;
}

std::uint8_t Apu::readMmio(std::uint32_t address) const {
    std::lock_guard lock(mutex);
    std::uint32_t offset = (address - ApuBase) & 0x0FFFu;
    if (offset < ChannelBase) {
        return getByte(globalRegister(offset & ~1u), offset);
    }
    std::uint32_t channelOffset = offset - ChannelBase;
    std::size_t index = channelOffset / ChannelSize;
    if (index >= channels.size()) {
        return 0;
    }
    return getByte(
        channelRegister(channels[index], channelOffset % ChannelSize), offset);
}

void Apu::writeMmio(std::uint32_t address, std::uint8_t value) {
    std::lock_guard lock(mutex);
    std::uint32_t offset = (address - ApuBase) & 0x0FFFu;
    if (offset < ChannelBase) {
        std::uint32_t reg = offset & ~1u;
        writeGlobalRegister(reg, setByte(globalRegister(reg), offset, value));
        return;
    }
    std::uint32_t channelOffset = offset - ChannelBase;
    std::size_t index = channelOffset / ChannelSize;
    if (index >= channels.size()) {
        status |= 1u << 7;
        setIrq(IrqInvalidAccess);
        return;
    }
    std::uint32_t reg = channelOffset % ChannelSize;
    std::uint32_t aligned = reg & ~1u;
    std::uint16_t current = channelRegister(channels[index], aligned);
    writeChannelRegister(index, aligned, setByte(current, reg, value));
}

void Apu::mix(float *output, int frames) {
    std::lock_guard lock(mutex);
    bool enabled = (control & ControlEnable) != 0;
    bool outputEnabled = (control & ControlOutputEnable) != 0;
    bool muted = (control & ControlMute) != 0;
    float masterLeft = static_cast<float>(masterVolume & 0x00FFu) / 255.0f;
    float masterRight =
        static_cast<float>((masterVolume >> 8) & 0x00FFu) / 255.0f;
    masterLeft *= static_cast<float>(masterPan & 0x00FFu) / 255.0f;
    masterRight *= static_cast<float>((masterPan >> 8) & 0x00FFu) / 255.0f;

    for (int i = 0; i < frames; i++) {
        float left = 0.0f;
        float right = 0.0f;

        if (enabled) {
            for (std::size_t c = 0; c < channels.size(); c++) {
                float sample = channelSample(c);
                const ApuChannel &channel = channels[c];
                left += sample * static_cast<float>(channel.volume & 0x00FFu) /
                        255.0f;
                right += sample *
                         static_cast<float>((channel.volume >> 8) & 0x00FFu) /
                         255.0f;
            }
        }

        if (!enabled || !outputEnabled || muted) {
            left = 0.0f;
            right = 0.0f;
        } else {
            left = std::clamp(left * masterLeft, -1.0f, 1.0f);
            right = std::clamp(right * masterRight, -1.0f, 1.0f);
        }

        output[i * 2] = left;
        output[i * 2 + 1] = right;
    }
}

void Apu::stepFrame() {
    std::lock_guard lock(mutex);
    if ((control & ControlEnable) == 0) {
        return;
    }
    frameCounter = static_cast<std::uint16_t>(frameCounter + 1);
    status |= 1u << 3;
    if ((control & ControlFrameSequencer) != 0) {
        setIrq(IrqFrameTick);
    }
    for (std::size_t i = 0; i < channels.size(); i++) {
        ApuChannel &channel = channels[i];
        if (!channel.active || (channel.control & ChannelLengthEnable) == 0) {
            continue;
        }
        if (channel.remainingFrames > 0) {
            channel.remainingFrames--;
        }
        if (channel.remainingFrames == 0) {
            finishChannel(i);
        }
    }
}

bool Apu::consumeInterrupt() {
    std::lock_guard lock(mutex);
    bool pending = interruptPending;
    interruptPending = false;
    return pending;
}

std::uint16_t Apu::globalRegister(std::uint32_t offset) const {
    switch (offset) {
    case ApuControl:
        return control;
    case ApuStatus:
        return static_cast<std::uint16_t>(
            (status & 0xFFE0u) |
            ((control & ControlEnable) != 0 ? 0x0001u : 0u) |
            ((control & ControlOutputEnable) != 0 ? 0x0002u : 0u) |
            ((control & ControlMute) != 0 ? 0x0004u : 0u) |
            ((channels[5].active) ? 0x0010u : 0u));
    case ApuMasterVolume:
        return masterVolume;
    case ApuMasterPan:
        return masterPan;
    case ApuFrameCounter:
        return frameCounter;
    case ApuIrqEnable:
        return irqEnable;
    case ApuIrqStatus:
        return irqStatus;
    default:
        return 0;
    }
}

void Apu::writeGlobalRegister(std::uint32_t offset, std::uint16_t value) {
    switch (offset) {
    case ApuControl:
        control = static_cast<std::uint16_t>(value & ~ControlResetChannels);
        if ((value & ControlResetChannels) != 0) {
            resetChannels();
        }
        break;
    case ApuStatus:
        status &= static_cast<std::uint16_t>(~value);
        break;
    case ApuMasterVolume:
        masterVolume = value;
        break;
    case ApuMasterPan:
        masterPan = value;
        break;
    case ApuIrqEnable:
        irqEnable = value;
        if ((irqStatus & irqEnable) != 0) {
            interruptPending = true;
        }
        break;
    case ApuIrqStatus:
        irqStatus &= static_cast<std::uint16_t>(~value);
        if ((value & IrqFrameTick) != 0) {
            status &= static_cast<std::uint16_t>(~(1u << 3));
        }
        if ((value & IrqPcmComplete) != 0) {
            status &= static_cast<std::uint16_t>(~(1u << 5));
        }
        if ((value & IrqInvalidAccess) != 0) {
            status &= static_cast<std::uint16_t>(~(1u << 7));
        }
        break;
    default:
        status |= 1u << 7;
        setIrq(IrqInvalidAccess);
        break;
    }
}

std::uint16_t Apu::channelRegister(const ApuChannel &channel,
                                   std::uint32_t offset) const {
    switch (offset) {
    case ChannelControl:
        return channel.control;
    case ChannelStatus:
        return static_cast<std::uint16_t>(
            (channel.status & ~ChannelStatusActive) |
            (channel.active ? ChannelStatusActive : 0));
    case ChannelFrequency:
        return channel.frequency;
    case ChannelVolume:
        return channel.volume;
    case ChannelPan:
        return channel.pan;
    case ChannelLength:
        return channel.length;
    case ChannelEnvelope:
        return channel.envelope;
    case ChannelParam:
        return channel.param;
    case ChannelBaseAddress:
        return static_cast<std::uint16_t>(channel.baseAddress & 0xFFFFu);
    case ChannelBaseAddress + 2:
        return static_cast<std::uint16_t>((channel.baseAddress >> 16) & 0xFFu);
    default:
        return 0;
    }
}

void Apu::writeChannelRegister(std::size_t index, std::uint32_t offset,
                               std::uint16_t value) {
    ApuChannel &channel = channels[index];
    switch (offset) {
    case ChannelControl:
        channel.control = static_cast<std::uint16_t>(value & ~ChannelTrigger);
        if ((value & ChannelEnable) == 0) {
            channel.active = false;
        }
        if ((value & ChannelTrigger) != 0) {
            trigger(index);
        } else if ((value & ChannelEnable) != 0 && !channel.active) {
            channel.active = true;
            channel.status &=
                static_cast<std::uint16_t>(~ChannelStatusFinished);
        }
        break;
    case ChannelStatus:
        channel.status &= static_cast<std::uint16_t>(~value);
        break;
    case ChannelFrequency:
        channel.frequency = value;
        break;
    case ChannelVolume:
        channel.volume = value;
        break;
    case ChannelPan:
        channel.pan = value;
        break;
    case ChannelLength:
        channel.length = value;
        channel.remainingFrames = value;
        break;
    case ChannelEnvelope:
        channel.envelope = value;
        break;
    case ChannelParam:
        channel.param = value;
        break;
    case ChannelBaseAddress:
        channel.baseAddress = (channel.baseAddress & 0xFF0000u) | value;
        break;
    case ChannelBaseAddress + 2:
        channel.baseAddress =
            (channel.baseAddress & 0x00FFFFu) |
            ((static_cast<std::uint32_t>(value) & 0x00FFu) << 16);
        break;
    default:
        status |= 1u << 7;
        setIrq(IrqInvalidAccess);
        break;
    }
}

void Apu::trigger(std::size_t index) {
    ApuChannel &channel = channels[index];
    if ((control & ControlEnable) == 0) {
        status |= 1u << 7;
        setIrq(IrqInvalidAccess);
    }
    channel.control |= ChannelEnable;
    channel.active = true;
    channel.status = ChannelStatusActive;
    channel.phase = 0.0;
    channel.pcmPosition = 0.0;
    channel.noiseTimer = 0.0;
    channel.lfsr = 0x4000;
    channel.remainingFrames = channel.length;
}

void Apu::resetChannels() {
    for (ApuChannel &channel : channels) {
        channel = ApuChannel{};
    }
}

float Apu::channelSample(std::size_t index) {
    ApuChannel &channel = channels[index];
    if (!channel.active || (channel.control & ChannelEnable) == 0) {
        return 0.0f;
    }
    if (channel.frequency == 0 && index != 5) {
        return 0.0f;
    }
    switch (index) {
    case 0:
    case 1:
        return pulseSample(channel);
    case 2:
        return triangleSample(channel);
    case 3:
        return noiseSample(channel);
    case 4:
        return wavetableSample(channel);
    case 5:
        if ((control & ControlPcmEnable) == 0) {
            return 0.0f;
        }
        return pcmSample(channel);
    default:
        return 0.0f;
    }
}

float Apu::pulseSample(ApuChannel &channel) {
    channel.phase += static_cast<double>(channel.frequency) / ApuSampleRate;
    channel.phase -= std::floor(channel.phase);
    constexpr std::array<double, 4> duties{0.125, 0.25, 0.5, 0.75};
    return channel.phase < duties[channel.param & 0x03u] ? 1.0f : -1.0f;
}

float Apu::triangleSample(ApuChannel &channel) {
    channel.phase += static_cast<double>(channel.frequency) / ApuSampleRate;
    channel.phase -= std::floor(channel.phase);
    if (channel.phase < 0.5) {
        return static_cast<float>(channel.phase * 4.0 - 1.0);
    }
    return static_cast<float>(3.0 - channel.phase * 4.0);
}

float Apu::noiseSample(ApuChannel &channel) {
    channel.noiseTimer +=
        static_cast<double>(channel.frequency) / ApuSampleRate;
    while (channel.noiseTimer >= 1.0) {
        channel.noiseTimer -= 1.0;
        std::uint16_t tap = (channel.param & 1u) != 0 ? 6u : 1u;
        std::uint16_t bit = static_cast<std::uint16_t>(
            (channel.lfsr ^ (channel.lfsr >> tap)) & 1u);
        channel.lfsr =
            static_cast<std::uint16_t>((channel.lfsr >> 1) | (bit << 14));
        if (channel.lfsr == 0) {
            channel.lfsr = 0x4000;
        }
    }
    return (channel.lfsr & 1u) != 0 ? 1.0f : -1.0f;
}

float Apu::wavetableSample(ApuChannel &channel) {
    int length = channel.length == 0 ? 32 : channel.length;
    std::uint32_t offset = 0;
    if (!audioRamOffset(channel.baseAddress, offset) ||
        offset + static_cast<std::uint32_t>(length) > audioRam.size()) {
        status |= 1u << 7;
        setIrq(IrqInvalidAccess);
        return 0.0f;
    }
    channel.phase +=
        static_cast<double>(channel.frequency) * length / ApuSampleRate;
    channel.phase = std::fmod(channel.phase, static_cast<double>(length));
    int index = static_cast<int>(channel.phase) % length;
    return normalizedByte(audioRam[offset + static_cast<std::uint32_t>(index)]);
}

float Apu::pcmSample(ApuChannel &channel) {
    if (channel.length == 0) {
        finishChannel(5);
        return 0.0f;
    }
    std::uint32_t offset = 0;
    if (!audioRamOffset(channel.baseAddress, offset) ||
        offset + channel.length > audioRam.size()) {
        status |= 1u << 7;
        setIrq(IrqInvalidAccess);
        finishChannel(5);
        return 0.0f;
    }
    int index = static_cast<int>(channel.pcmPosition);
    if (index >= channel.length) {
        if ((channel.control & ChannelLoop) != 0) {
            channel.pcmPosition = 0.0;
            index = 0;
        } else {
            finishChannel(5);
            return 0.0f;
        }
    }
    float sample =
        normalizedByte(audioRam[offset + static_cast<std::uint32_t>(index)]);
    channel.pcmPosition +=
        static_cast<double>(channel.frequency) / ApuSampleRate;
    if (channel.pcmPosition >= channel.length &&
        (channel.control & ChannelLoop) == 0) {
        finishChannel(5);
    }
    return sample;
}

void Apu::finishChannel(std::size_t index) {
    ApuChannel &channel = channels[index];
    channel.active = false;
    channel.control &= static_cast<std::uint16_t>(~ChannelEnable);
    channel.status &= static_cast<std::uint16_t>(~ChannelStatusActive);
    channel.status |= ChannelStatusFinished;
    if (index == 5) {
        status |= 1u << 5;
        setIrq(IrqPcmComplete);
    }
}

void Apu::setIrq(std::uint16_t bits) {
    irqStatus |= bits;
    if ((irqStatus & irqEnable & bits) != 0) {
        interruptPending = true;
    }
}

bool Apu::audioRamOffset(std::uint32_t address, std::uint32_t &offset) const {
    address = mask24(address);
    if (address < AudioRamBase || address > AudioRamEnd) {
        return false;
    }
    offset = address - AudioRamBase;
    return true;
}

}
