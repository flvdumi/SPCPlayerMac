#include "Dsp.hpp"
#include <algorithm>
#include <cstring>

const uint32_t Dsp::RATE_TABLE[32] = {
    0,
    2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
    64,   48,   40,   32,   24,  20,  16,  12,  10,  8,   6,   5,   4,   3,  2, 1
};

const int16_t Dsp::GAUSS_TABLE[1024] = {
    // s0 (0..255)
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
      16,   16,   16,   16,   16,   16,   16,   16,   16,   16,   16,   32,   32,   32,   32,   32,
      32,   32,   48,   48,   48,   48,   48,   64,   64,   64,   64,   64,   80,   80,   80,   80,
      96,   96,   96,   96,  112,  112,  112,  128,  128,  128,  144,  144,  144,  160,  160,  160,
     176,  176,  176,  192,  192,  208,  208,  224,  224,  240,  240,  240,  256,  256,  272,  272,
     288,  304,  304,  320,  320,  336,  336,  352,  368,  368,  384,  384,  400,  416,  432,  432,
     448,  464,  464,  480,  496,  512,  512,  528,  544,  560,  576,  576,  592,  608,  624,  640,
     656,  672,  688,  704,  720,  736,  752,  768,  784,  800,  816,  832,  848,  864,  880,  896,
     928,  944,  960,  976,  992, 1024, 1040, 1056, 1072, 1104, 1120, 1136, 1168, 1184, 1216, 1232,
    1248, 1280, 1296, 1328, 1344, 1376, 1392, 1424, 1440, 1472, 1504, 1520, 1552, 1584, 1600, 1632,
    1664, 1696, 1712, 1744, 1776, 1808, 1840, 1872, 1888, 1920, 1952, 1984, 2016, 2048, 2080, 2112,
    2144, 2192, 2224, 2256, 2288, 2320, 2352, 2400, 2432, 2464, 2496, 2544, 2576, 2608, 2656, 2688,
    2736, 2768, 2800, 2848, 2880, 2928, 2976, 3008, 3056, 3088, 3136, 3184, 3216, 3264, 3312, 3360,
    3392, 3440, 3488, 3536, 3584, 3632, 3680, 3728, 3776, 3824, 3872, 3920, 3968, 4016, 4064, 4112,
    4160, 4208, 4272, 4320, 4368, 4416, 4480, 4528, 4576, 4640, 4688, 4752, 4800, 4864, 4912, 4976,
    5024, 5088, 5136, 5200, 5248, 5312, 5376, 5424, 5488, 5552, 5616, 5664, 5728, 5792, 5856, 5920,
    // s1 (256..511)
    5984, 6048, 6096, 6160, 6224, 6288, 6352, 6416, 6480, 6560, 6624, 6688, 6752, 6816, 6880, 6944,
    7024, 7088, 7152, 7216, 7296, 7360, 7424, 7504, 7568, 7632, 7712, 7776, 7856, 7920, 7984, 8064,
    8128, 8208, 8272, 8352, 8432, 8496, 8576, 8640, 8720, 8800, 8864, 8944, 9008, 9088, 9168, 9232,
    9312, 9392, 9472, 9536, 9616, 9696, 9776, 9840, 9920,10000,10080,10160,10240,10304,10384,10464,
   10544,10624,10704,10784,10848,10928,11008,11088,11168,11248,11328,11408,11488,11568,11648,11712,
   11792,11872,11952,12032,12112,12192,12272,12352,12432,12512,12592,12672,12752,12832,12896,12976,
   13056,13136,13216,13296,13376,13456,13536,13616,13680,13760,13840,13920,14000,14080,14144,14224,
   14304,14384,14464,14528,14608,14688,14768,14832,14912,14992,15056,15136,15216,15280,15360,15440,
   15504,15584,15648,15728,15808,15872,15952,16016,16080,16160,16224,16304,16368,16432,16512,16576,
   16640,16720,16784,16848,16912,16976,17056,17120,17184,17248,17312,17376,17440,17504,17568,17632,
   17696,17744,17808,17872,17936,18000,18048,18112,18176,18224,18288,18336,18400,18448,18512,18560,
   18624,18672,18720,18784,18832,18880,18928,18976,19040,19088,19136,19184,19232,19280,19312,19360,
   19408,19456,19504,19536,19584,19632,19664,19712,19744,19792,19824,19856,19904,19936,19968,20016,
   20048,20080,20112,20144,20176,20208,20240,20272,20304,20320,20352,20384,20400,20432,20464,20480,
   20512,20528,20544,20576,20592,20608,20640,20656,20672,20688,20704,20720,20736,20752,20752,20768,
   20784,20800,20800,20816,20832,20832,20848,20848,20848,20864,20864,20864,20864,20864,20880,20880,
   // s2 (512..767)
   20880,20880,20864,20864,20864,20864,20864,20848,20848,20848,20832,20832,20816,20800,20800,20784,
   20768,20752,20752,20736,20720,20704,20688,20672,20656,20640,20608,20592,20576,20544,20528,20512,
   20480,20464,20432,20400,20384,20352,20320,20304,20272,20240,20208,20176,20144,20112,20080,20048,
   20016,19968,19936,19904,19856,19824,19792,19744,19712,19664,19632,19584,19536,19504,19456,19408,
   19360,19312,19280,19232,19184,19136,19088,19040,18976,18928,18880,18832,18784,18720,18672,18624,
   18560,18512,18448,18400,18336,18288,18224,18176,18112,18048,18000,17936,17872,17808,17744,17696,
   17632,17568,17504,17440,17376,17312,17248,17184,17120,17056,16976,16912,16848,16784,16720,16640,
   16576,16512,16432,16368,16304,16224,16160,16080,16016,15952,15872,15808,15728,15648,15584,15504,
   15440,15360,15280,15216,15136,15056,14992,14912,14832,14768,14688,14608,14528,14464,14384,14304,
   14224,14144,14080,14000,13920,13840,13760,13680,13616,13536,13456,13376,13296,13216,13136,13056,
   12976,12896,12832,12752,12672,12592,12512,12432,12352,12272,12192,12112,12032,11952,11872,11792,
   11712,11648,11568,11488,11408,11328,11248,11168,11088,11008,10928,10848,10784,10704,10624,10544,
   10464,10384,10304,10240,10160,10080,10000, 9920, 9840, 9776, 9696, 9616, 9536, 9472, 9392, 9312,
    9232, 9168, 9088, 9008, 8944, 8864, 8800, 8720, 8640, 8576, 8496, 8432, 8352, 8272, 8208, 8128,
    8064, 7984, 7920, 7856, 7776, 7712, 7632, 7568, 7504, 7424, 7360, 7296, 7216, 7152, 7088, 7024,
    6944, 6880, 6816, 6752, 6688, 6624, 6560, 6480, 6416, 6352, 6288, 6224, 6160, 6096, 6048, 5984,
    // s3 (768..1023)
    5920, 5856, 5792, 5728, 5664, 5616, 5552, 5488, 5424, 5376, 5312, 5248, 5200, 5136, 5088, 5024,
    4976, 4912, 4864, 4800, 4752, 4688, 4640, 4576, 4528, 4480, 4416, 4368, 4320, 4272, 4208, 4160,
    4112, 4064, 4016, 3968, 3920, 3872, 3824, 3776, 3728, 3680, 3632, 3584, 3536, 3488, 3440, 3392,
    3360, 3312, 3264, 3216, 3184, 3136, 3088, 3056, 3008, 2976, 2928, 2880, 2848, 2800, 2768, 2736,
    2688, 2656, 2608, 2576, 2544, 2496, 2464, 2432, 2400, 2352, 2320, 2288, 2256, 2224, 2192, 2144,
    2112, 2080, 2048, 2016, 1984, 1952, 1920, 1888, 1872, 1840, 1808, 1776, 1744, 1712, 1696, 1664,
    1632, 1600, 1584, 1552, 1520, 1504, 1472, 1440, 1424, 1392, 1376, 1344, 1328, 1296, 1280, 1248,
    1232, 1216, 1184, 1168, 1136, 1120, 1104, 1072, 1056, 1040, 1024,  992,  976,  960,  944,  928,
     896,  880,  864,  848,  832,  816,  800,  784,  768,  752,  736,  720,  704,  688,  672,  656,
     640,  624,  608,  592,  576,  576,  560,  544,  528,  512,  512,  496,  480,  464,  464,  448,
     432,  432,  416,  400,  384,  384,  368,  368,  352,  336,  336,  320,  320,  304,  304,  288,
     272,  272,  256,  256,  240,  240,  240,  224,  224,  208,  208,  192,  192,  176,  176,  176,
     160,  160,  160,  144,  144,  144,  128,  128,  128,  112,  112,  112,   96,   96,   96,   96,
      80,   80,   80,   80,   64,   64,   64,   64,   64,   48,   48,   48,   48,   48,   32,   32,
      32,   32,   32,   32,   32,   16,   16,   16,   16,   16,   16,   16,   16,   16,   16,   16,
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0
};

