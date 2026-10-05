#pragma once
#include <cstdint>
#include <functional>
#include "Spc700Types.hpp"

// Forward declaration of DSP interface for register reads/writes
class Dsp;

class Spc700 {
public:
    Spc700();
    ~Spc700() = default;

    void reset();
    void set_dsp(Dsp* dsp_ptr) { dsp = dsp_ptr; }

    // Run the SPC700 for a given number of APU cycles (24.576 MHz / 24 = 1.024 MHz base)
    // Returns remaining cycles (negative if more cycles than requested were executed)
    int32_t execute(int32_t apu_cycles);

    // Memory access
    uint8_t read_byte(uint16_t addr);
    void write_byte(uint16_t addr, uint8_t val);
    uint16_t read_word(uint16_t addr);
    void write_word(uint16_t addr, uint16_t val);
    void fix_after_load();

    // External ports ($F4 - $F7)
    uint8_t read_port(uint8_t port) const { return in_ports[port & 3]; }
    void write_port(uint8_t port, uint8_t val) {
        in_ports[port & 3] = val;
        state.sleeping = false; // Port write by 65816 wakes SPC700 from SLEEP mode
    }
    uint8_t get_out_port(uint8_t port) const { return out_ports[port & 3]; }

    // State inspection
    Spc700State& get_state() { return state; }
    const Spc700State& get_state() const { return state; }
    uint8_t* get_ram() { return ram; }

    // Custom memory hook for Script700 communication events
    std::function<void(uint16_t addr, uint8_t val)> on_port_write = nullptr;

private:
    Spc700State state;
    Dsp* dsp = nullptr;

    alignas(64) uint8_t ram[0x10000];
    uint8_t extra_ram[64]; // Stores RAM writes behind the IPL ROM

    // I/O Communication latches
    uint8_t in_ports[4];
    uint8_t out_ports[4];
    uint8_t dsp_addr = 0;
    uint8_t control_reg = 0x80; // Bit 7: IPL ROM read enabled by default

    // Helper functions for opcodes
    uint8_t fetch_byte();
    uint16_t fetch_word();

    void push_byte(uint8_t val);
    uint8_t pop_byte();
    void push_word(uint16_t val);
    uint16_t pop_word();

    // Flag calculation helpers
    void set_nz(uint8_t val);
    void set_nz16(uint16_t val);

    // Arithmetic helpers
    uint8_t do_adc(uint8_t a, uint8_t b);
    uint8_t do_sbc(uint8_t a, uint8_t b);
    uint16_t do_addw(uint16_t a, uint16_t b);
    uint16_t do_subw(uint16_t a, uint16_t b);

    // Direct page addressing helpers
    uint16_t addr_dp(uint8_t offset) const;
    uint16_t addr_dp_inc(uint16_t dp_addr) const;
    uint16_t read_word_dp(uint16_t dp_addr);

    // Timer stepping
    void step_timers(int32_t cycles);

    // Opcode execution table
    void execute_opcode(uint8_t opcode, int32_t& cycles_left);
};