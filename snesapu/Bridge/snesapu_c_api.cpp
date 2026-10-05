#include "SnesApu.hpp"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>

#pragma pack(push, 1)
struct SnesApuVisualState {
    uint32_t t64_ticks;
    uint8_t  key_on_mask;
    uint8_t  env_levels[8];
    int16_t  voice_outs[8];
    uint8_t  dsp_regs[128];
};
#pragma pack(pop)

extern "C" {

void* snesapu_create(void) {
    return reinterpret_cast<void*>(new SnesApu());
}

void snesapu_destroy(void* handle) {
    if (handle) {
        delete reinterpret_cast<SnesApu*>(handle);
    }
}

void snesapu_reset(void* handle) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->reset();
    }
}

bool snesapu_load_spc(void* handle, const uint8_t* data, size_t size) {
    if (!handle || !data) return false;
    return reinterpret_cast<SnesApu*>(handle)->load_spc(data, size);
}

bool snesapu_load_script(void* handle, const char* script_text) {
    if (!handle || !script_text) return false;
    return reinterpret_cast<SnesApu*>(handle)->load_script700(script_text);
}

void snesapu_render(void* handle, int16_t* out_stereo_buf, size_t num_samples) {
    if (handle && out_stereo_buf) {
        reinterpret_cast<SnesApu*>(handle)->render(out_stereo_buf, num_samples);
    }
}

void snesapu_set_sample_rate(void* handle, uint32_t rate) {
    (void)handle;
    (void)rate;
    // SnesApu natively renders at 32000Hz; AVAudioEngine handles resample/output
}

void snesapu_set_speed(void* handle, float multiplier) {
    if (handle) {
        auto* apu = reinterpret_cast<SnesApu*>(handle);
        apu->set_speed(multiplier);
        apu->get_dsp().set_speed_multiplier(multiplier);
    }
}

void snesapu_set_amp(void* handle, float volume) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->get_dsp().set_amplification(volume);
    }
}

void snesapu_get_visual_state(void* handle, SnesApuVisualState* out_state) {
    if (!handle || !out_state) return;

    auto* apu = reinterpret_cast<SnesApu*>(handle);
    auto& spc = apu->get_spc();
    auto& dsp = apu->get_dsp();

    out_state->t64_ticks = spc.get_state().t64_cnt;
    
    const uint8_t* regs = dsp.get_registers();
    if (regs) {
        std::memcpy(out_state->dsp_regs, regs, 128);
        out_state->key_on_mask = regs[0x4C];
        for (int i = 0; i < 8; ++i) {
            out_state->env_levels[i] = regs[(i << 4) | 0x08]; // ENVX
            out_state->voice_outs[i] = static_cast<int8_t>(regs[(i << 4) | 0x09]) << 8; // OUTX
        }
    }
}

void snesapu_set_channel_mute(void* handle, uint8_t mute_mask) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->get_dsp().set_channel_mute(mute_mask);
    }
}

void snesapu_set_channel_noise(void* handle, uint8_t noise_mask) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->get_dsp().set_channel_noise(noise_mask);
    }
}

void snesapu_set_stereo_separation(void* handle, uint32_t sep) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->get_dsp().set_stereo_separation(sep);
    }
}

void snesapu_set_feedback_mixer(void* handle, uint32_t fb) {
    (void)handle;
    (void)fb;
}

void snesapu_set_pitch_base_hz(void* handle, uint32_t hz) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->get_dsp().set_pitch_base_hz(hz);
    }
}

void snesapu_set_pitch_multiplier(void* handle, float mul) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->get_dsp().set_pitch_multiplier(mul);
    }
}

void snesapu_set_pitch_sync_speed(void* handle, bool sync) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->get_dsp().set_pitch_sync_speed(sync);
    }
}

void snesapu_set_dsp_options(void* handle, uint32_t options) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->get_dsp().set_dsp_options(options);
    }
}

void snesapu_set_interpolation(void* handle, uint8_t mode) {
    if (handle) {
        reinterpret_cast<SnesApu*>(handle)->get_dsp().set_interpolation_mode(
            static_cast<InterpolationMode>(mode)
        );
    }
}

void snesapu_seek(void* handle, uint32_t target_ticks) {
    if (!handle) return;
    auto* apu = reinterpret_cast<SnesApu*>(handle);
    uint32_t cur = apu->get_spc().get_state().t64_cnt;
    if (target_ticks > cur) {
        uint32_t diff_ticks = target_ticks - cur;
        size_t samples = static_cast<size_t>(diff_ticks / 2);
        
        std::vector<int16_t> dummy(1024 * 2);
        while (samples > 0) {
            size_t step = std::min<size_t>(samples, 1024);
            apu->render(dummy.data(), step);
            samples -= step;
        }
    }
}

} // extern "C"
