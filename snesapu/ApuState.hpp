#pragma once
#include <cstdint>
#include <array>
#include "Spc700Types.hpp"
#include "DspRegisters.hpp"
#include "DspChannel.hpp"
#include "Script700Types.hpp"

constexpr size_t APU_RAM_SIZE = 0x10000; // 64 KB

enum class InterpolationMode : uint8_t {
    None    = 0,
    Linear  = 1,
    Cubic   = 2,
    Gauss   = 3, // SNES authentic Gaussian
    Sinc    = 4,
    Gauss4  = 7
};

struct DspState {
    DspRegs regs;
    VoiceChannel voices[8];

    // Global volume and panning
    float vol_main_l = 0.0f, vol_main_r = 0.0f;
    float vol_echo_l = 0.0f, vol_echo_r = 0.0f;
    float echo_fb    = 0.0f, echo_fb_crosstalk = 0.0f;

    // Noise Generator
    uint32_t noise_rate = 0;
    uint32_t noise_acc = 0;
    int32_t  noise_sample = 0;
    uint32_t noise_seed = 1;

    // 8-Tap FIR Echo Filter Ring Buffers
    static constexpr size_t ECHO_BUF_SIZE = 2 * ((192000 * 240) / 1000); // 240ms @ 192kHz
    static constexpr size_t FIR_BUF_SIZE  = 2 * 2 * 64;
    std::array<float, ECHO_BUF_SIZE> echo_buf{};
    std::array<float, FIR_BUF_SIZE * 3> fir_buf{};
    uint32_t fir_cur = 0;
    uint32_t echo_len = 0;
    uint32_t echo_cur = 0;

    // Resampler & Interpolation Options
    InterpolationMode inter_mode = InterpolationMode::Gauss;
    uint32_t sample_rate = 32000;
};

// Full APU Context
struct SnesApuContext {
    // Memory
    alignas(64) uint8_t ram[APU_RAM_SIZE]{};
    uint8_t extra_ram[64]{}; // Storage behind IPL ROM when ROM read is active
    uint8_t ipl_rom[64]{};   // Original 64-byte boot ROM

    // Communication Ports ($F4-$F7)
    uint8_t in_ports[4]{};   // Main CPU -> APU
    uint8_t out_ports[4]{};  // APU -> Main CPU

    // Subsystems
    Spc700State    spc;
    DspState       dsp;
    Script700State script700;

    // Clock and Synchronization
    int32_t cycles_left = 0;
    uint32_t apu_speed  = 0x10000; // 16.16 multiplier (1.0 = 100% speed)
};