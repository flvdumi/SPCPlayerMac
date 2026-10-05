#include "Spc700.hpp"
#include "Dsp.hpp"
#include <cstring>
#include <algorithm>

extern const uint8_t IPL_ROM[64];

Spc700::Spc700() {
    reset();
}

void Spc700::reset() {
    std::memset(ram, 0xFF, sizeof(ram));
    std::memset(extra_ram, 0, sizeof(extra_ram));
    std::memset(in_ports, 0, sizeof(in_ports));
    std::memset(out_ports, 0, sizeof(out_ports));

    state.pc = 0xFFC0;
    state.a = 0;
    state.x = 0;
    state.y = 0;
    state.sp = 0xFF;
    state.psw.from_byte(0);

    state.stopped = false;
    state.sleeping = false;
    state.t64_cnt = 0;
    state.t8_cycle_counter = T8_CYCLES - 1;
    state.t64_cycle_counter = T64_CYCLES - 1;

    for (int i = 0; i < 3; ++i) {
        state.timer[i].target = 0;
        state.timer[i].counter = 0;
        state.timer[i].step = 0;
        state.timer[i].enabled = false;
    }

    control_reg = 0x80;
    dsp_addr = 0;

    // Default MMIO state in RAM
    ram[0x00F0] = 0x0A;
    ram[0x00F1] = 0x80;
    ram[0x00F2] = 0x00;
}

inline uint16_t Spc700::addr_dp(uint8_t offset) const {
    return (state.psw.p ? 0x0100 : 0x0000) | offset;
}

inline void Spc700::set_nz(uint8_t val) {
    state.psw.z = (val == 0);
    state.psw.n = (val & 0x80) != 0;
}

inline void Spc700::set_nz16(uint16_t val) {
    state.psw.z = (val == 0);
    state.psw.n = (val & 0x8000) != 0;
}

uint8_t Spc700::read_byte(uint16_t addr) {
    if (addr >= 0xFFC0 && (control_reg & 0x80)) {
        return IPL_ROM[addr - 0xFFC0];
    }

    if ((addr & 0xFFF0) == 0x00F0) {
        switch (addr & 0x0F) {
            case 0x0: return 0x0A;
            case 0x1: return control_reg;
            case 0x2: return dsp_addr;
            case 0x3: // Read DSP Data register
                if (dsp) return dsp->read_reg(dsp_addr & 0x7F);
                return ram[0x00F3];
            case 0x4: case 0x5: case 0x6: case 0x7:
                return in_ports[addr & 3];
            case 0x8: case 0x9:
                return ram[addr];
            case 0xA: case 0xB: case 0xC:
                return 0; // Targets are write-only
            case 0xD: case 0xE: case 0xF: {
                uint8_t val = state.timer[(addr & 0x0F) - 0xD].counter & 0x0F;
                state.timer[(addr & 0x0F) - 0xD].counter = 0;
                return val;
            }
        }
    }

    return ram[addr];
}

void Spc700::write_byte(uint16_t addr, uint8_t val) {
    if (addr >= 0xFFC0) {
        if (control_reg & 0x80) {
            extra_ram[addr - 0xFFC0] = val;
        }
        ram[addr] = val;
        return;
    }

    if ((addr & 0xFFF0) == 0x00F0) {
        switch (addr & 0x0F) {
            case 0x0:
                ram[0x00F0] = val;
                break;

            case 0x1: {
                ram[0x00F1] = val;
                control_reg = val;
                if (val & 0x10) { in_ports[0] = 0; in_ports[1] = 0; }
                if (val & 0x20) { in_ports[2] = 0; in_ports[3] = 0; }

                for (int i = 0; i < 3; ++i) {
                    bool enable = (val & (1 << i)) != 0;
                    if (!state.timer[i].enabled && enable) {
                        state.timer[i].step = state.timer[i].target ? (state.timer[i].target - 1) : 255;
                        state.timer[i].counter = 0;
                    }
                    state.timer[i].enabled = enable;
                }
                break;
            }

            case 0x2: // DSP address
                dsp_addr = val;
                ram[0x00F2] = val;
                break;

            case 0x3: // DSP data
                ram[0x00F3] = val;
                // Bit 7 of DSP address acts as a write-protect flag
                if (dsp && !(dsp_addr & 0x80)) {
                    dsp->write_reg(dsp_addr & 0x7F, val);
                }
                break;

            case 0x4: case 0x5: case 0x6: case 0x7: {
                uint8_t port = addr & 3;
                out_ports[port] = val;
                ram[addr] = val;
                if (on_port_write) {
                    on_port_write(addr, val);
                }
                break;
            }

            case 0x8: case 0x9:
                ram[addr] = val;
                break;

            case 0xA: case 0xB: case 0xC: {
                int t = (addr & 0x0F) - 0xA;
                ram[addr] = val;
                state.timer[t].target = val;
                break;
            }

            case 0xD: case 0xE: case 0xF:
                // Writes to counter registers clear the internal counters
                state.timer[(addr & 0x0F) - 0xD].counter = 0;
                break;
        }
        return;
    }

    ram[addr] = val;
}

uint16_t Spc700::read_word(uint16_t addr) {
    uint8_t lo = read_byte(addr);
    uint8_t hi = read_byte(addr + 1);
    return lo | (hi << 8);
}

void Spc700::write_word(uint16_t addr, uint16_t val) {
    write_byte(addr, static_cast<uint8_t>(val));
    write_byte(addr + 1, static_cast<uint8_t>(val >> 8));
}

uint8_t Spc700::fetch_byte() {
    return read_byte(state.pc++);
}

uint16_t Spc700::fetch_word() {
    uint16_t val = read_word(state.pc);
    state.pc += 2;
    return val;
}

void Spc700::push_byte(uint8_t val) {
    ram[0x0100 | state.sp--] = val;
}

