#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <bit>

enum class InterpolationMode : uint8_t {
    None   = 0,
    Linear = 1,
    Cubic  = 2,
    Gauss  = 3
};

enum class EnvelopeMode : uint8_t {
    Dec    = 0x00,
    Exp    = 0x01,
    Inc    = 0x02,
    Bent   = 0x06,
    Direct = 0x07,
    Rel    = 0x08,
    Sust   = 0x09,
    Att    = 0x0A,
    Decay  = 0x0D
};

struct VoiceFlags {
    bool inactive            = true;
    bool key_off             = false;
    bool end_block_decoded   = false;
};

struct VoiceChannel {
    VoiceFlags flags{};
    int16_t last_output      = 0;

    float target_vol_l       = 0.0f;
    float target_vol_r       = 0.0f;
    float current_vol_l      = 0.0f;
    float current_vol_r      = 0.0f;

    uint32_t original_pitch  = 0;
    uint32_t latched_pitch   = 0;
    uint32_t pitch_rate      = 0;
    uint32_t pitch_dec       = 0;

    uint8_t  srcn            = 0;
    uint16_t brr_addr        = 0;
    uint8_t  brr_header      = 0;

    int16_t prev1            = 0;
    int16_t prev2            = 0;

    // Buffer structure matching DSP.asm:
    // samples[0..3]   = 4-sample history from previous block (s[-3]..s[0])
    // samples[4..19]  = 16 uncompressed samples of current block
    std::array<int16_t, 20> sample_buf{};
    int sample_index         = 3; // Starts at index 3 (sIdx = 6 bytes)

    uint16_t saved_adsr      = 0;
    uint8_t  saved_gain      = 0;

    int32_t      env_val     = 0;
    EnvelopeMode env_mode    = EnvelopeMode::Direct;
    int32_t      env_dest    = 0;
    int32_t      env_adj     = 0;
    bool         env_idle    = true;
    uint32_t     env_rate    = 0;
    uint32_t     env_counter = 0;
};

struct DspRegs {
    uint8_t raw[128];
};

// 2-stage Bilinear Transform Anti-Aliasing Filter matching DSP.asm MixAAF
struct BilinearAafStage {
    float aaf1_a1 = 0.0f, aaf1_b0 = 0.0f, aaf1_b1 = 0.0f;
    float aaf2_a1 = 0.0f, aaf2_b0 = 0.0f, aaf2_b1 = 0.0f;
    float buf_l[3] = {0.0f, 0.0f, 0.0f};
    float buf_r[3] = {0.0f, 0.0f, 0.0f};

    void init(float dsp_rate) {
        constexpr float PI = 3.14159265358979323846f;
        constexpr float CF1 = 8038.1284389846f;
        constexpr float CF2 = 16176.421441299f;

        // Stage 1
        float wdt1 = (2.0f * PI * CF1) * (1.0f / dsp_rate);
        aaf1_a1 = (-2.0f + wdt1) / (2.0f + wdt1);
        aaf1_b0 = wdt1 / (2.0f + wdt1);
        aaf1_b1 = aaf1_b0;

        // Stage 2
        float wdt2 = (2.0f * PI * CF2) * (1.0f / dsp_rate);
        aaf2_a1 = (-2.0f + wdt2) / (2.0f + wdt2);
        aaf2_b0 = wdt2 / (2.0f + wdt2);
        aaf2_b1 = aaf2_b0;

        reset();
    }

    void reset() {
        std::fill(std::begin(buf_l), std::end(buf_l), 0.0f);
        std::fill(std::begin(buf_r), std::end(buf_r), 0.0f);
    }

    static inline float zero_dn(float val) {
        uint32_t bits = std::bit_cast<uint32_t>(val);
        if ((bits & 0x7F800000u) == 0) return 0.0f;
        return val;
    }

    inline void process(float& l, float& r) {
        // Left channel: Filter 1
        float in_l = l;
        float v1_l = in_l - buf_l[0] * aaf1_a1;
        float out1_l = v1_l * aaf1_b0 + buf_l[0] * aaf1_b1;
        buf_l[0] = v1_l;
        out1_l = zero_dn(out1_l);

        // Left channel: Filter 2 (evaluated twice, 2nd order)
        float v2_l = out1_l - buf_l[1] * aaf2_a1;
        float mid2_l = v2_l * aaf2_b0 + buf_l[1] * aaf2_b1;
        float v3_l = mid2_l - buf_l[1] * aaf2_a1;
        buf_l[1] = v3_l;
        l = zero_dn(v3_l * aaf2_b0 + buf_l[1] * aaf2_b1);

        // Right channel: Filter 1
        float in_r = r;
        float v1_r = in_r - buf_r[0] * aaf1_a1;
        float out1_r = v1_r * aaf1_b0 + buf_r[0] * aaf1_b1;
        buf_r[0] = v1_r;
        out1_r = zero_dn(out1_r);

        // Right channel: Filter 2 (evaluated twice, 2nd order)
        float v2_r = out1_r - buf_r[1] * aaf2_a1;
        float mid2_r = v2_r * aaf2_b0 + buf_r[1] * aaf2_b1;
        float v3_r = mid2_r - buf_r[1] * aaf2_a1;
        buf_r[1] = v3_r;
        r = zero_dn(v3_r * aaf2_b0 + buf_r[1] * aaf2_b1);
    }
};

