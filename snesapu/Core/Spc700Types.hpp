#pragma once
#include <cstdint>

// Clock rate: 24.576 MHz / 24 = 1.024 MHz base SPC clock
constexpr uint32_t APU_CLOCK_RATE = 24576000;
constexpr uint32_t SPC_CLOCK_DIVISOR = 24;
constexpr uint32_t SPC_CLOCK_HZ = APU_CLOCK_RATE / SPC_CLOCK_DIVISOR; // 1,024,000 Hz

// Timer tick divisors (in APU clock cycles)
constexpr int32_t T64_CYCLES = 384;              // 64 kHz timer (1,024,000 / 64,000 = 16 SPC cycles = 384 APU cycles)
constexpr int32_t T8_CYCLES  = 8 * T64_CYCLES;  // 8 kHz timer (3,072 APU cycles)

// SPC700 Processor Status Word (PSW)
struct SpcFlags {
    bool c = false; // Carry
    bool z = false; // Zero
    bool i = false; // Interrupt enable (unused on SNES hardware, but emulated)
    bool h = false; // Half-carry
    bool b = false; // Break
    bool p = false; // Direct Page selector (0 = $0000-$00FF, 1 = $0100-$01FF)
    bool v = false; // Overflow
    bool n = false; // Negative (sign)

    [[nodiscard]] uint8_t to_byte() const {
        return (c ? 0x01 : 0) |
               (z ? 0x02 : 0) |
               (i ? 0x04 : 0) |
               (h ? 0x08 : 0) |
               (b ? 0x10 : 0) |
               (p ? 0x20 : 0) |
               (v ? 0x40 : 0) |
               (n ? 0x80 : 0);
    }

    void from_byte(uint8_t val) {
        c = (val & 0x01) != 0;
        z = (val & 0x02) != 0;
        i = (val & 0x04) != 0;
        h = (val & 0x08) != 0;
        b = (val & 0x10) != 0;
        p = (val & 0x20) != 0;
        v = (val & 0x40) != 0;
        n = (val & 0x80) != 0;
    }
};

// SPC700 internal hardware timer state
struct SpcTimer {
    uint8_t target = 0;   // Reload value from $FA, $FB, $FC
    uint8_t counter = 0;  // 4-bit output counter ($FD, $FE, $FF)
    uint8_t step = 0;     // Internal down-counter to next increment
    bool enabled = false;
};

// SPC700 Execution State
struct Spc700State {
    uint16_t pc = 0xFFC0; // Power-on reset vector
    uint8_t  a  = 0;
    uint8_t  x  = 0;
    uint8_t  y  = 0;
    uint8_t  sp = 0xFF;
    SpcFlags psw;

    bool stopped  = false;
    bool sleeping = false;

    // Timers 0 and 1 run at 8 kHz; Timer 2 runs at 64 kHz
    SpcTimer timer[3];

    // Cycle tracking
    int32_t t8_cycle_counter  = T8_CYCLES - 1;
    int32_t t64_cycle_counter = T64_CYCLES - 1;
    uint32_t t64_cnt          = 0; // Total 64 kHz ticks (used for song timing & fades)
};