#ifndef SNESAPU_C_API_H
#define SNESAPU_C_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct {
    uint32_t t64_ticks;
    uint8_t  key_on_mask;
    uint8_t  env_levels[8];
    int16_t  voice_outs[8];
    uint8_t  dsp_regs[128];
} SnesApuVisualState;
#pragma pack(pop)

void* snesapu_create(void);
void  snesapu_destroy(void* apu);
void  snesapu_reset(void* apu);
bool  snesapu_load_spc(void* apu, const uint8_t* data, size_t size);
bool  snesapu_load_script(void* apu, const char* script);
void  snesapu_render(void* apu, int16_t* buffer, size_t frames);
void  snesapu_set_speed(void* apu, float speed);
void  snesapu_set_amp(void* apu, float amp);
void  snesapu_get_visual_state(void* apu, SnesApuVisualState* state);

void  snesapu_set_sample_rate(void* apu, uint32_t rate);
void  snesapu_set_channel_mute(void* apu, uint8_t mask);
void  snesapu_set_channel_noise(void* apu, uint8_t mask);
void  snesapu_set_stereo_separation(void* apu, uint32_t sep);
void  snesapu_set_feedback_mixer(void* apu, uint32_t fb);
void  snesapu_set_pitch_base_hz(void* apu, uint32_t hz);
void  snesapu_set_pitch_multiplier(void* apu, float mul);
void  snesapu_set_pitch_sync_speed(void* apu, bool sync);
void  snesapu_set_dsp_options(void* apu, uint32_t options);
void  snesapu_set_interpolation(void* apu, uint8_t mode);
void  snesapu_seek(void* apu, uint32_t target_ticks);

#ifdef __cplusplus
}
#endif

#endif