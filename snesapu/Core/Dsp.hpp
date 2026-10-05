#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>
#include <cstring>
#include <cmath>
#include "ApuState.hpp"

// Single-pole IIR Filter for SNES Analog output emulation (8 kHz & 12 kHz)
struct AnalogFilterStage {
    float a0 = 1.0f, b1 = 0.0f;
    float z_l = 0.0f, z_r = 0.0f;

    void init(float fc, float fs) {
        float f_clamped = std::min(fc, fs * 0.45f); // Prevent Nyquist singularity
        float theta = 2.0f * 3.14159265358979323846f * (f_clamped / fs);
        b1 = std::exp(-theta);
        a0 = 1.0f - b1;
    }

    void reset() {
        z_l = 0.0f;
        z_r = 0.0f;
    }

    inline void process(float& l, float& r) {
        z_l = l * a0 + z_l * b1;
        l = z_l;
        z_r = r * a0 + z_r * b1;
        r = z_r;
    }
};

// Dual-band moving-average Bass Boost filter
struct BassBoostFilter {
    static constexpr size_t BUF1_SIZE = 64;
    static constexpr size_t BUF2_SIZE = 192;

    std::array<float, BUF1_SIZE> buf_l1{}, buf_r1{};
    std::array<float, BUF2_SIZE> buf_l2{}, buf_r2{};
    float sum_l1 = 0.0f, sum_r1 = 0.0f;
    float sum_l2 = 0.0f, sum_r2 = 0.0f;
    size_t idx1 = 0, idx2 = 0;

    void reset() {
        buf_l1.fill(0.0f); buf_r1.fill(0.0f);
        buf_l2.fill(0.0f); buf_r2.fill(0.0f);
        sum_l1 = sum_r1 = sum_l2 = sum_r2 = 0.0f;
        idx1 = idx2 = 0;
    }

    inline void process(float& l, float& r) {
        sum_l1 += l - buf_l1[idx1];
        buf_l1[idx1] = l;
        float b1_l = sum_l1 / static_cast<float>(BUF1_SIZE);

        sum_l2 += l - buf_l2[idx2];
        buf_l2[idx2] = l;
        float b2_l = sum_l2 / static_cast<float>(BUF2_SIZE);

        l += (b1_l * 1.5f - b2_l * 0.5f);

        sum_r1 += r - buf_r1[idx1];
        buf_r1[idx1] = r;
        float b1_r = sum_r1 / static_cast<float>(BUF1_SIZE);

        sum_r2 += r - buf_r2[idx2];
        buf_r2[idx2] = r;
        float b2_r = sum_r2 / static_cast<float>(BUF2_SIZE);

        r += (b1_r * 1.5f - b2_r * 0.5f);

        idx1 = (idx1 + 1) % BUF1_SIZE;
        idx2 = (idx2 + 1) % BUF2_SIZE;
    }
};

class Dsp {
public:
    Dsp();
    ~Dsp() = default;

    void reset();
    void fix_after_load();
    void set_apu_ram(uint8_t* ram_ptr) { apu_ram = ram_ptr; }

    uint8_t read_reg(uint8_t reg);
    void write_reg(uint8_t reg, uint8_t val);

    void render(int16_t* output_buffer, size_t num_samples);
    void render_fast(); // Internal step for fast seeking
    void set_amplification(float amp) { vol_amp = amp; }

    uint8_t* get_registers() { return regs.raw; }
    const uint8_t* get_registers() const { return regs.raw; }

    void set_channel_mute(uint8_t mute_mask) { channel_mute_mask = mute_mask; }
    uint8_t get_channel_mute() const { return channel_mute_mask; }

    void set_channel_noise(uint8_t noise_mask) { channel_noise_mask = noise_mask; }
    uint8_t get_channel_noise() const { return channel_noise_mask; }

    void set_stereo_separation(uint32_t sep);
    uint32_t get_stereo_separation() const { return stereo_sep_value; }

    void set_feedback_mixer(uint32_t fb);
    uint32_t get_feedback_mixer() const { return feedback_value; }

    void set_pitch_base_hz(uint32_t hz) { pitch_base_hz = hz; }
    uint32_t get_pitch_base_hz() const { return pitch_base_hz; }

    void set_pitch_multiplier(float mul) { key_shift_multiplier = mul; }
    float get_pitch_multiplier() const { return key_shift_multiplier; }

    void set_pitch_sync_speed(bool sync) { pitch_sync_speed = sync; }
    void set_speed_multiplier(float spd) { speed_multiplier = spd; }

    void set_interpolation_mode(InterpolationMode mode) { inter_mode = mode; }
    InterpolationMode get_interpolation_mode() const { return inter_mode; }

    void set_dsp_options(uint32_t opts);
    uint32_t get_dsp_options() const { return dsp_options; }

private:
    uint8_t* apu_ram = nullptr;

    DspRegs regs{};
    std::array<VoiceChannel, 8> voices{};

    float master_vol_l  = 0.0f;
    float master_vol_r  = 0.0f;
    float echo_vol_l    = 0.0f;
    float echo_vol_r    = 0.0f;
    float echo_fb       = 0.0f;
    float echo_fb_ct    = 0.0f;
    float vol_amp       = 1.0f;

    uint8_t  channel_mute_mask    = 0x00;
    uint8_t  channel_noise_mask   = 0x00;
    uint32_t stereo_sep_value     = 65536; // 65536 = 100% normal stereo
    uint32_t feedback_value       = 0;     // Crosstalk feedback
    uint32_t pitch_base_hz        = 32000;
    float    key_shift_multiplier = 1.0f;
    bool     pitch_sync_speed     = false;
    float    speed_multiplier     = 1.0f;
    uint32_t dsp_options          = 0x01 | 0x800;
    InterpolationMode inter_mode  = InterpolationMode::Gauss;

    uint32_t noise_period  = 0;
    uint32_t noise_counter = 0;
    uint16_t noise_lfsr    = 0x4000;
    int16_t  noise_sample  = 0;

    static constexpr size_t FIR_BUF_SIZE = 8;
    std::array<float, FIR_BUF_SIZE * 2> fir_buf{};
    size_t fir_cur = 0;
    uint16_t echo_ram_ptr = 0;
    size_t echo_length = 4;

    AnalogFilterStage aaf1{}, aaf2{};
    BassBoostFilter   bass{};

    static const uint32_t RATE_TABLE[32];
    static const int16_t  GAUSS_TABLE[1024];

    void update_echo_feedback();
    void key_on_voice(int i);
    void key_off(uint8_t voice_mask);

    void decode_brr_block(VoiceChannel& v);
    void update_envelope(VoiceChannel& v, int v_idx);
    int16_t interpolate_sample(const VoiceChannel& v);

    void recalc_adsr(int v_idx);
    void recalc_gain(int v_idx);

    void step_noise();
    void process_echo(float in_l, float in_r, float& out_l, float& out_r, bool echo_write_enabled);

    float get_total_pitch_multiplier() const {
        float base_ratio = static_cast<float>(pitch_base_hz) / 32000.0f;
        float total = base_ratio * key_shift_multiplier;
        if (pitch_sync_speed) total *= speed_multiplier;
        return total;
    }

    static inline float clamp_sample(float val) {
        return std::clamp(val, -1.0f, 1.0f);
    }

    static inline int16_t float_to_int16(float val) {
        int32_t ival = static_cast<int32_t>(val * 32767.0f);
        return static_cast<int16_t>(std::clamp(ival, -32768, 32767));
    }
};