uint8_t Spc700::pop_byte() {
    return ram[0x0100 | ++state.sp];
}

void Spc700::push_word(uint16_t val) {
    push_byte(static_cast<uint8_t>(val >> 8));
    push_byte(static_cast<uint8_t>(val));
}

uint16_t Spc700::pop_word() {
    uint8_t lo = pop_byte();
    uint8_t hi = pop_byte();
    return lo | (hi << 8);
}

uint8_t Spc700::do_adc(uint8_t a, uint8_t b) {
    int carry = state.psw.c ? 1 : 0;
    int result = a + b + carry;

    state.psw.v = (~(a ^ b) & (a ^ result) & 0x80) != 0;
    state.psw.h = ((a & 0x0F) + (b & 0x0F) + carry) > 0x0F;
    state.psw.c = result > 0xFF;
    uint8_t res8 = static_cast<uint8_t>(result);
    set_nz(res8);
    return res8;
}

uint8_t Spc700::do_sbc(uint8_t a, uint8_t b) {
    int carry = state.psw.c ? 1 : 0;
    int result = a - b - (1 - carry);

    state.psw.v = ((a ^ b) & (a ^ result) & 0x80) != 0;
    state.psw.h = ((a & 0x0F) - (b & 0x0F) - (1 - carry)) >= 0;
    state.psw.c = result >= 0;
    uint8_t res8 = static_cast<uint8_t>(result);
    set_nz(res8);
    return res8;
}

uint16_t Spc700::do_addw(uint16_t a, uint16_t b) {
    int result = a + b;
    state.psw.v = (~(a ^ b) & (a ^ result) & 0x8000) != 0;
    state.psw.c = result > 0xFFFF;
    state.psw.h = ((a & 0x0FFF) + (b & 0x0FFF)) > 0x0FFF;
    uint16_t res16 = static_cast<uint16_t>(result);
    set_nz16(res16);
    return res16;
}

uint16_t Spc700::do_subw(uint16_t a, uint16_t b) {
    int result = a - b;
    state.psw.v = ((a ^ b) & (a ^ result) & 0x8000) != 0;
    state.psw.c = result >= 0;
    state.psw.h = ((a & 0x0FFF) - (b & 0x0FFF)) >= 0;
    uint16_t res16 = static_cast<uint16_t>(result);
    set_nz16(res16);
    return res16;
}

void Spc700::step_timers(int32_t cycles) {
    // 64 kHz timer (Timer 2)
    state.t64_cycle_counter -= cycles;
    while (state.t64_cycle_counter < 0) {
        state.t64_cycle_counter += T64_CYCLES;
        state.t64_cnt++;

        if (state.timer[2].enabled) {
            if (state.timer[2].step == 0) {
                state.timer[2].step = state.timer[2].target ? (state.timer[2].target - 1) : 255;
                state.timer[2].counter = (state.timer[2].counter + 1) & 0x0F;
            } else {
                state.timer[2].step--;
            }
        }
    }

    // 8 kHz timers (Timers 0 and 1)
    state.t8_cycle_counter -= cycles;
    while (state.t8_cycle_counter < 0) {
        state.t8_cycle_counter += T8_CYCLES;

        for (int i = 0; i < 2; ++i) {
            if (state.timer[i].enabled) {
                if (state.timer[i].step == 0) {
                    state.timer[i].step = state.timer[i].target ? (state.timer[i].target - 1) : 255;
                    state.timer[i].counter = (state.timer[i].counter + 1) & 0x0F;
                } else {
                    state.timer[i].step--;
                }
            }
        }
    }
}

int32_t Spc700::execute(int32_t apu_cycles) {
    int32_t cycles = apu_cycles;

    while (cycles > 0) {
        if (state.stopped) {
            step_timers(cycles);
            cycles = 0;
            break;
        }

        if (state.sleeping) {
            int32_t step = std::min(cycles, static_cast<int32_t>(T64_CYCLES));
            step_timers(step);
            cycles -= step;
            continue;
        }

        int32_t prev_cycles = cycles;
        uint8_t opcode = fetch_byte();
        execute_opcode(opcode, cycles);

        int32_t elapsed = prev_cycles - cycles;
        step_timers(elapsed);
    }

    return cycles;
}

