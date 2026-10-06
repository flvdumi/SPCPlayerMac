#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <string_view>
#include <algorithm>
#include "Spc700.hpp"
#include "Dsp.hpp"
#include "Script700.hpp"

class SnesApu {
public:
    static constexpr uint32_t APU_CLK = 24576000;              // 24.576 MHz APU master clock
    static constexpr uint32_t CLK_PER_SAMPLE_32K = 768;         // 24,576,000 / 32,000
    static constexpr uint32_t CLK_PER_TICK_64K   = 384;         // 24,576,000 / 64,000
    void set_bit_depth(int bits);       // 8, 16, 24, 32, or -4 (float)
        void set_channels(int channels);    // 1 (mono) or 2 (stereo)
        void set_song_length(uint32_t song_ticks, uint32_t fade_ticks);

        // Buffer is void* so it can accept uint8_t, int16_t, int24, int32_t, or float
        void render(void* buffer, size_t num_samples);

    SnesApu();
    ~SnesApu() = default;

    void reset();
    // Add to public methods:
    void render_float(float* out_l, float* out_r, size_t num_samples);
    bool load_spc(const uint8_t* spc_data, size_t size);
    bool load_script700(std::string_view script_text);

    // Audio synthesis entry point (renders stereo at output_sample_rate)
    void render(int16_t* buffer, size_t num_samples);

    void set_output_sample_rate(uint32_t rate);
    uint32_t get_output_sample_rate() const { return output_sample_rate; }

    void set_speed(float speed_multiplier);

    // Seeks forward to target_ticks (based on 64 kHz timer ticks)
    void seek(uint32_t target_ticks);

    Spc700& get_spc() { return spc; }
    Dsp& get_dsp() { return dsp; }

private:
    Spc700    spc;
    Dsp       dsp;
    Script700 script;
    int bit_depth = 16;
        int channels = 2;
    // Timing and cycle accumulator matching APU.asm
    int32_t   cyc_left = 0;              // Cycle debt/overshoot from SPC execution
    uint32_t  clock_speed = 0x10000;     // 16.16 fixed-point speed multiplier (1.0 = 0x10000)

    // Song length & fadeout
    uint32_t  song_length_ticks = 0xFFFFFFFF;
    uint32_t  fade_length_ticks = 0;

    // Sample rate & Resampler
    uint32_t  output_sample_rate = 32000;
    double    resample_phase = 0.0;
    int16_t   prev_sample_l = 0;
    int16_t   prev_sample_r = 0;
    int16_t   curr_sample_l = 0;
    int16_t   curr_sample_r = 0;

    // Buffer for batch 32 kHz synthesis
    static constexpr size_t BATCH_SAMPLES = 512;
    std::array<int16_t, BATCH_SAMPLES * 2> batch_buf{};

    void step_apu_cycles(uint32_t cycles);
    void update_fade();
};
