#pragma once
#include <cstdint>
#include <array>

constexpr size_t SCRIPT700_RAM_SIZE = 0x100000; // 1 MB program/data area
constexpr size_t SCRIPT700_MASK     = SCRIPT700_RAM_SIZE - 1;

struct Script700State {
    bool enabled = false;

    // Script700 Bytecode RAM
    std::array<uint8_t, SCRIPT700_RAM_SIZE> ram{};

    // Script700 Labels (up to 1024 branch targets)
    std::array<uint32_t, 1024> labels{};

    // DSP override flags per voice/source
    std::array<uint8_t, 256>  dsp_src_flags{};  // Source-level Mute / Detune / Vol flags
    std::array<uint8_t, 32>   dsp_master_flags{};
    std::array<uint32_t, 256> dsp_detune{};     // Detune rates
    std::array<uint8_t, 256>  dsp_note_change{}; // Source remapping
    std::array<uint32_t, 256> dsp_vol{};        // Source volume overrides
    std::array<uint32_t, 32>  dsp_master_vol{};

    // Virtual registers & work area
    std::array<uint32_t, 8> work_vars{};        // w0 - w7
    std::array<uint32_t, 2> cmp_params{};       // Compare values
    uint32_t wait_counter = 0;                  // Cycle wait countdown
    uint32_t prog_ptr = 0;                      // Current execution index in ram[]
    uint8_t  status_flags = 0x03;               // Flow control & port writing modes
    uint8_t  interrupt_ports[2] = {0, 0};       // wi/wo wait targets
    uint32_t data_offset = 0;                   // Base offset of embedded data blocks

    // Return call stack
    uint32_t stack_ptr = 0;
    std::array<uint32_t, 128> stack{};
};