Dsp::Dsp() {
    aaf1.init(8000.0f, 32000.0f);
    aaf2.init(12000.0f, 32000.0f);
    reset();
}

void Dsp::reset() {
    std::memset(&regs, 0, sizeof(regs));
    regs.raw[0x6C] = 0xE0;

    for (auto& v : voices) {
        v = VoiceChannel{};
        v.flags.inactive = true;
    }

    fir_buf.fill(0.0f);
    fir_cur = 0;
    echo_ram_ptr = 0;
    echo_length = 4;

    master_vol_l = 0.0f;
    master_vol_r = 0.0f;
    echo_vol_l   = 0.0f;
    echo_vol_r   = 0.0f;
    echo_fb      = 0.0f;
    echo_fb_ct   = 0.0f;
    vol_amp      = 1.0f;

    channel_mute_mask    = 0x00;
    channel_noise_mask   = 0x00;
    stereo_sep_value     = 65536; // 100% normal stereo
    feedback_value       = 0;
    pitch_base_hz        = 32000;
    key_shift_multiplier = 1.0f;
    pitch_sync_speed     = false;
    speed_multiplier     = 1.0f;
    dsp_options          = 0x01 | 0x800;
    inter_mode           = InterpolationMode::Gauss;

    noise_period  = 0;
    noise_counter = 0;
    noise_lfsr    = 0x4000;
    noise_sample  = 0;

    aaf1.reset();
    aaf2.reset();
    bass.reset();
}

