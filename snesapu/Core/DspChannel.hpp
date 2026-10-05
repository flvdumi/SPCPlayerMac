#pragma once
#include <cstdint>

enum class EnvelopeMode : uint8_t {
    Att = 0,
    Decay,
    Sust,
    Rel,
    Inc,
    Bent,
    Dec,
    Exp,
    Direct
};

struct VoiceFlags {
    bool inactive          = true;
    bool key_off           = false;
    bool end_block_decoded = false;
    bool mute              = false;
    bool noise             = false;
};

struct VoiceChannel {
    VoiceFlags flags{};
    uint8_t srcn = 0;

    float target_vol_l  = 0.0f;
    float target_vol_r  = 0.0f;
    float current_vol_l = 0.0f;
    float current_vol_r = 0.0f;

    // Pitch & Phase
    uint32_t original_pitch = 0;
    uint32_t latched_pitch  = 0; // For 0x40 (Disable Pitch Bend)
    uint32_t pitch_rate     = 0;
    uint32_t pitch_dec      = 0;

    // BRR Streaming
    uint16_t brr_addr       = 0;
    uint8_t  brr_header     = 0;
    int16_t  prev1          = 0;
    int16_t  prev2          = 0;
    int16_t  sample_buf[16]{};
    int16_t  prev_samples[8]{};
    int      sample_index   = 0;

    uint16_t saved_adsr = 0;
    uint8_t  saved_gain = 0;

    // Envelope
    int32_t      env_val     = 0;
    EnvelopeMode env_mode    = EnvelopeMode::Rel;
    uint32_t     env_rate    = 0;
    uint32_t     env_counter = 0;
    int32_t      env_dest    = 0;
    bool         env_idle    = true;

    int16_t last_output = 0;
};