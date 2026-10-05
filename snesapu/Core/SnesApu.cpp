#include "SnesApu.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

SnesApu::SnesApu() {
    spc.set_dsp(&dsp);
    dsp.set_apu_ram(spc.get_ram());

    spc.on_port_write = [this](uint16_t addr, uint8_t val) {
        script.on_port_write(static_cast<uint8_t>(addr & 3), val);
    };

    reset();
}

void SnesApu::reset() {
    spc.reset();
    dsp.reset();
    script.reset();
    cycle_debt = 0;
    song_length_ticks = 0xFFFFFFFF;
    fade_length_ticks = 0;
    resample_phase = 0.0;
    prev_sample_l = prev_sample_r = 0;
    curr_sample_l = curr_sample_r = 0;
}

bool SnesApu::load_spc(const uint8_t* data, size_t size) {
    if (!data || size < 66048) {
        return false;
    }

    reset();

    std::memcpy(spc.get_ram(), data + 0x0100, 0x10000);

    // Clear stale echo buffer
    const uint8_t* dsp_regs = data + 0x10100;
    const bool echo_write_enabled = !(dsp_regs[0x6C] & 0x20);

    if (echo_write_enabled) {
        const uint16_t esa  = static_cast<uint16_t>(dsp_regs[0x6D] << 8);
        const uint8_t  edl  = dsp_regs[0x7D] & 0x0F;
        const uint32_t echo_sz = (edl == 0) ? 4 : (edl * 2048u);

        uint8_t* ram = spc.get_ram();
        for (uint32_t i = 0; i < echo_sz; ++i)
            ram[(esa + i) & 0xFFFF] = 0;
    }

    for (uint8_t reg = 0; reg < 128; ++reg) {
        if (reg != 0x4C) {
            dsp.write_reg(reg, data[0x10100 + reg]);
        }
    }

    dsp.write_reg(0x4C, data[0x10100 + 0x4C]);

    if (size >= 0x10200) {
        std::memcpy(spc.get_ram() + 0xFFC0, data + 0x101C0, 64);
    }

    auto& state = spc.get_state();
    state.pc = data[0x25] | (data[0x26] << 8);
    state.a  = data[0x27];
    state.x  = data[0x28];
    state.y  = data[0x29];
    state.psw.from_byte(data[0x2A]);
    state.sp = data[0x2B];

    spc.fix_after_load();
    dsp.fix_after_load();

    // Prime the resampler
    step_apu_one_sample();
    prev_sample_l = curr_sample_l;
    prev_sample_r = curr_sample_r;

    return true;
}

bool SnesApu::load_script700(std::string_view script_text) {
    return script.load_script(script_text);
}

void SnesApu::set_output_sample_rate(uint32_t rate) {
    output_sample_rate = (rate < 8000) ? 8000 : ((rate > 192000) ? 192000 : rate);
}

void SnesApu::set_song_length(uint32_t song_ticks, uint32_t fade_ticks) {
    song_length_ticks = song_ticks;
    fade_length_ticks = fade_ticks;
}

void SnesApu::set_speed(float speed) {
    speed = std::clamp(speed, 0.01f, 8.0f);
    clock_speed = static_cast<uint32_t>(speed * 65536.0f);
}

void SnesApu::step_apu_one_sample() {
    constexpr int32_t APU_CYCLES_PER_SAMPLE = 768; // 24,576,000 / 32,000

    uint32_t current_ticks = spc.get_state().t64_cnt;
    if (current_ticks > song_length_ticks && fade_length_ticks > 0) {
        uint32_t fade_elapsed = current_ticks - song_length_ticks;
        if (fade_elapsed >= fade_length_ticks) {
            dsp.set_amplification(0.0f);
        } else {
            float vol = 1.0f - (static_cast<float>(fade_elapsed) / fade_length_ticks);
            dsp.set_amplification(vol);
        }
    }

    cycle_debt += (APU_CYCLES_PER_SAMPLE * clock_speed) >> 16;

    while (cycle_debt >= 384) {
        int32_t leftover = spc.execute(384);
        int32_t executed = 384 - leftover;
        cycle_debt -= executed;
        if (script.is_active()) {
            script.step(1, spc, dsp);
        }
    }

    int16_t smp[2];
    dsp.render(smp, 1);
    curr_sample_l = smp[0];
    curr_sample_r = smp[1];
}

void SnesApu::render(int16_t* buffer, size_t num_samples) {
    if (output_sample_rate == 32000) {
        for (size_t i = 0; i < num_samples; ++i) {
            step_apu_one_sample();
            buffer[i * 2]     = curr_sample_l;
            buffer[i * 2 + 1] = curr_sample_r;
        }
        return;
    }

    // High quality band-limited resampler (32 kHz -> output_sample_rate)
    const double step = 32000.0 / static_cast<double>(output_sample_rate);

    for (size_t i = 0; i < num_samples; ++i) {
        while (resample_phase >= 1.0) {
            prev_sample_l = curr_sample_l;
            prev_sample_r = curr_sample_r;
            step_apu_one_sample();
            resample_phase -= 1.0;
        }

        float frac = static_cast<float>(resample_phase);
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

    uint32_t diff_ticks = target_ticks - cur;
    size_t samples_to_fast_forward = static_cast<size_t>(diff_ticks / 2);

    while (samples_to_fast_forward > 0) {
        step_apu_one_sample();
        samples_to_fast_forward--;
    }
}