void Dsp::set_stereo_separation(uint32_t sep) {
    stereo_sep_value = sep;
    update_echo_feedback();
}

void Dsp::set_feedback_mixer(uint32_t fb) {
    feedback_value = fb;
    update_echo_feedback();
}

void Dsp::set_dsp_options(uint32_t opts) {
    dsp_options = opts;
}

void Dsp::update_echo_feedback() {
    int8_t efb = static_cast<int8_t>(regs.raw[0x0D]);
    float fb_norm = efb / 128.0f;
    float crosstalk = std::clamp(feedback_value / 65536.0f, 0.0f, 1.0f);
    echo_fb    = fb_norm * (1.0f - crosstalk);
    echo_fb_ct = fb_norm * crosstalk;
}

uint8_t Dsp::read_reg(uint8_t reg) {
    reg &= 0x7F;
    return regs.raw[reg];
}

void Dsp::write_reg(uint8_t reg, uint8_t val) {
    reg &= 0x7F;
    regs.raw[reg] = val;

    uint8_t v_idx = reg >> 4;
    uint8_t r_type = reg & 0x0F;

    if (r_type <= 0x09) {
        auto& v = voices[v_idx];
        switch (r_type) {
            case 0x00:
                v.target_vol_l = static_cast<int8_t>(val) / 128.0f;
                v.current_vol_l = v.target_vol_l;
                break;
            case 0x01:
                v.target_vol_r = static_cast<int8_t>(val) / 128.0f;
                v.current_vol_r = v.target_vol_r;
                break;
            case 0x02: case 0x03: {
                uint16_t p = regs.raw[(v_idx << 4) | 2] | (regs.raw[(v_idx << 4) | 3] << 8);
                v.original_pitch = (p & 0x3FFF) << 4;
                // If 0x40 (DSP_NOPREAD: Disable Pitch Bend) is set and voice is active, keep latched pitch
                if (!(dsp_options & 0x40) || v.flags.inactive) {
                    v.latched_pitch = v.original_pitch;
                }
                v.pitch_rate = static_cast<uint32_t>(v.latched_pitch * get_total_pitch_multiplier());
                break;
            }
            case 0x04: break; // SRCN
            case 0x05: case 0x06: recalc_adsr(v_idx); break;
            case 0x07: recalc_gain(v_idx); break;
        }
    } else {
        switch (reg) {
            case 0x0C: master_vol_l = static_cast<int8_t>(val) / 128.0f; break;
            case 0x1C: master_vol_r = static_cast<int8_t>(val) / 128.0f; break;
            case 0x2C: echo_vol_l   = static_cast<int8_t>(val) / 128.0f; break;
            case 0x3C: echo_vol_r   = static_cast<int8_t>(val) / 128.0f; break;
            case 0x0D: update_echo_feedback(); break;
            case 0x4C: // Key On
                for (int i = 0; i < 8; ++i) {
                    if (val & (1 << i)) {
                        voices[i].saved_adsr = regs.raw[(i << 4) | 5] | (regs.raw[(i << 4) | 6] << 8);
                        voices[i].saved_gain = regs.raw[(i << 4) | 7];
                        key_on_voice(i);
                    }
                }
                regs.raw[0x4C] = 0;
                break;
            case 0x5C: key_off(val); break;
            case 0x7D: { // EDL
                uint8_t edl = val & 0x0F;
                echo_length = (edl == 0) ? 4 : (edl * 2048);
                if (echo_ram_ptr >= echo_length) echo_ram_ptr = 0;
                break;
            }
            case 0x6C: {
                uint8_t n_val = val & 0x1F;
                noise_period = RATE_TABLE[n_val];
                noise_counter = noise_period;
                if (val & 0x80) { // Soft Reset
                    for (auto& v : voices) {
                        v.flags.inactive = true;
                        v.env_val = 0;
                    }
                    regs.raw[0x7C] = 0;
                    regs.raw[0x6C] |= 0x60;
                }
                break;
            }
            case 0x7C: regs.raw[0x7C] = 0; break;
        }
    }
}