// Moving-Average Bass Boost filter matching DSP.asm MixBASS
struct DualBandBassBoost {
    static constexpr size_t MAX_BUF1 = 384;
    static constexpr size_t MAX_BUF2 = 1152;

    std::array<float, MAX_BUF1> buf_l1{}, buf_r1{};
    std::array<float, MAX_BUF2> buf_l2{}, buf_r2{};
    float sum_l1 = 0.0f, sum_r1 = 0.0f;
    float sum_l2 = 0.0f, sum_r2 = 0.0f;

    size_t size1 = 64;
    size_t size2 = 192;
    float  lv1   = 0.018f;
    float  lv2   = 0.018f;
    size_t cnt1  = 0;
    size_t cnt2  = 0;

    void init(float dsp_rate) {
        lv1 = (192000.0f / dsp_rate) * 0.003f;
        lv2 = (192000.0f / dsp_rate) * 0.003f;
        size1 = static_cast<size_t>(dsp_rate * 0.002f);
        size2 = static_cast<size_t>(dsp_rate * 0.006f);
        if (size1 > MAX_BUF1) size1 = MAX_BUF1;
        if (size2 > MAX_BUF2) size2 = MAX_BUF2;
        reset();
    }

    void reset() {
        buf_l1.fill(0.0f); buf_r1.fill(0.0f);
        buf_l2.fill(0.0f); buf_r2.fill(0.0f);
        sum_l1 = sum_r1 = sum_l2 = sum_r2 = 0.0f;
        cnt1 = 0; cnt2 = 0;
    }

    inline void process(float& l, float& r) {
        // Left channel
        sum_l1 = sum_l1 - buf_l1[cnt1] + l;
        buf_l1[cnt1] = l;
        float b1_l = sum_l1 * lv1;

        sum_l2 = sum_l2 - buf_l2[cnt2] + l;
        buf_l2[cnt2] = l;
        float b2_l = sum_l2 * lv2;

        l += (b1_l - b2_l);

        // Right channel
        sum_r1 = sum_r1 - buf_r1[cnt1] + r;
        buf_r1[cnt1] = r;
        float b1_r = sum_r1 * lv1;

        sum_r2 = sum_r2 - buf_r2[cnt2] + r;
        buf_r2[cnt2] = r;
        float b2_r = sum_r2 * lv2;

        r += (b1_r - b2_r);

        cnt1 = (cnt1 == 0) ? (size1 - 1) : (cnt1 - 1);
        cnt2 = (cnt2 == 0) ? (size2 - 1) : (cnt2 - 1);
    }
};

class Dsp {
public:
    Dsp();
    ~Dsp() = default;
    static constexpr size_t SCOPE_RING_SIZE = 2048;
    void get_voice_scope(int voice, int16_t* out_buf, size_t count) const;

    void reset();
    void fix_after_load();
    void set_apu_ram(uint8_t* ram_ptr) { apu_ram = ram_ptr; }

    uint8_t read_reg(uint8_t reg);
    void write_reg(uint8_t reg, uint8_t val);

    void render(int16_t* output_buffer, size_t num_samples);
    void render_fast();

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
    std::array<std::array<int16_t, SCOPE_RING_SIZE>, 8> voice_scope_buf{};
        size_t scope_write_ptr = 0;
    float master_vol_l  = 0.0f;
    float master_vol_r  = 0.0f;
    float echo_vol_l    = 0.0f;
    float echo_vol_r    = 0.0f;
    float echo_fb       = 0.0f;
    float echo_fb_ct    = 0.0f;
    float vol_amp       = 1.0f;

    uint8_t  channel_mute_mask    = 0x00;
    uint8_t  channel_noise_mask   = 0x00;
    uint32_t stereo_sep_value     = 65536;
    uint32_t feedback_value       = 0;
    uint32_t pitch_base_hz        = 32000;
    float    key_shift_multiplier = 1.0f;
    bool     pitch_sync_speed     = false;
    float    speed_multiplier     = 1.0f;
    uint32_t dsp_options          = 0x01 | 0x800;
    InterpolationMode inter_mode  = InterpolationMode::Gauss;

    // 32-bit Fibonacci LFSR matching DSP.asm
    uint32_t noise_rate    = 0;
    uint32_t noise_acc     = 0;
    uint32_t noise_seed    = 1;
    int16_t  noise_sample  = 0;

    static constexpr size_t FIR_RING_SIZE = 64;
    std::array<float, FIR_RING_SIZE * 2> fir_buf{};
    size_t   fir_cur      = 0;
    uint16_t echo_ram_ptr = 0;
    size_t   echo_length  = 4;

    BilinearAafStage  aaf{};
    DualBandBassBoost bass{};

    static const uint32_t RATE_TABLE[32];
    static const int16_t  GAUSS_TABLE[1024];
    static int16_t        CUBIC_TABLE[1024];
    static bool           tables_initialized;

    static void init_tables();
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

    static inline float zero_dn(float val) {
        uint32_t bits = std::bit_cast<uint32_t>(val);
        if ((bits & 0x7F800000u) == 0) return 0.0f;
        return val;
    }

    static inline float clamp_sample(float val) {
        return std::clamp(val, -1.0f, 1.0f);
    }

    static inline int16_t float_to_int16(float val) {
        int32_t ival = static_cast<int32_t>(val * 32767.0f);
        return static_cast<int16_t>(std::clamp(ival, -32768, 32767));
    }
};
