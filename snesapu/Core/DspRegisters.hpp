#pragma once
#include <cstdint>

#pragma pack(push, 1)

// Per-voice registers (16 bytes per voice)
struct DspVoiceRegs {
    int8_t   vol_l;       // 0x00: Volume Left (-.7 signed)
    int8_t   vol_r;       // 0x01: Volume Right (-.7 signed)
    uint16_t pitch;       // 0x02: Pitch step (3.11 fixed-point, 14-bit max)
    uint8_t  srcn;        // 0x04: Source instrument index
    uint8_t  adsr1;       // 0x05: ADSR enable & Attack/Decay rates
    uint8_t  adsr2;       // 0x06: Sustain level & Sustain rate
    uint8_t  gain;        // 0x07: Direct / Linear / Bent / Exponential Gain
    int8_t   envx;        // 0x08: Current envelope height (.7)
    int8_t   outx;        // 0x09: Current sample output after envelope (-.7)
    uint8_t  reserved[6]; // 0x0A-0x0F
};

// 128-byte S-DSP register map
union DspRegs {
    uint8_t raw[128];
    DspVoiceRegs voice[8];

    struct {
        DspVoiceRegs v0;
        int8_t  mvol_l;   // 0x0C: Master Volume Left
        int8_t  efb;      // 0x0D: Echo Feedback
        uint8_t _pad0E;
        int8_t  fir[8];   // 0x0F, 0x1F, 0x2F, 0x3F, 0x4F, 0x5F, 0x6F, 0x7F

        DspVoiceRegs v1;
        int8_t  mvol_r;   // 0x1C: Master Volume Right
        uint8_t _pad1D, _pad1E;

        DspVoiceRegs v2;
        int8_t  evol_l;   // 0x2C: Echo Volume Left
        uint8_t pmon;     // 0x2D: Pitch modulation flags (voices 1-7)
        uint8_t _pad2E;

        DspVoiceRegs v3;
        int8_t  evol_r;   // 0x3C: Echo Volume Right
        uint8_t non;      // 0x3D: Noise enable flags
        uint8_t _pad3E;

        DspVoiceRegs v4;
        uint8_t kon;      // 0x4C: Key On flags
        uint8_t eon;      // 0x4D: Echo enable flags
        uint8_t _pad4E;

        DspVoiceRegs v5;
        uint8_t kof;      // 0x5C: Key Off flags
        uint8_t dir;      // 0x5D: Sample directory page base ($xx00)
        uint8_t _pad5E;

        DspVoiceRegs v6;
        uint8_t flg;      // 0x6C: Reset, Mute, Echo Write Disable, Noise Clock
        uint8_t esa;      // 0x6D: Echo Ring Buffer start address page
        uint8_t _pad6E;

        DspVoiceRegs v7;
        uint8_t endx;     // 0x7C: Voice end-of-sample flags
        uint8_t edl;      // 0x7D: Echo Delay
        uint8_t _pad7E;
    };
};

#pragma pack(pop)