void Dsp::recalc_adsr(int v_idx) {
    auto& v = voices[v_idx];
    if (v.flags.inactive || v.flags.key_off) return;

    uint8_t adsr1 = regs.raw[(v_idx << 4) | 5];
    uint8_t adsr2 = regs.raw[(v_idx << 4) | 6];

    if (adsr1 & 0x80) {
        if (v.env_mode != EnvelopeMode::Att &&
            v.env_mode != EnvelopeMode::Decay &&
            v.env_mode != EnvelopeMode::Sust) {
            v.env_mode = EnvelopeMode::Att;
            v.env_dest = 2047;
            v.env_idle = false;
        }

        if (v.env_mode == EnvelopeMode::Att) {
            uint8_t ar = adsr1 & 0x0F;
            v.env_rate = RATE_TABLE[ar * 2 + 1];
            v.env_counter = v.env_rate;
            if (ar == 15) {
                v.env_val = 2047;
                v.env_mode = EnvelopeMode::Decay;
                uint8_t dr = (adsr1 >> 4) & 7;
                v.env_rate = RATE_TABLE[dr * 2 + 16];
                v.env_counter = v.env_rate;
                int sl = (adsr2 >> 5) + 1;
                v.env_dest = (sl * 2048) / 8 - 1;
            }
        } else if (v.env_mode == EnvelopeMode::Decay) {
            uint8_t dr = (adsr1 >> 4) & 7;
            v.env_rate = RATE_TABLE[dr * 2 + 16];
            v.env_counter = v.env_rate;
            int sl = (adsr2 >> 5) + 1;
            v.env_dest = (sl * 2048) / 8 - 1;
            if (v.env_val <= v.env_dest) {
                v.env_mode = EnvelopeMode::Sust;
                uint8_t sr = adsr2 & 0x1F;
                v.env_rate = RATE_TABLE[sr];
                v.env_counter = v.env_rate;
                v.env_dest = 0;
                v.env_idle = (sr == 0);
            }
        } else if (v.env_mode == EnvelopeMode::Sust) {
            uint8_t sr = adsr2 & 0x1F;
            v.env_rate = RATE_TABLE[sr];
            v.env_counter = v.env_rate;
            v.env_idle = (sr == 0);
        }
    } else {
        recalc_gain(v_idx);
    }
}

void Dsp::recalc_gain(int v_idx) {
    auto& v = voices[v_idx];
    if (v.flags.inactive || v.flags.key_off) return;

    uint8_t adsr1 = regs.raw[(v_idx << 4) | 5];
    if (adsr1 & 0x80) return;

    uint8_t gain = regs.raw[(v_idx << 4) | 7];
    if ((gain & 0x80) == 0) {
        uint8_t direct = gain & 0x7F;
        v.env_val = (direct << 4) + (direct >> 3);
        v.env_mode = EnvelopeMode::Direct;
        v.env_idle = true;
    } else {
        uint8_t rate_idx = gain & 0x1F;
        v.env_rate = RATE_TABLE[rate_idx];
        v.env_counter = v.env_rate;
        v.env_idle = (rate_idx == 0);

        uint8_t mode = (gain >> 5) & 3;
        if (mode == 0)      { v.env_mode = EnvelopeMode::Dec;  v.env_dest = 0; }
        else if (mode == 1) { v.env_mode = EnvelopeMode::Exp;  v.env_dest = 0; }
        else if (mode == 2) { v.env_mode = EnvelopeMode::Inc;  v.env_dest = 2047; }
        else                { v.env_mode = EnvelopeMode::Bent; v.env_dest = 1536; }
    }
}

void Dsp::key_on_voice(int i) {
    if (!apu_ram) return;
    uint16_t dir_page = regs.raw[0x5D] << 8;

    auto& v = voices[i];
    v.flags.inactive = false;
    v.flags.key_off = false;
    v.flags.end_block_decoded = false;
    v.last_output = 0;

    v.target_vol_l  = static_cast<int8_t>(regs.raw[(i << 4) | 0]) / 128.0f;
    v.target_vol_r  = static_cast<int8_t>(regs.raw[(i << 4) | 1]) / 128.0f;
    v.current_vol_l = v.target_vol_l;
    v.current_vol_r = v.target_vol_r;

    uint16_t p = regs.raw[(i << 4) | 2] | (regs.raw[(i << 4) | 3] << 8);
    v.original_pitch = (p & 0x3FFF) << 4;
    v.latched_pitch  = v.original_pitch;
    v.pitch_rate     = static_cast<uint32_t>(v.latched_pitch * get_total_pitch_multiplier());

    uint8_t src = regs.raw[(i << 4) | 4];
    v.srcn = src;
    uint16_t entry = dir_page + (src * 4);
    v.brr_addr = apu_ram[entry] | (apu_ram[(entry + 1) & 0xFFFF] << 8);

    v.pitch_dec = 0;
    v.prev1 = 0;
    v.prev2 = 0;
    std::memset(v.sample_buf, 0, sizeof(v.sample_buf));

    uint8_t adsr1 = v.saved_adsr & 0xFF;
    uint8_t adsr2 = (v.saved_adsr >> 8) & 0xFF;
    uint8_t gain  = v.saved_gain;

    if (adsr1 & 0x80) {
        v.env_val = 0;
        uint8_t ar = adsr1 & 0x0F;
        if (ar == 15) {
            v.env_val = 2047;
            v.env_mode = EnvelopeMode::Decay;
            uint8_t dr = (adsr1 >> 4) & 7;
            v.env_rate = RATE_TABLE[dr * 2 + 16];
            v.env_counter = v.env_rate;
            int sl = (adsr2 >> 5) + 1;
            v.env_dest = (sl * 2048) / 8 - 1;
            v.env_idle = false;
        } else {
            v.env_mode = EnvelopeMode::Att;
            v.env_idle = false;
            v.env_dest = 2047;
            v.env_rate = RATE_TABLE[ar * 2 + 1];
            v.env_counter = v.env_rate;
        }
    } else {
        if ((gain & 0x80) == 0) {
            uint8_t direct = gain & 0x7F;
            v.env_val = (direct << 4) + (direct >> 3);
            v.env_mode = EnvelopeMode::Direct;
            v.env_idle = true;
        } else {
            uint8_t mode = (gain >> 5) & 3;
            uint8_t rate_idx = gain & 0x1F;
            v.env_rate = RATE_TABLE[rate_idx];
            v.env_counter = v.env_rate;
            v.env_idle = (rate_idx == 0);

            if (mode == 0) { 
                v.env_mode = EnvelopeMode::Dec;  v.env_dest = 0; v.env_val = 2047;
            } else if (mode == 1) { 
                v.env_mode = EnvelopeMode::Exp;  v.env_dest = 0; v.env_val = 2047;
            } else if (mode == 2) { 
                v.env_mode = EnvelopeMode::Inc;  v.env_dest = 2047; v.env_val = 0;
            } else { 
                v.env_mode = EnvelopeMode::Bent; v.env_dest = 1536; v.env_val = 0;
            }
        }
    }

    decode_brr_block(v);

    v.prev_samples[0] = v.sample_buf[0];
    v.prev_samples[1] = v.sample_buf[0];
    v.prev_samples[2] = v.sample_buf[0];
    v.prev_samples[3] = v.sample_buf[0];
    v.sample_index = 0;

    regs.raw[0x7C] &= ~(1 << i);
}

