#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string_view>
#include "Spc700.hpp"
#include "Dsp.hpp"
#include "Script700.hpp"

class SnesApu {
public:
    SnesApu();
    ~SnesApu() = default;

    void reset();

    bool load_spc(const uint8_t* spc_data, size_t size);
    bool load_script700(std::string_view script_text);

    // Audio synthesis entry point (renders stereo at output_sample_rate)
    void render(int16_t* buffer, size_t num_samples);

    void set_output_sample_rate(uint32_t rate);
    uint32_t get_output_sample_rate() const { return output_sample_rate; }

    void set_song_length(uint32_t song_ticks, uint32_t fade_ticks);
    void set_speed(float speed_multiplier);

    void seek(uint32_t target_ticks);

    Spc700& get_spc() { return spc; }
    Dsp& get_dsp() { return dsp; }

private:
    Spc700 spc;
    Dsp dsp;
    Script700 script;

    uint32_t clock_speed = 0x10000;
    int32_t  cycle_debt = 0;

    uint32_t song_length_ticks = 0xFFFFFFFF;
    uint32_t fade_length_ticks = 0;

    // Resampler variables
    uint32_t output_sample_rate = 32000;
    double   resample_phase = 0.0;
    int16_t  prev_sample_l = 0, prev_sample_r = 0;
    int16_t  curr_sample_l = 0, curr_sample_r = 0;

    void step_apu_one_sample();
};