#ifndef E16_APU_H
#define E16_APU_H

#include "e16/common.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace e16 {

constexpr int ApuChannelCount = 6;
constexpr int ApuSampleRate = 48000;

struct ApuChannel {
    std::uint16_t control = 0;
    std::uint16_t status = 0;
    std::uint16_t frequency = 0;
    std::uint16_t volume = 0;
    std::uint16_t pan = 0;
    std::uint16_t length = 0;
    std::uint16_t envelope = 0;
    std::uint16_t param = 0;
    std::uint32_t baseAddress = AudioRamBase;
    double phase = 0.0;
    double pcmPosition = 0.0;
    double noiseTimer = 0.0;
    std::uint16_t lfsr = 0x4000;
    std::uint16_t remainingFrames = 0;
    bool active = false;
};

class Apu {
  public:
    Apu();

    void reset();
    std::uint8_t readAudioRam(std::uint32_t address) const;
    void writeAudioRam(std::uint32_t address, std::uint8_t value);
    std::uint8_t readMmio(std::uint32_t address) const;
    void writeMmio(std::uint32_t address, std::uint8_t value);
    void mix(float *output, int frames);
    void stepFrame();
    bool consumeInterrupt();

  private:
    mutable std::mutex mutex;
    std::array<ApuChannel, ApuChannelCount> channels{};
    std::vector<std::uint8_t> audioRam;
    std::uint16_t control = 0;
    std::uint16_t status = 0;
    std::uint16_t masterVolume = 0xFFFF;
    std::uint16_t masterPan = 0xFFFF;
    std::uint16_t frameCounter = 0;
    std::uint16_t irqEnable = 0;
    std::uint16_t irqStatus = 0;
    bool interruptPending = false;

    std::uint16_t globalRegister(std::uint32_t offset) const;
    void writeGlobalRegister(std::uint32_t offset, std::uint16_t value);
    std::uint16_t channelRegister(const ApuChannel &channel,
                                  std::uint32_t offset) const;
    void writeChannelRegister(std::size_t index, std::uint32_t offset,
                              std::uint16_t value);
    void trigger(std::size_t index);
    void resetChannels();
    float channelSample(std::size_t index);
    float pulseSample(ApuChannel &channel);
    float triangleSample(ApuChannel &channel);
    float noiseSample(ApuChannel &channel);
    float wavetableSample(ApuChannel &channel);
    float pcmSample(ApuChannel &channel);
    void finishChannel(std::size_t index);
    void setIrq(std::uint16_t bits);
    bool audioRamOffset(std::uint32_t address, std::uint32_t &offset) const;
};

}

#endif