void Dsp::key_off(uint8_t mask) {
    for (int i = 0; i < 8; ++i) {
        if (mask & (1 << i)) {
            auto& v = voices[i];
            if (!v.flags.inactive) {
                v.flags.key_off = true;
                v.env_mode = EnvelopeMode::Rel;
                v.env_rate = RATE_TABLE[31];
                v.env_counter = v.env_rate;
                v.env_dest = 0;
                v.env_idle = false;
            }
        }
    }
}

void Dsp::decode_brr_block(VoiceChannel& v) {
    if (!apu_ram) return;

    std::memcpy(v.prev_samples, &v.sample_buf[12], 4 * sizeof(int16_t));

    uint8_t header = apu_ram[v.brr_addr++];
    v.brr_header = header;

    int range = header >> 4;
    int filter = (header >> 2) & 3;
    bool is_old_decoder = (dsp_options & 0x02) != 0; // 0x02: Old ADPCM Decoder

    for (int byte_idx = 0; byte_idx < 8; ++byte_idx) {
        uint8_t data = apu_ram[v.brr_addr++];

        for (int nybble_idx = 0; nybble_idx < 2; ++nybble_idx) {
            int32_t delta = (nybble_idx == 0) ? (data >> 4) : (data & 0x0F);
            if (delta >= 8) delta -= 16;

            if (range <= 12) {
                delta = (delta << range) >> 1;
            } else {
                delta = (delta < 0) ? -2048 : 0;
            }

            int32_t s = delta;
            int32_t p1 = v.prev1;
            int32_t p2 = v.prev2;

            if (is_old_decoder) {
                // Classic unclipped BRR filter
                switch (filter) {
                    case 0: break;
                    case 1: s += p1 + (-p1 >> 4); break;
                    case 2: s += (p1 * 2) + ((-3 * p1) >> 5) - p2 + (p2 >> 4); break;
                    case 3: s += (p1 * 2) + ((-13 * p1) >> 6) - p2 + ((p2 * 3) >> 4); break;
                }
            } else {
                // Authentic SNES S-DSP clamped arithmetic
                switch (filter) {
                    case 0: break;
                    case 1: s += p1 + ((-p1 >> 4) & ~1); break;
                    case 2: s += (p1 * 2) + (((-3 * p1) >> 5) & ~1) - p2 + ((p2 >> 4) & ~1); break;
                    case 3: s += (p1 * 2) + (((-13 * p1) >> 6) & ~1) - p2 + (((p2 * 3) >> 4) & ~1); break;
                }
            }

            s = std::clamp<int32_t>(s, -32768, 32767);
            int16_t out_s = static_cast<int16_t>(s) & ~1;

            v.sample_buf[byte_idx * 2 + nybble_idx] = out_s;
            v.prev2 = v.prev1;
            v.prev1 = out_s;
        }
    }

    if (header & 0x01) {
        int v_idx = static_cast<int>(&v - &voices[0]);
        regs.raw[0x7C] |= (1 << v_idx);

        if (header & 0x02) {
            uint16_t dir_page = regs.raw[0x5D] << 8;
            uint16_t entry = dir_page + (v.srcn * 4) + 2;
            v.brr_addr = apu_ram[entry] | (apu_ram[(entry + 1) & 0xFFFF] << 8);
        } else {
            v.flags.end_block_decoded = true;
        }
    }
}