void Spc700::execute_opcode(uint8_t opcode, int32_t& cycles) {
    // Direct page 16-bit word read/write with 8-bit page wrapping
    auto read_dp_word = [this](uint8_t offset) -> uint16_t {
        uint8_t lo = read_byte(addr_dp(offset));
        uint8_t hi = read_byte(addr_dp(static_cast<uint8_t>(offset + 1)));
        return lo | (static_cast<uint16_t>(hi) << 8);
    };

    auto write_dp_word = [this](uint8_t offset, uint16_t val) {
        write_byte(addr_dp(offset), static_cast<uint8_t>(val));
        write_byte(addr_dp(static_cast<uint8_t>(offset + 1)), static_cast<uint8_t>(val >> 8));
    };

    switch (opcode) {
        case 0x00: cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xEF: state.sleeping = true; cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0xFF: state.stopped = true; cycles -= 3 * SPC_CLOCK_DIVISOR; break;

        case 0x60: state.psw.c = false; cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x80: state.psw.c = true;  cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xED: state.psw.c = !state.psw.c; cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0x20: state.psw.p = false; cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x40: state.psw.p = true;  cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xE0: state.psw.v = false; state.psw.h = false; cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xA0: state.psw.i = true;  cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0xC0: state.psw.i = false; cycles -= 3 * SPC_CLOCK_DIVISOR; break;

        case 0xE8: state.a = fetch_byte(); set_nz(state.a); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xCD: state.x = fetch_byte(); set_nz(state.x); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x8D: state.y = fetch_byte(); set_nz(state.y); cycles -= 2 * SPC_CLOCK_DIVISOR; break;

        case 0x7D: state.a = state.x; set_nz(state.a); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xDD: state.a = state.y; set_nz(state.a); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x5D: state.x = state.a; set_nz(state.x); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xFD: state.y = state.a; set_nz(state.y); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x9D: state.x = state.sp; set_nz(state.x); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xBD: state.sp = state.x; cycles -= 2 * SPC_CLOCK_DIVISOR; break;

        case 0xE4: state.a = read_byte(addr_dp(fetch_byte())); set_nz(state.a); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0xF4: state.a = read_byte(addr_dp(fetch_byte() + state.x)); set_nz(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xE5: state.a = read_byte(fetch_word()); set_nz(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xF5: state.a = read_byte(fetch_word() + state.x); set_nz(state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0xF6: state.a = read_byte(fetch_word() + state.y); set_nz(state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0xE6: state.a = read_byte(addr_dp(state.x)); set_nz(state.a); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0xBF: state.a = read_byte(addr_dp(state.x++)); set_nz(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xE7: {
            uint8_t dp_off = fetch_byte() + state.x;
            state.a = read_byte(read_dp_word(dp_off));
            set_nz(state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xF7: {
            uint8_t dp_off = fetch_byte();
            state.a = read_byte(read_dp_word(dp_off) + state.y);
            set_nz(state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0xF8: state.x = read_byte(addr_dp(fetch_byte())); set_nz(state.x); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0xF9: state.x = read_byte(addr_dp(fetch_byte() + state.y)); set_nz(state.x); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xE9: state.x = read_byte(fetch_word()); set_nz(state.x); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xEB: state.y = read_byte(addr_dp(fetch_byte())); set_nz(state.y); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0xFB: state.y = read_byte(addr_dp(fetch_byte() + state.x)); set_nz(state.y); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xEC: state.y = read_byte(fetch_word()); set_nz(state.y); cycles -= 4 * SPC_CLOCK_DIVISOR; break;

        case 0xC4: write_byte(addr_dp(fetch_byte()), state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xD4: write_byte(addr_dp(fetch_byte() + state.x), state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0xC5: write_byte(fetch_word(), state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0xD5: write_byte(fetch_word() + state.x, state.a); cycles -= 6 * SPC_CLOCK_DIVISOR; break;
        case 0xD6: write_byte(fetch_word() + state.y, state.a); cycles -= 6 * SPC_CLOCK_DIVISOR; break;
        case 0xC6: write_byte(addr_dp(state.x), state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xAF: write_byte(addr_dp(state.x++), state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xC7: {
            uint8_t dp_off = fetch_byte() + state.x;
            write_byte(read_dp_word(dp_off), state.a);
            cycles -= 7 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xD7: {
            uint8_t dp_off = fetch_byte();
            write_byte(read_dp_word(dp_off) + state.y, state.a);
            cycles -= 7 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0xD8: write_byte(addr_dp(fetch_byte()), state.x); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xD9: write_byte(addr_dp(fetch_byte() + state.y), state.x); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0xC9: write_byte(fetch_word(), state.x); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0xCB: write_byte(addr_dp(fetch_byte()), state.y); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xDB: write_byte(addr_dp(fetch_byte() + state.x), state.y); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0xCC: write_byte(fetch_word(), state.y); cycles -= 5 * SPC_CLOCK_DIVISOR; break;

        case 0x8F: {
            uint8_t imm = fetch_byte();
            uint16_t dest = addr_dp(fetch_byte());
            write_byte(dest, imm);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xFA: {
            uint16_t src = addr_dp(fetch_byte());
            uint16_t dest = addr_dp(fetch_byte());
            write_byte(dest, read_byte(src));
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0xBA: {
            uint8_t dp_off = fetch_byte();
            state.a = read_byte(addr_dp(dp_off));
            state.y = read_byte(addr_dp(static_cast<uint8_t>(dp_off + 1)));
            set_nz16((state.y << 8) | state.a);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xDA: {
            uint8_t dp_off = fetch_byte();
            write_byte(addr_dp(dp_off), state.a);
            write_byte(addr_dp(static_cast<uint8_t>(dp_off + 1)), state.y);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x88: state.a = do_adc(state.a, fetch_byte()); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x84: state.a = do_adc(state.a, read_byte(addr_dp(fetch_byte()))); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0x94: state.a = do_adc(state.a, read_byte(addr_dp(fetch_byte() + state.x))); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x85: state.a = do_adc(state.a, read_byte(fetch_word())); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x95: state.a = do_adc(state.a, read_byte(fetch_word() + state.x)); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0x96: state.a = do_adc(state.a, read_byte(fetch_word() + state.y)); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0x86: state.a = do_adc(state.a, read_byte(addr_dp(state.x))); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0x87: {
            uint8_t dp_off = fetch_byte() + state.x;
            state.a = do_adc(state.a, read_byte(read_dp_word(dp_off)));
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x97: {
            uint8_t dp_off = fetch_byte();
            state.a = do_adc(state.a, read_byte(read_dp_word(dp_off) + state.y));
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x98: {
            uint8_t imm = fetch_byte();
            uint16_t dest = addr_dp(fetch_byte());
            write_byte(dest, do_adc(read_byte(dest), imm));
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x89: {
            uint16_t src = addr_dp(fetch_byte());
            uint16_t dest = addr_dp(fetch_byte());
            write_byte(dest, do_adc(read_byte(dest), read_byte(src)));
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x99: {
            uint16_t src = addr_dp(state.y);
            uint16_t dest = addr_dp(state.x);
            write_byte(dest, do_adc(read_byte(dest), read_byte(src)));
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0xA8: state.a = do_sbc(state.a, fetch_byte()); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xA4: state.a = do_sbc(state.a, read_byte(addr_dp(fetch_byte()))); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0xB4: state.a = do_sbc(state.a, read_byte(addr_dp(fetch_byte() + state.x))); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xA5: state.a = do_sbc(state.a, read_byte(fetch_word())); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xB5: state.a = do_sbc(state.a, read_byte(fetch_word() + state.x)); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0xB6: state.a = do_sbc(state.a, read_byte(fetch_word() + state.y)); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0xA6: state.a = do_sbc(state.a, read_byte(addr_dp(state.x))); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0xA7: {
            uint8_t dp_off = fetch_byte() + state.x;
            state.a = do_sbc(state.a, read_byte(read_dp_word(dp_off)));
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xB7: {
            uint8_t dp_off = fetch_byte();
            state.a = do_sbc(state.a, read_byte(read_dp_word(dp_off) + state.y));
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xB8: {
            uint8_t imm = fetch_byte();
            uint16_t dest = addr_dp(fetch_byte());
            write_byte(dest, do_sbc(read_byte(dest), imm));
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xA9: {
            uint16_t src = addr_dp(fetch_byte());
            uint16_t dest = addr_dp(fetch_byte());
            write_byte(dest, do_sbc(read_byte(dest), read_byte(src)));
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xB9: {
            uint16_t src = addr_dp(state.y);
            uint16_t dest = addr_dp(state.x);
            write_byte(dest, do_sbc(read_byte(dest), read_byte(src)));
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x7A: {
            uint8_t dp_off = fetch_byte();
            uint16_t val = read_dp_word(dp_off);
            uint16_t ya = (state.y << 8) | state.a;
            uint16_t res = do_addw(ya, val);
            state.a = res & 0xFF;
            state.y = res >> 8;
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x9A: {
            uint8_t dp_off = fetch_byte();
            uint16_t val = read_dp_word(dp_off);
            uint16_t ya = (state.y << 8) | state.a;
            uint16_t res = do_subw(ya, val);
            state.a = res & 0xFF;
            state.y = res >> 8;
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x68: { uint8_t val = fetch_byte(); state.psw.c = (state.a >= val); set_nz(static_cast<uint8_t>(state.a - val)); cycles -= 2 * SPC_CLOCK_DIVISOR; break; }
        case 0xC8: { uint8_t val = fetch_byte(); state.psw.c = (state.x >= val); set_nz(static_cast<uint8_t>(state.x - val)); cycles -= 2 * SPC_CLOCK_DIVISOR; break; }
        case 0xAD: { uint8_t val = fetch_byte(); state.psw.c = (state.y >= val); set_nz(static_cast<uint8_t>(state.y - val)); cycles -= 2 * SPC_CLOCK_DIVISOR; break; }
        case 0x64: { uint8_t val = read_byte(addr_dp(fetch_byte())); state.psw.c = (state.a >= val); set_nz(static_cast<uint8_t>(state.a - val)); cycles -= 3 * SPC_CLOCK_DIVISOR; break; }
        case 0x3E: { uint8_t val = read_byte(addr_dp(fetch_byte())); state.psw.c = (state.x >= val); set_nz(static_cast<uint8_t>(state.x - val)); cycles -= 3 * SPC_CLOCK_DIVISOR; break; }
        case 0x7E: { uint8_t val = read_byte(addr_dp(fetch_byte())); state.psw.c = (state.y >= val); set_nz(static_cast<uint8_t>(state.y - val)); cycles -= 3 * SPC_CLOCK_DIVISOR; break; }
        case 0x74: { uint8_t val = read_byte(addr_dp(fetch_byte() + state.x)); state.psw.c = (state.a >= val); set_nz(static_cast<uint8_t>(state.a - val)); cycles -= 4 * SPC_CLOCK_DIVISOR; break; }
        case 0x65: { uint8_t val = read_byte(fetch_word()); state.psw.c = (state.a >= val); set_nz(static_cast<uint8_t>(state.a - val)); cycles -= 4 * SPC_CLOCK_DIVISOR; break; }
        case 0x1E: { uint8_t val = read_byte(fetch_word()); state.psw.c = (state.x >= val); set_nz(static_cast<uint8_t>(state.x - val)); cycles -= 4 * SPC_CLOCK_DIVISOR; break; }
        case 0x5E: { uint8_t val = read_byte(fetch_word()); state.psw.c = (state.y >= val); set_nz(static_cast<uint8_t>(state.y - val)); cycles -= 4 * SPC_CLOCK_DIVISOR; break; }
        case 0x75: { uint8_t val = read_byte(fetch_word() + state.x); state.psw.c = (state.a >= val); set_nz(static_cast<uint8_t>(state.a - val)); cycles -= 5 * SPC_CLOCK_DIVISOR; break; }
        case 0x76: { uint8_t val = read_byte(fetch_word() + state.y); state.psw.c = (state.a >= val); set_nz(static_cast<uint8_t>(state.a - val)); cycles -= 5 * SPC_CLOCK_DIVISOR; break; }
        case 0x66: { uint8_t val = read_byte(addr_dp(state.x)); state.psw.c = (state.a >= val); set_nz(static_cast<uint8_t>(state.a - val)); cycles -= 3 * SPC_CLOCK_DIVISOR; break; }
        case 0x67: {
            uint8_t dp_off = fetch_byte() + state.x;
            uint8_t val = read_byte(read_dp_word(dp_off));
            state.psw.c = (state.a >= val);
            set_nz(static_cast<uint8_t>(state.a - val));
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x77: {
            uint8_t dp_off = fetch_byte();
            uint8_t val = read_byte(read_dp_word(dp_off) + state.y);
            state.psw.c = (state.a >= val);
            set_nz(static_cast<uint8_t>(state.a - val));
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x78: {
            uint8_t imm = fetch_byte();
            uint8_t mem = read_byte(addr_dp(fetch_byte()));
            state.psw.c = (mem >= imm);
            set_nz(static_cast<uint8_t>(mem - imm));
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x69: {
            uint8_t src = read_byte(addr_dp(fetch_byte()));
            uint8_t dest = read_byte(addr_dp(fetch_byte()));
            state.psw.c = (dest >= src);
            set_nz(static_cast<uint8_t>(dest - src));
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x79: {
            uint8_t src = read_byte(addr_dp(state.y));
            uint8_t dest = read_byte(addr_dp(state.x));
            state.psw.c = (dest >= src);
            set_nz(static_cast<uint8_t>(dest - src));
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x5A: {
            uint8_t dp_off = fetch_byte();
            uint16_t val = read_dp_word(dp_off);
            uint16_t ya = (state.y << 8) | state.a;
            state.psw.c = (ya >= val);
            set_nz16(ya - val);
            cycles -= 4 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0xCF: {
            uint16_t res = static_cast<uint16_t>(state.y) * static_cast<uint16_t>(state.a);
            state.a = res & 0xFF;
            state.y = res >> 8;
            set_nz(state.y);
            cycles -= 9 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x9E: {
            uint16_t ya = (state.y << 8) | state.a;
            state.psw.h = (state.y & 0x0F) >= (state.x & 0x0F);
            state.psw.v = (state.y >= state.x);

            if (state.x == 0) {
                // SNES hardware behavior on division by zero
                uint8_t old_y = state.y;
                state.y = state.a;
                state.a = ~old_y;
                state.psw.v = true;
            } else if (state.y < (state.x << 1)) {
                state.a = ya / state.x;
                state.y = ya % state.x;
            } else {
                state.a = 255 - ((ya - (state.x << 9)) / (256 - state.x));
                state.y = state.x + ((ya - (state.x << 9)) % (256 - state.x));
            }
            set_nz(state.a);
            cycles -= 12 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0xDF: {
            if (state.a > 0x99 || state.psw.c) {
                state.a += 0x60;
                state.psw.c = true;
            }
            if ((state.a & 0x0F) > 0x09 || state.psw.h) {
                state.a += 0x06;
            }
            set_nz(state.a);
            cycles -= 3 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xBE: {
            if (state.a > 0x99 || !state.psw.c) {
                state.a -= 0x60;
                state.psw.c = false;
            }
            if ((state.a & 0x0F) > 0x09 || !state.psw.h) {
                state.a -= 0x06;
            }
            set_nz(state.a);
            cycles -= 3 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x28: state.a &= fetch_byte(); set_nz(state.a); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x24: state.a &= read_byte(addr_dp(fetch_byte())); set_nz(state.a); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0x34: state.a &= read_byte(addr_dp(fetch_byte() + state.x)); set_nz(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x25: state.a &= read_byte(fetch_word()); set_nz(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x35: state.a &= read_byte(fetch_word() + state.x); set_nz(state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0x36: state.a &= read_byte(fetch_word() + state.y); set_nz(state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0x26: state.a &= read_byte(addr_dp(state.x)); set_nz(state.a); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0x27: {
            uint8_t dp_off = fetch_byte() + state.x;
            state.a &= read_byte(read_dp_word(dp_off));
            set_nz(state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x37: {
            uint8_t dp_off = fetch_byte();
            state.a &= read_byte(read_dp_word(dp_off) + state.y);
            set_nz(state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x38: {
            uint8_t imm = fetch_byte();
            uint16_t dest = addr_dp(fetch_byte());
            uint8_t res = read_byte(dest) & imm;
            write_byte(dest, res);
            set_nz(res);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x29: {
            uint16_t src = addr_dp(fetch_byte());
            uint16_t dest = addr_dp(fetch_byte());
            uint8_t res = read_byte(dest) & read_byte(src);
            write_byte(dest, res);
            set_nz(res);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x39: {
            uint16_t src = addr_dp(state.y);
            uint16_t dest = addr_dp(state.x);
            uint8_t res = read_byte(dest) & read_byte(src);
            write_byte(dest, res);
            set_nz(res);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x08: state.a |= fetch_byte(); set_nz(state.a); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x04: state.a |= read_byte(addr_dp(fetch_byte())); set_nz(state.a); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0x14: state.a |= read_byte(addr_dp(fetch_byte() + state.x)); set_nz(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x05: state.a |= read_byte(fetch_word()); set_nz(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x15: state.a |= read_byte(fetch_word() + state.x); set_nz(state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0x16: state.a |= read_byte(fetch_word() + state.y); set_nz(state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0x06: state.a |= read_byte(addr_dp(state.x)); set_nz(state.a); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0x07: {
            uint8_t dp_off = fetch_byte() + state.x;
            state.a |= read_byte(read_dp_word(dp_off));
            set_nz(state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x17: {
            uint8_t dp_off = fetch_byte();
            state.a |= read_byte(read_dp_word(dp_off) + state.y);
            set_nz(state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x18: {
            uint8_t imm = fetch_byte();
            uint16_t dest = addr_dp(fetch_byte());
            uint8_t res = read_byte(dest) | imm;
            write_byte(dest, res);
            set_nz(res);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x09: {
            uint16_t src = addr_dp(fetch_byte());
            uint16_t dest = addr_dp(fetch_byte());
            uint8_t res = read_byte(dest) | read_byte(src);
            write_byte(dest, res);
            set_nz(res);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x19: {
            uint16_t src = addr_dp(state.y);
            uint16_t dest = addr_dp(state.x);
            uint8_t res = read_byte(dest) | read_byte(src);
            write_byte(dest, res);
            set_nz(res);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x48: state.a ^= fetch_byte(); set_nz(state.a); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x44: state.a ^= read_byte(addr_dp(fetch_byte())); set_nz(state.a); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0x54: state.a ^= read_byte(addr_dp(fetch_byte() + state.x)); set_nz(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x45: state.a ^= read_byte(fetch_word()); set_nz(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x55: state.a ^= read_byte(fetch_word() + state.x); set_nz(state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0x56: state.a ^= read_byte(fetch_word() + state.y); set_nz(state.a); cycles -= 5 * SPC_CLOCK_DIVISOR; break;
        case 0x46: state.a ^= read_byte(addr_dp(state.x)); set_nz(state.a); cycles -= 3 * SPC_CLOCK_DIVISOR; break;
        case 0x47: {
            uint8_t dp_off = fetch_byte() + state.x;
            state.a ^= read_byte(read_dp_word(dp_off));
            set_nz(state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x57: {
            uint8_t dp_off = fetch_byte();
            state.a ^= read_byte(read_dp_word(dp_off) + state.y);
            set_nz(state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x58: {
            uint8_t imm = fetch_byte();
            uint16_t dest = addr_dp(fetch_byte());
            uint8_t res = read_byte(dest) ^ imm;
            write_byte(dest, res);
            set_nz(res);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x49: {
            uint16_t src = addr_dp(fetch_byte());
            uint16_t dest = addr_dp(fetch_byte());
            uint8_t res = read_byte(dest) ^ read_byte(src);
            write_byte(dest, res);
            set_nz(res);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x59: {
            uint16_t src = addr_dp(state.y);
            uint16_t dest = addr_dp(state.x);
            uint8_t res = read_byte(dest) ^ read_byte(src);
            write_byte(dest, res);
            set_nz(res);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0xBC: state.a++; set_nz(state.a); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x3D: state.x++; set_nz(state.x); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xFC: state.y++; set_nz(state.y); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xAB: { uint16_t d = addr_dp(fetch_byte()); uint8_t r = read_byte(d) + 1; write_byte(d, r); set_nz(r); cycles -= 4 * SPC_CLOCK_DIVISOR; break; }
        case 0xBB: { uint16_t d = addr_dp(fetch_byte() + state.x); uint8_t r = read_byte(d) + 1; write_byte(d, r); set_nz(r); cycles -= 5 * SPC_CLOCK_DIVISOR; break; }
        case 0xAC: { uint16_t d = fetch_word(); uint8_t r = read_byte(d) + 1; write_byte(d, r); set_nz(r); cycles -= 5 * SPC_CLOCK_DIVISOR; break; }
        case 0x3A: {
            uint8_t dp_off = fetch_byte();
            uint16_t r = read_dp_word(dp_off) + 1;
            write_dp_word(dp_off, r);
            set_nz16(r);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x9C: state.a--; set_nz(state.a); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x1D: state.x--; set_nz(state.x); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0xDC: state.y--; set_nz(state.y); cycles -= 2 * SPC_CLOCK_DIVISOR; break;
        case 0x8B: { uint16_t d = addr_dp(fetch_byte()); uint8_t r = read_byte(d) - 1; write_byte(d, r); set_nz(r); cycles -= 4 * SPC_CLOCK_DIVISOR; break; }
        case 0x9B: { uint16_t d = addr_dp(fetch_byte() + state.x); uint8_t r = read_byte(d) - 1; write_byte(d, r); set_nz(r); cycles -= 5 * SPC_CLOCK_DIVISOR; break; }
        case 0x8C: { uint16_t d = fetch_word(); uint8_t r = read_byte(d) - 1; write_byte(d, r); set_nz(r); cycles -= 5 * SPC_CLOCK_DIVISOR; break; }
        case 0x1A: {
            uint8_t dp_off = fetch_byte();
            uint16_t r = read_dp_word(dp_off) - 1;
            write_dp_word(dp_off, r);
            set_nz16(r);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x1C: {
            state.psw.c = (state.a & 0x80) != 0;
            state.a <<= 1;
            set_nz(state.a);
            cycles -= 2 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x0B: {
            uint16_t d = addr_dp(fetch_byte());
            uint8_t val = read_byte(d);
            state.psw.c = (val & 0x80) != 0;
            val <<= 1;
            write_byte(d, val);
            set_nz(val);
            cycles -= 4 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x1B: {
            uint16_t d = addr_dp(fetch_byte() + state.x);
            uint8_t val = read_byte(d);
            state.psw.c = (val & 0x80) != 0;
            val <<= 1;
            write_byte(d, val);
            set_nz(val);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x0C: {
            uint16_t d = fetch_word();
            uint8_t val = read_byte(d);
            state.psw.c = (val & 0x80) != 0;
            val <<= 1;
            write_byte(d, val);
            set_nz(val);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x5C: {
            state.psw.c = (state.a & 0x01) != 0;
            state.a >>= 1;
            set_nz(state.a);
            cycles -= 2 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x4B: {
            uint16_t d = addr_dp(fetch_byte());
            uint8_t val = read_byte(d);
            state.psw.c = (val & 0x01) != 0;
            val >>= 1;
            write_byte(d, val);
            set_nz(val);
            cycles -= 4 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x5B: {
            uint16_t d = addr_dp(fetch_byte() + state.x);
            uint8_t val = read_byte(d);
            state.psw.c = (val & 0x01) != 0;
            val >>= 1;
            write_byte(d, val);
            set_nz(val);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x4C: {
            uint16_t d = fetch_word();
            uint8_t val = read_byte(d);
            state.psw.c = (val & 0x01) != 0;
            val >>= 1;
            write_byte(d, val);
            set_nz(val);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x3C: {
            bool old_c = state.psw.c;
            state.psw.c = (state.a & 0x80) != 0;
            state.a = (state.a << 1) | (old_c ? 1 : 0);
            set_nz(state.a);
            cycles -= 2 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x2B: {
            uint16_t d = addr_dp(fetch_byte());
            uint8_t val = read_byte(d);
            bool old_c = state.psw.c;
            state.psw.c = (val & 0x80) != 0;
            val = (val << 1) | (old_c ? 1 : 0);
            write_byte(d, val);
            set_nz(val);
            cycles -= 4 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x3B: {
            uint16_t d = addr_dp(fetch_byte() + state.x);
            uint8_t val = read_byte(d);
            bool old_c = state.psw.c;
            state.psw.c = (val & 0x80) != 0;
            val = (val << 1) | (old_c ? 1 : 0);
            write_byte(d, val);
            set_nz(val);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x2C: {
            uint16_t d = fetch_word();
            uint8_t val = read_byte(d);
            bool old_c = state.psw.c;
            state.psw.c = (val & 0x80) != 0;
            val = (val << 1) | (old_c ? 1 : 0);
            write_byte(d, val);
            set_nz(val);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x7C: {
            bool old_c = state.psw.c;
            state.psw.c = (state.a & 0x01) != 0;
            state.a = (state.a >> 1) | (old_c ? 0x80 : 0);
            set_nz(state.a);
            cycles -= 2 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x6B: {
            uint16_t d = addr_dp(fetch_byte());
            uint8_t val = read_byte(d);
            bool old_c = state.psw.c;
            state.psw.c = (val & 0x01) != 0;
            val = (val >> 1) | (old_c ? 0x80 : 0);
            write_byte(d, val);
            set_nz(val);
            cycles -= 4 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x7B: {
            uint16_t d = addr_dp(fetch_byte() + state.x);
            uint8_t val = read_byte(d);
            bool old_c = state.psw.c;
            state.psw.c = (val & 0x01) != 0;
            val = (val >> 1) | (old_c ? 0x80 : 0);
            write_byte(d, val);
            set_nz(val);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x6C: {
            uint16_t d = fetch_word();
            uint8_t val = read_byte(d);
            bool old_c = state.psw.c;
            state.psw.c = (val & 0x01) != 0;
            val = (val >> 1) | (old_c ? 0x80 : 0);
            write_byte(d, val);
            set_nz(val);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x9F: {
            state.a = (state.a >> 4) | (state.a << 4);
            set_nz(state.a);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x2F: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            state.pc += offset;
            cycles -= 4 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xF0: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (state.psw.z) { state.pc += offset; cycles -= 4 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 2 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0xD0: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (!state.psw.z) { state.pc += offset; cycles -= 4 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 2 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0xB0: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (state.psw.c) { state.pc += offset; cycles -= 4 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 2 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0x90: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (!state.psw.c) { state.pc += offset; cycles -= 4 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 2 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0x70: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (state.psw.v) { state.pc += offset; cycles -= 4 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 2 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0x50: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (!state.psw.v) { state.pc += offset; cycles -= 4 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 2 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0x30: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (state.psw.n) { state.pc += offset; cycles -= 4 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 2 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0x10: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (!state.psw.n) { state.pc += offset; cycles -= 4 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 2 * SPC_CLOCK_DIVISOR; }
            break;
        }

        case 0x2E: {
            uint16_t d = addr_dp(fetch_byte());
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (state.a != read_byte(d)) { state.pc += offset; cycles -= 7 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 5 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0xDE: {
            uint16_t d = addr_dp(fetch_byte() + state.x);
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (state.a != read_byte(d)) { state.pc += offset; cycles -= 8 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 6 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0x6E: {
            uint16_t d = addr_dp(fetch_byte());
            int8_t offset = static_cast<int8_t>(fetch_byte());
            uint8_t res = read_byte(d) - 1;
            write_byte(d, res);
            if (res != 0) { state.pc += offset; cycles -= 7 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 5 * SPC_CLOCK_DIVISOR; }
            break;
        }
        case 0xFE: {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            state.y--;
            if (state.y != 0) { state.pc += offset; cycles -= 6 * SPC_CLOCK_DIVISOR; }
            else { cycles -= 4 * SPC_CLOCK_DIVISOR; }
            break;
        }

        case 0x3F: {
            uint16_t target = fetch_word();
            push_word(state.pc);
            state.pc = target;
            cycles -= 8 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x4F: {
            uint8_t offset = fetch_byte();
            push_word(state.pc);
            state.pc = 0xFF00 | offset;
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x6F: {
            state.pc = pop_word();
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x7F: {
            state.psw.from_byte(pop_byte());
            state.pc = pop_word();
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x0F: {
            push_word(state.pc);
            push_byte(state.psw.to_byte());
            state.psw.b = true;
            state.psw.i = false;
            state.pc = read_word(0xFFDE);
            cycles -= 8 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x5F: {
            state.pc = fetch_word();
            cycles -= 3 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x1F: {
            uint16_t ptr = fetch_word() + state.x;
            state.pc = read_word(ptr);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0x2D: push_byte(state.a); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x4D: push_byte(state.x); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x6D: push_byte(state.y); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x0D: push_byte(state.psw.to_byte()); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xAE: state.a = pop_byte(); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xCE: state.x = pop_byte(); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0xEE: state.y = pop_byte(); cycles -= 4 * SPC_CLOCK_DIVISOR; break;
        case 0x8E: state.psw.from_byte(pop_byte()); cycles -= 4 * SPC_CLOCK_DIVISOR; break;

        #define BIT_OPCODES(bit) \
        case (0x02 | ((bit) << 5)): { \
            uint16_t d = addr_dp(fetch_byte()); \
            write_byte(d, read_byte(d) | (1 << (bit))); \
            cycles -= 4 * SPC_CLOCK_DIVISOR; \
            break; \
        } \
        case (0x12 | ((bit) << 5)): { \
            uint16_t d = addr_dp(fetch_byte()); \
            write_byte(d, read_byte(d) & ~(1 << (bit))); \
            cycles -= 4 * SPC_CLOCK_DIVISOR; \
            break; \
        } \
        case (0x03 | ((bit) << 5)): { \
            uint16_t d = addr_dp(fetch_byte()); \
            int8_t offset = static_cast<int8_t>(fetch_byte()); \
            if (read_byte(d) & (1 << (bit))) { state.pc += offset; cycles -= 7 * SPC_CLOCK_DIVISOR; } \
            else { cycles -= 5 * SPC_CLOCK_DIVISOR; } \
            break; \
        } \
        case (0x13 | ((bit) << 5)): { \
            uint16_t d = addr_dp(fetch_byte()); \
            int8_t offset = static_cast<int8_t>(fetch_byte()); \
            if (!(read_byte(d) & (1 << (bit)))) { state.pc += offset; cycles -= 7 * SPC_CLOCK_DIVISOR; } \
            else { cycles -= 5 * SPC_CLOCK_DIVISOR; } \
            break; \
        }

        BIT_OPCODES(0) BIT_OPCODES(1) BIT_OPCODES(2) BIT_OPCODES(3)
        BIT_OPCODES(4) BIT_OPCODES(5) BIT_OPCODES(6) BIT_OPCODES(7)
        #undef BIT_OPCODES

        case 0x0E: {
            uint16_t addr = fetch_word();
            uint8_t val = read_byte(addr);
            set_nz(static_cast<uint8_t>(state.a - val));
            write_byte(addr, val | state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x4E: {
            uint16_t addr = fetch_word();
            uint8_t val = read_byte(addr);
            set_nz(static_cast<uint8_t>(state.a - val));
            write_byte(addr, val & ~state.a);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }

        case 0xAA: {
            uint16_t raw = fetch_word();
            uint16_t addr = raw & 0x1FFF;
            uint8_t bit = raw >> 13;
            state.psw.c = (read_byte(addr) & (1 << bit)) != 0;
            cycles -= 4 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xCA: {
            uint16_t raw = fetch_word();
            uint16_t addr = raw & 0x1FFF;
            uint8_t bit = raw >> 13;
            uint8_t val = read_byte(addr);
            if (state.psw.c) val |= (1 << bit);
            else val &= ~(1 << bit);
            write_byte(addr, val);
            cycles -= 6 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x4A: {
            uint16_t raw = fetch_word();
            uint16_t addr = raw & 0x1FFF;
            uint8_t bit = raw >> 13;
            state.psw.c = state.psw.c && ((read_byte(addr) & (1 << bit)) != 0);
            cycles -= 4 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x6A: {
            uint16_t raw = fetch_word();
            uint16_t addr = raw & 0x1FFF;
            uint8_t bit = raw >> 13;
            state.psw.c = state.psw.c && ((read_byte(addr) & (1 << bit)) == 0);
            cycles -= 4 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x0A: {
            uint16_t raw = fetch_word();
            uint16_t addr = raw & 0x1FFF;
            uint8_t bit = raw >> 13;
            state.psw.c = state.psw.c || ((read_byte(addr) & (1 << bit)) != 0);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x2A: {
            uint16_t raw = fetch_word();
            uint16_t addr = raw & 0x1FFF;
            uint8_t bit = raw >> 13;
            state.psw.c = state.psw.c || ((read_byte(addr) & (1 << bit)) == 0);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0x8A: {
            uint16_t raw = fetch_word();
            uint16_t addr = raw & 0x1FFF;
            uint8_t bit = raw >> 13;
            state.psw.c = state.psw.c ^ ((read_byte(addr) & (1 << bit)) != 0);
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }
        case 0xEA: {
            uint16_t raw = fetch_word();
            uint16_t addr = raw & 0x1FFF;
            uint8_t bit = raw >> 13;
            write_byte(addr, read_byte(addr) ^ (1 << bit));
            cycles -= 5 * SPC_CLOCK_DIVISOR;
            break;
        }

        #define TCALL_OPCODE(idx) \
        case (0x01 | ((idx) << 4)): { \
            push_word(state.pc); \
            state.pc = read_word(0xFFDE - ((idx) * 2)); \
            cycles -= 8 * SPC_CLOCK_DIVISOR; \
            break; \
        }

        TCALL_OPCODE(0)  TCALL_OPCODE(1)  TCALL_OPCODE(2)  TCALL_OPCODE(3)
        TCALL_OPCODE(4)  TCALL_OPCODE(5)  TCALL_OPCODE(6)  TCALL_OPCODE(7)
        TCALL_OPCODE(8)  TCALL_OPCODE(9)  TCALL_OPCODE(10) TCALL_OPCODE(11)
        TCALL_OPCODE(12) TCALL_OPCODE(13) TCALL_OPCODE(14) TCALL_OPCODE(15)
        #undef TCALL_OPCODE

        default:
            cycles -= 2 * SPC_CLOCK_DIVISOR;
            break;
    }
}

void Spc700::fix_after_load() {
    // 1. Restore timer reload targets from RAM ($00FA, $00FB, $00FC)
    state.timer[0].target = ram[0x00FA];
    state.timer[1].target = ram[0x00FB];
    state.timer[2].target = ram[0x00FC];

    // 2. Initialize timer steps
    for (int i = 0; i < 3; ++i) {
        state.timer[i].step = (state.timer[i].target == 0) ? 255 : (state.timer[i].target - 1);
        state.timer[i].counter = 0;
    }

    // 3. Restore Control Register ($00F1) and enable active timers
    control_reg = ram[0x00F1];
    state.timer[0].enabled = (control_reg & 0x01) != 0;
    state.timer[1].enabled = (control_reg & 0x02) != 0;
    state.timer[2].enabled = (control_reg & 0x04) != 0;

    // 4. Restore communication ports ($00F4-$00F7)
    for (int i = 0; i < 4; ++i) {
        in_ports[i] = ram[0x00F4 + i];
        out_ports[i] = ram[0x00F4 + i];
    }

    state.stopped = false;
    state.sleeping = false;
}