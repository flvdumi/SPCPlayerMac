#include "SnesApu.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
SnesApu::SnesApu() {
    spc.set_dsp(&dsp);
    dsp.set_apu_ram(spc.get_ram());

    spc.on_port_write = [this](uint16_t addr, uint8_t val) {
        script.on_port_write(static_cast<uint8_t>(addr & 3), val);
    };

    reset();
}


void SnesApu::set_bit_depth(int bits) {
    if (bits == 8 || bits == 16 || bits == 24 || bits == 32 || bits == -4 || bits == -32) {
        bit_depth = (bits == -32) ? -4 : bits;
    } else {
        bit_depth = 16;
    }
}

void SnesApu::set_channels(int ch) {
    channels = (ch == 1) ? 1 : 2;
}

// Helper to write a single sample into the output buffer at any bit depth matching DSP.asm
static inline void write_sample(uint8_t*& out, float sample, int bits) {
    float clamped = std::clamp(sample, -1.0f, 1.0f);

    switch (bits) {
        case 8: {
            // DSP.asm .OutStereo8: Add 0x80 for unsigned 8-bit PCM
            int val = static_cast<int>(std::round(clamped * 127.0f)) + 128;
            *out++ = static_cast<uint8_t>(std::clamp(val, 0, 255));
            break;
        }
        case 16: {
            // DSP.asm .OutStereo16: Standard signed 16-bit PCM
            int16_t val = static_cast<int16_t>(std::clamp(clamped * 32767.0f, -32768.0f, 32767.0f));
            std::memcpy(out, &val, 2);
            out += 2;
            break;
        }
        case 24: {
            // DSP.asm .OutStereo24: 24-bit signed PCM (3 bytes little-endian)
            int32_t val = static_cast<int32_t>(std::clamp(clamped * 8388607.0f, -8388608.0f, 8388607.0f));
            out[0] = static_cast<uint8_t>(val & 0xFF);
            out[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            out[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            out += 3;
            break;
        }
        case 32: {
            // DSP.asm .OutStereo32: 32-bit signed integer PCM
            int32_t val = static_cast<int32_t>(std::clamp(clamped * 2147483647.0f, -2147483648.0f, 2147483647.0f));
            std::memcpy(out, &val, 4);
            out += 4;
            break;
        }
        case -4: // 32-Bit Float
        default: {
            // DSP.asm .OutStereoFloat: IEEE 754 float in [-1.0f, 1.0f]
            std::memcpy(out, &clamped, 4);
            out += 4;
            break;
        }
    }
}
// Helper to apply bit-depth quantization in real-time
static inline void quantize_sample(float& sample, int bits) {
    float clamped = std::clamp(sample, -1.0f, 1.0f);
    switch (bits) {
        case 8: {
            // Authentic 8-bit unsigned quantization with grit & noise floor
            int val = static_cast<int>(std::round(clamped * 127.0f)) + 128;
            val = std::clamp(val, 0, 255);
            sample = (val - 128) / 127.0f;
            break;
        }
        case 16: {
            // Standard 16-bit signed PCM
            int16_t val = static_cast<int16_t>(std::clamp(std::round(clamped * 32767.0f), -32768.0f, 32767.0f));
            sample = val / 32768.0f;
            break;
        }
        case 24: {
            // 24-bit studio resolution
            int32_t val = static_cast<int32_t>(std::clamp(std::round(clamped * 8388607.0f), -8388608.0f, 8388607.0f));
            sample = static_cast<float>(val / 8388608.0);
            break;
        }
        case 32: {
            // 32-bit integer
            int32_t val = static_cast<int32_t>(std::clamp(std::round(clamped * 2147483647.0f), -2147483648.0f, 2147483647.0f));
            sample = static_cast<float>(val / 2147483648.0);
            break;
        }
        case -4: // 32-bit float
        default:
            sample = clamped;
            break;
    }
}

void SnesApu::render_float(float* out_l, float* out_r, size_t num_samples) {
    // Native 32 kHz path
    if (output_sample_rate == 32000) {
        size_t samples_remaining = num_samples;
        int16_t raw_pcm[BATCH_SAMPLES * 2];
        size_t out_idx = 0;

        while (samples_remaining > 0) {
            size_t chunk = std::min(samples_remaining, BATCH_SAMPLES);
            uint32_t cycles = static_cast<uint32_t>(chunk * CLK_PER_SAMPLE_32K);

            step_apu_cycles(cycles);
            dsp.render(raw_pcm, chunk);

            for (size_t s = 0; s < chunk; ++s) {
                float smp_l = raw_pcm[s * 2]     / 32768.0f;
                float smp_r = raw_pcm[s * 2 + 1] / 32768.0f;

                if (channels == 1) {
                    float mono = (smp_l + smp_r) * 0.5f;
                    smp_l = smp_r = mono;
                }

                quantize_sample(smp_l, bit_depth);
                quantize_sample(smp_r, bit_depth);

                out_l[out_idx] = smp_l;
                out_r[out_idx] = smp_r;
                out_idx++;
            }
            samples_remaining -= chunk;
        }
        return;
    }

    // Resampling path (8,000 Hz, 44,100 Hz, 48,000 Hz, 96,000 Hz, etc.)
    const double step = 32000.0 / static_cast<double>(output_sample_rate);

    for (size_t i = 0; i < num_samples; ++i) {
        while (resample_phase >= 1.0) {
            prev_sample_l = curr_sample_l;
            prev_sample_r = curr_sample_r;

            step_apu_cycles(CLK_PER_SAMPLE_32K);

            int16_t smp[2];
            dsp.render(smp, 1);
            curr_sample_l = smp[0];
            curr_sample_r = smp[1];

            resample_phase -= 1.0;
        }

        float frac  = static_cast<float>(resample_phase);
        float smp_l = (prev_sample_l + (curr_sample_l - prev_sample_l) * frac) / 32768.0f;
        float smp_r = (prev_sample_r + (curr_sample_r - prev_sample_r) * frac) / 32768.0f;

        if (channels == 1) {
            float mono = (smp_l + smp_r) * 0.5f;
            smp_l = smp_r = mono;
        }

        quantize_sample(smp_l, bit_depth);
        quantize_sample(smp_r, bit_depth);

        out_l[i] = smp_l;
        out_r[i] = smp_r;

        resample_phase += step;
    }
}
void SnesApu::render(void* buffer, size_t num_samples) {
    uint8_t* out_ptr = reinterpret_cast<uint8_t*>(buffer);

    // If native 32 kHz rate:
    if (output_sample_rate == 32000) {
        size_t samples_remaining = num_samples;
        int16_t raw_pcm[BATCH_SAMPLES * 2];

        while (samples_remaining > 0) {
            size_t chunk = std::min(samples_remaining, BATCH_SAMPLES);
            uint32_t cycles = static_cast<uint32_t>(chunk * CLK_PER_SAMPLE_32K);

            step_apu_cycles(cycles);
            dsp.render(raw_pcm, chunk);

            for (size_t s = 0; s < chunk; ++s) {
                float smp_l = raw_pcm[s * 2]     / 32768.0f;
                float smp_r = raw_pcm[s * 2 + 1] / 32768.0f;

                if (channels == 1) {
                    float mono = (smp_l + smp_r) * 0.5f;
                    write_sample(out_ptr, mono, bit_depth);
                } else {
                    write_sample(out_ptr, smp_l, bit_depth);
                    write_sample(out_ptr, smp_r, bit_depth);
                }
            }
            samples_remaining -= chunk;
        }
        return;
    }

    // Resampling path (for rates != 32 kHz)
    const double step = 32000.0 / static_cast<double>(output_sample_rate);

    for (size_t i = 0; i < num_samples; ++i) {
        while (resample_phase >= 1.0) {
            prev_sample_l = curr_sample_l;
            prev_sample_r = curr_sample_r;

            step_apu_cycles(CLK_PER_SAMPLE_32K);

            int16_t smp[2];
            dsp.render(smp, 1);
            curr_sample_l = smp[0];
            curr_sample_r = smp[1];

            resample_phase -= 1.0;
        }

        float frac  = static_cast<float>(resample_phase);
        float smp_l = (prev_sample_l + (curr_sample_l - prev_sample_l) * frac) / 32768.0f;
        float smp_r = (prev_sample_r + (curr_sample_r - prev_sample_r) * frac) / 32768.0f;

        if (channels == 1) {
            float mono = (smp_l + smp_r) * 0.5f;
            write_sample(out_ptr, mono, bit_depth);
        } else {
            write_sample(out_ptr, smp_l, bit_depth);
            write_sample(out_ptr, smp_r, bit_depth);
        }

        resample_phase += step;
    }
}
void SnesApu::reset() {
    spc.reset();
    dsp.reset();
    script.reset();

    cyc_left = 0;
    song_length_ticks = 0xFFFFFFFF;
    fade_length_ticks = 0;
    resample_phase = 0.0;
    prev_sample_l = prev_sample_r = 0;
    curr_sample_l = curr_sample_r = 0;
    dsp.set_amplification(1.0f);
}

void SnesApu::set_output_sample_rate(uint32_t rate) {
    output_sample_rate = std::clamp(rate, 8000u, 192000u);
}

void SnesApu::set_song_length(uint32_t song_ticks, uint32_t fade_ticks) {
    song_length_ticks = song_ticks;
    fade_length_ticks = fade_ticks;
}

void SnesApu::set_speed(float speed) {
    // Clamping limits from SetAPUSmpClk in APU.asm:
    // Min 1024 / 65536 (~1.5% speed), Max 1048576 / 65536 (16.0x speed)
    speed = std::clamp(speed, 1024.0f / 65536.0f, 1048576.0f / 65536.0f);
    clock_speed = static_cast<uint32_t>(speed * 65536.0f);
}

bool SnesApu::load_spc(const uint8_t* data, size_t size) {
    if (!data || size < 0x10180) {
        return false;
    }

    reset();

    // 1. Restore 64 KB APU RAM from offset 0x100 (memcpy(&apuRAM, &spc[0x100], 0x10000))
    std::memcpy(spc.get_ram(), data + 0x0100, 0x10000);

    // 2. Restore 128 DSP registers via write_reg, ensuring KON (0x4C) is written last
    // (Never zero the echo buffer: an SPC dump contains active reverberation history)
    for (uint8_t reg = 0; reg < 128; ++reg) {
        if (reg != 0x4C) {
            dsp.write_reg(reg, data[0x10100 + reg]);
        }
    }
    dsp.write_reg(0x4C, data[0x10100 + 0x4C]);

    // 3. Restore Extra RAM (IPL ROM area) at 0xFFC0 if present
    if (size >= 0x10200) {
        std::memcpy(spc.get_ram() + 0xFFC0, data + 0x101C0, 64);
    }

    // 4. Restore SPC700 CPU registers (offsets 0x25..0x2B)
    auto& state = spc.get_state();
    state.pc = static_cast<uint16_t>(data[0x25] | (data[0x26] << 8));
    state.a  = data[0x27];
    state.x  = data[0x28];
    state.y  = data[0x29];
    state.psw.from_byte(data[0x2A]);
    state.sp = data[0x2B];

    // 5. Reconstruct voice envelopes and internal hardware timers
    spc.fix_after_load();
    dsp.fix_after_load();

    // Note: Do NOT execute a dummy priming sample here.
    // Priming consumes CPU cycles and drops the song's first sample.

    return true;
}

bool SnesApu::load_script700(std::string_view script_text) {
    return script.load_script(script_text);
}

void SnesApu::update_fade() {
    uint32_t current_ticks = spc.get_state().t64_cnt;
    if (current_ticks > song_length_ticks && fade_length_ticks > 0) {
        uint32_t fade_elapsed = current_ticks - song_length_ticks;
        if (fade_elapsed >= fade_length_ticks) {
            dsp.set_amplification(0.0f);
        } else {
            float vol = 1.0f - (static_cast<float>(fade_elapsed) / static_cast<float>(fade_length_ticks));
            dsp.set_amplification(vol);
        }
    }
}

void SnesApu::step_apu_cycles(uint32_t cycles) {
    // Adjust cycles by speed multiplier (EAX = EAX * smpRAdj / 65536)
    if (clock_speed != 0x10000) {
        cycles = static_cast<uint32_t>((static_cast<uint64_t>(cycles) * clock_speed) >> 16);
    }

    int32_t eff_cycles = static_cast<int32_t>(cycles) + cyc_left;
    if (eff_cycles > 0) {
        // Run SPC700 execution; save leftover overshoot cycles to cyc_left
        cyc_left = spc.execute(eff_cycles);
    } else {
        cyc_left = eff_cycles;
    }

    // Step Script700 synchronized to 64 kHz timer ticks (every 384 cycles)
    if (script.is_active()) {
        uint32_t ticks = cycles / CLK_PER_TICK_64K;
        if (ticks > 0) {
            script.step(ticks, spc, dsp);
        }
    }

    update_fade();
}

void SnesApu::render(int16_t* buffer, size_t num_samples) {
    // Direct batch generation when running at native 32 kHz
    if (output_sample_rate == 32000) {
        size_t samples_remaining = num_samples;
        int16_t* out_ptr = buffer;

        while (samples_remaining > 0) {
            size_t chunk = std::min(samples_remaining, BATCH_SAMPLES);
            uint32_t cycles = static_cast<uint32_t>(chunk * CLK_PER_SAMPLE_32K);

            step_apu_cycles(cycles);
            dsp.render(out_ptr, chunk);

            out_ptr += chunk * 2;
            samples_remaining -= chunk;
        }
        return;
    }

    // Resampling for non-32 kHz rates
    const double step = 32000.0 / static_cast<double>(output_sample_rate);

    for (size_t i = 0; i < num_samples; ++i) {
        while (resample_phase >= 1.0) {
            prev_sample_l = curr_sample_l;
            prev_sample_r = curr_sample_r;

            step_apu_cycles(CLK_PER_SAMPLE_32K);

            int16_t smp[2];
            dsp.render(smp, 1);
            curr_sample_l = smp[0];
            curr_sample_r = smp[1];

            resample_phase -= 1.0;
        }

        float frac  = static_cast<float>(resample_phase);
        float out_l = prev_sample_l + (curr_sample_l - prev_sample_l) * frac;
        float out_r = prev_sample_r + (curr_sample_r - prev_sample_r) * frac;

        buffer[i * 2]     = static_cast<int16_t>(std::clamp(out_l, -32768.0f, 32767.0f));
        buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(out_r, -32768.0f, 32767.0f));

        resample_phase += step;
    }
}

void SnesApu::seek(uint32_t target_ticks) {
    uint32_t cur = spc.get_state().t64_cnt;
    if (target_ticks <= cur) return;

    uint32_t diff_ticks   = target_ticks - cur;
    uint32_t total_cycles = diff_ticks * CLK_PER_TICK_64K;

    // Fast seek: runs SPC700 without rendering audio samples
    while (total_cycles > 0) {
        uint32_t chunk = std::min(total_cycles, APU_CLK);
        total_cycles -= chunk;

        int32_t eff_cycles = static_cast<int32_t>(chunk) + cyc_left;
        if (eff_cycles > 0) {
            cyc_left = spc.execute(eff_cycles);
        } else {
            cyc_left = eff_cycles;
        }

        if (script.is_active()) {
            uint32_t ticks = chunk / CLK_PER_TICK_64K;
            if (ticks > 0) {
                script.step(ticks, spc, dsp);
            }
        }
    }

    update_fade();
}