void Dsp::update_envelope(VoiceChannel& v, int v_idx) {
    if (v.env_idle) return;

    if (v.env_counter > 0) {
        v.env_counter--;
        if (v.env_counter > 0) return;
    }
    v.env_counter = v.env_rate;

    int32_t val = v.env_val;

    switch (v.env_mode) {
        case EnvelopeMode::Att: {
            val += 32;
            if (val >= 2047) {
                val = 2047;
                v.env_mode = EnvelopeMode::Decay;
                uint8_t dr = (regs.raw[(v_idx << 4) | 5] >> 4) & 7;
                v.env_rate = RATE_TABLE[dr * 2 + 16];
                v.env_counter = v.env_rate;
                int sl = (regs.raw[(v_idx << 4) | 6] >> 5) + 1;
                v.env_dest = (sl * 2048) / 8 - 1;
            }
            break;
        }
        case EnvelopeMode::Decay: {
            val -= ((val - 1) >> 8) + 1;
            if (val <= v.env_dest) {
                val = v.env_dest;
                v.env_mode = EnvelopeMode::Sust;
                uint8_t sr = regs.raw[(v_idx << 4) | 6] & 0x1F;
                v.env_rate = RATE_TABLE[sr];
                v.env_counter = v.env_rate;
                v.env_dest = 0;
                v.env_idle = (sr == 0);
            }
            break;
        }
        case EnvelopeMode::Sust: {
            val -= ((val - 1) >> 8) + 1;
            if (val <= 0) { val = 0; v.env_idle = true; }
            break;
        }
        case EnvelopeMode::Rel: {
            val -= 8;
            if (val <= 0) { val = 0; v.env_idle = true; v.flags.inactive = true; }
            break;
        }
        case EnvelopeMode::Inc: {
            val += 32;
            if (val >= 2047) { val = 2047; v.env_idle = true; }
            break;
        }
        case EnvelopeMode::Bent: {
            val += (val < 1536) ? 32 : 8;
            if (val >= 2047) { val = 2047; v.env_idle = true; }
            break;
        }
        case EnvelopeMode::Dec: {
            val -= 32;
            if (val <= 0) { val = 0; v.env_idle = true; }
            break;
        }
        case EnvelopeMode::Exp: {
            val -= ((val - 1) >> 8) + 1;
            if (val <= 0) { val = 0; v.env_idle = true; }
            break;
        }
        case EnvelopeMode::Direct: {
            v.env_idle = true;
            break;
        }
    }

    v.env_val = std::clamp(val, 0, 2047);
}

int16_t Dsp::interpolate_sample(const VoiceChannel& v) {
    int idx = v.sample_index;
    int16_t s0 = (idx >= 2) ? v.sample_buf[idx - 2] : v.prev_samples[idx + 2];
    int16_t s1 = (idx >= 1) ? v.sample_buf[idx - 1] : v.prev_samples[idx + 3];
    int16_t s2 = v.sample_buf[idx];
    int16_t s3 = (idx + 1 < 16) ? v.sample_buf[idx + 1] : v.sample_buf[15]; 
    float frac = (v.pitch_dec & 0xFFFF) / 65536.0f;

    switch (inter_mode) {
        case InterpolationMode::None:
            return s1;

        case InterpolationMode::Linear: {
            float out = s1 + (s2 - s1) * frac;
            return static_cast<int16_t>(std::clamp(out, -32768.0f, 32767.0f));
        }

        case InterpolationMode::Cubic: {
            float a = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
            float b = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
            float c = -0.5f * s0 + 0.5f * s2;
            float d = s1;
            float out = ((a * frac + b) * frac + c) * frac + d;
            return static_cast<int16_t>(std::clamp(out, -32768.0f, 32767.0f));
        }

        case InterpolationMode::Gauss:
        default: {
            int f_idx = (v.pitch_dec >> 8) & 0xFF;
            int c0 = GAUSS_TABLE[f_idx];
            int c1 = GAUSS_TABLE[256 + f_idx];
            int c2 = GAUSS_TABLE[512 + f_idx];
            int c3 = GAUSS_TABLE[768 + f_idx];
            int32_t out = (s0 * c3 + s1 * c2 + s2 * c1 + s3 * c0) >> 15;
            return static_cast<int16_t>(std::clamp(out, -32768, 32767));
        }
    }
}

void Dsp::step_noise() {
    if (noise_period == 0) return;

    if (--noise_counter == 0) {
        noise_counter = noise_period;
        uint16_t feedback = (noise_lfsr & 1) ^ ((noise_lfsr >> 1) & 1);
        noise_lfsr = (noise_lfsr >> 1) | (feedback << 14);
        noise_sample = static_cast<int16_t>(noise_lfsr << 1);
    }
}

void Dsp::process_echo(float in_l, float in_r, float& out_l, float& out_r, bool echo_write_enabled) {
    if (!apu_ram) return;

    uint16_t esa_base = regs.raw[0x6D] << 8;
    uint16_t cur_addr = (esa_base + echo_ram_ptr) & 0xFFFF;

    int16_t raw_ram_l = static_cast<int16_t>(apu_ram[cur_addr] | (apu_ram[(cur_addr + 1) & 0xFFFF] << 8));
    int16_t raw_ram_r = static_cast<int16_t>(apu_ram[(cur_addr + 2) & 0xFFFF] | (apu_ram[(cur_addr + 3) & 0xFFFF] << 8));

    float echo_l = raw_ram_l / 32768.0f;
    float echo_r = raw_ram_r / 32768.0f;

    fir_buf[fir_cur * 2]     = echo_l;
    fir_buf[fir_cur * 2 + 1] = echo_r;

    float filtered_l = 0.0f;
    float filtered_r = 0.0f;

    // 0x80 (DSP_NOFIR: Disable FIR Filter)
    if (dsp_options & 0x80) {
        filtered_l = echo_l;
        filtered_r = echo_r;
    } else {
        for (int tap = 0; tap < 8; ++tap) {
            size_t idx = (fir_cur + tap + 1) % 8;
            float coeff = static_cast<int8_t>(regs.raw[(tap << 4) | 0x0F]) / 128.0f;
            filtered_l += fir_buf[idx * 2]     * coeff;
            filtered_r += fir_buf[idx * 2 + 1] * coeff;
        }
    }

    fir_cur = (fir_cur + 1) % 8;

    filtered_l = std::clamp(filtered_l, -1.0f, 1.0f);
    filtered_r = std::clamp(filtered_r, -1.0f, 1.0f);

    out_l = filtered_l;
    out_r = filtered_r;

    // Feedback mixing with crosstalk
    float fb_l = std::clamp(in_l + (filtered_l * echo_fb + filtered_r * echo_fb_ct), -1.0f, 1.0f);
    float fb_r = std::clamp(in_r + (filtered_r * echo_fb + filtered_l * echo_fb_ct), -1.0f, 1.0f);

    if (echo_write_enabled) {
        int16_t ram_out_l, ram_out_r;

        // 0x800 (DSP_ECHOFIR: Authentic 16-bit hardware truncation)
        if (dsp_options & 0x800) {
            int32_t il = static_cast<int32_t>(fb_l * 32768.0f);
            int32_t ir = static_cast<int32_t>(fb_r * 32768.0f);
            ram_out_l = static_cast<int16_t>(std::clamp(il, -32768, 32767)) & ~1;
            ram_out_r = static_cast<int16_t>(std::clamp(ir, -32768, 32767)) & ~1;
        } else {
            ram_out_l = float_to_int16(clamp_sample(fb_l)) & ~1;
            ram_out_r = float_to_int16(clamp_sample(fb_r)) & ~1;
        }

        apu_ram[cur_addr]               = static_cast<uint8_t>(ram_out_l & 0xFF);
        apu_ram[(cur_addr + 1) & 0xFFFF] = static_cast<uint8_t>((ram_out_l >> 8) & 0xFF);
        apu_ram[(cur_addr + 2) & 0xFFFF] = static_cast<uint8_t>(ram_out_r & 0xFF);
        apu_ram[(cur_addr + 3) & 0xFFFF] = static_cast<uint8_t>((ram_out_r >> 8) & 0xFF);
    }

    echo_ram_ptr += 4;
    if (echo_ram_ptr >= echo_length) {
        echo_ram_ptr = 0;
    }
}

void Dsp::render(int16_t* output_buffer, size_t num_samples) {
    const float pitch_mul = get_total_pitch_multiplier();

    for (size_t smp = 0; smp < num_samples; ++smp) {
        step_noise();

        float mix_main_l = 0.0f, mix_main_r = 0.0f;
        float mix_echo_l = 0.0f, mix_echo_r = 0.0f;

        uint8_t pmon = regs.raw[0x2D];
        uint8_t non  = regs.raw[0x3D];
        uint8_t eon  = regs.raw[0x4D];

        for (int i = 0; i < 8; ++i) {
            auto& v = voices[i];

            if (v.flags.inactive) {
                v.last_output = 0;
                continue;
            }

            int32_t p = (dsp_options & 0x40) ? (v.latched_pitch >> 4) : 
                        ((regs.raw[(i << 4) | 2] | (regs.raw[(i << 4) | 3] << 8)) & 0x3FFF);

            if (i > 0 && (pmon & (1 << i)) && !(dsp_options & 0x20)) {
                int32_t prev_out = voices[i - 1].last_output;
                p += ((prev_out >> 5) * p) >> 10;
                p = std::clamp(p, 0, 0x3FFF);
            }

            v.pitch_rate = static_cast<uint32_t>((p << 4) * pitch_mul);

            // 0x200 (DSP_NOENV: Disable Envelope)
            if (dsp_options & 0x200) {
                v.env_val = 2047;
            } else {
                update_envelope(v, i);
            }

            // 0x400 (DSP_NONOISE: Disable Noise Generator)
            bool use_noise = ((non | channel_noise_mask) & (1 << i)) != 0;
            if (dsp_options & 0x400) use_noise = false;

            int16_t raw_sample = use_noise ? noise_sample : interpolate_sample(v);

            float env_scale = v.env_val / 2048.0f;
            float scaled_sample = (raw_sample * env_scale) / 32768.0f;

            int32_t out16 = static_cast<int32_t>(raw_sample * env_scale);
            v.last_output = static_cast<int16_t>(std::clamp(out16, -32768, 32767));

            regs.raw[(i << 4) | 0x08] = static_cast<uint8_t>(v.env_val >> 4);
            regs.raw[(i << 4) | 0x09] = static_cast<uint8_t>(v.last_output >> 8);

            float sample_l = scaled_sample * v.current_vol_l;
            float sample_r = scaled_sample * v.current_vol_r;

            if (channel_mute_mask & (1 << i)) {
                sample_l = 0.0f;
                sample_r = 0.0f;
            }

            mix_main_l += sample_l;
            mix_main_r += sample_r;

            if (eon & (1 << i)) {
                mix_echo_l += sample_l;
                mix_echo_r += sample_r;
            }

            uint32_t new_dec = v.pitch_dec + v.pitch_rate;
            v.pitch_dec = new_dec & 0xFFFF;
            int advance = new_dec >> 16;

            for (int a = 0; a < advance; ++a) {
                v.sample_index++;
                if (v.sample_index >= 16) {
                    if (v.flags.end_block_decoded) {
                        v.flags.inactive = true;
                        v.last_output = 0;
                        break;
                    }
                    v.sample_index = 0;
                    decode_brr_block(v);
                    if (v.flags.inactive) {
                        v.last_output = 0;
                        break;
                    }
                }
            }
        }

        // Echo Processing
        float echo_out_l = 0.0f, echo_out_r = 0.0f;
        bool echo_write_enabled = !(regs.raw[0x6C] & 0x20) && !(dsp_options & 0x10);
        process_echo(mix_echo_l, mix_echo_r, echo_out_l, echo_out_r, echo_write_enabled);

        if (dsp_options & 0x10) {
            echo_out_l = 0.0f;
            echo_out_r = 0.0f;
        }

        float out_l = (mix_main_l * master_vol_l + echo_out_l * echo_vol_l) * vol_amp;
        float out_r = (mix_main_r * master_vol_r + echo_out_r * echo_vol_r) * vol_amp;

        // 0x100: Bass Boost
        if (dsp_options & 0x100) {
            bass.process(out_l, out_r);
        }

        // 0x01: Analog Low-Pass Filter
        if (dsp_options & 0x01) {
            aaf1.process(out_l, out_r);
            aaf2.process(out_l, out_r);
        }

        // Stereo Separation Matrixing (0 = mono, 65536 = 100% stereo)
        float sep_factor = std::clamp(static_cast<float>(stereo_sep_value) / 65536.0f, 0.0f, 1.0f);
        float mid = (out_l + out_r) * 0.5f;
        out_l = mid + (out_l - mid) * sep_factor;
        out_r = mid + (out_r - mid) * sep_factor;

        // 0x08: Reverse Stereo
        if (dsp_options & 0x08) std::swap(out_l, out_r);

        // Master Mute
        if (regs.raw[0x6C] & 0x40) {
            out_l = 0.0f;
            out_r = 0.0f;
        }

        output_buffer[smp * 2]     = float_to_int16(clamp_sample(out_l));
        output_buffer[smp * 2 + 1] = float_to_int16(clamp_sample(out_r));
    }
}

// Fast step for seeking (bypasses heavy filters)
void Dsp::render_fast() {
    int16_t dummy[2];
    uint32_t saved_opts = dsp_options;
    dsp_options &= ~(0x01 | 0x100); // Disable AAF & Bass Boost during fast seeking
    render(dummy, 1);
    dsp_options = saved_opts;
}

void Dsp::fix_after_load() {
    master_vol_l = static_cast<int8_t>(regs.raw[0x0C]) / 128.0f;
    master_vol_r = static_cast<int8_t>(regs.raw[0x1C]) / 128.0f;
    echo_vol_l   = static_cast<int8_t>(regs.raw[0x2C]) / 128.0f;
    echo_vol_r   = static_cast<int8_t>(regs.raw[0x3C]) / 128.0f;

    update_echo_feedback();

    noise_period  = RATE_TABLE[regs.raw[0x6C] & 0x1F];
    noise_counter = noise_period;

    uint8_t edl = regs.raw[0x7D] & 0x0F;
    echo_length = (edl == 0) ? 4 : (edl * 2048);
    if (echo_ram_ptr >= echo_length) echo_ram_ptr = 0;

    const float pitch_mul = get_total_pitch_multiplier();

    for (int i = 0; i < 8; ++i) {
        auto& v = voices[i];
        v.target_vol_l  = static_cast<int8_t>(regs.raw[(i << 4) | 0]) / 128.0f;
        v.target_vol_r  = static_cast<int8_t>(regs.raw[(i << 4) | 1]) / 128.0f;
        v.current_vol_l = v.target_vol_l;
        v.current_vol_r = v.target_vol_r;

        uint16_t p = regs.raw[(i << 4) | 2] | (regs.raw[(i << 4) | 3] << 8);
        v.original_pitch = (p & 0x3FFF) << 4;
        v.latched_pitch  = v.original_pitch;
        v.pitch_rate     = static_cast<uint32_t>(v.latched_pitch * pitch_mul);

        uint8_t envx = regs.raw[(i << 4) | 0x08];
        if (envx > 0 || (regs.raw[0x4C] & (1 << i))) {
            v.flags.inactive = false;
            v.env_val = envx << 4;
            v.env_idle = false;
            recalc_adsr(i);
        }
    }
}