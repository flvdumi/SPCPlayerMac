#include "Script700.hpp"
#include "Spc700.hpp"
#include "Dsp.hpp"
#include <sstream>

Script700::Script700() {
    reset();
}

void Script700::reset() {
    state = Script700State{};
    instructions.clear();
    embedded_data.clear();
}

uint32_t Script700::get_operand_val(const ScriptOperand& op, Spc700& spc) {
    switch (op.type) {
        case OperandType::Immediate: return op.value;
        case OperandType::InPort:    return spc.read_port(static_cast<uint8_t>(op.value & 3));
        case OperandType::OutPort:   return spc.get_out_port(static_cast<uint8_t>(op.value & 3));
        case OperandType::WorkVar:   return state.work_vars[op.value & 7];
        case OperandType::ExtraRam:  return spc.read_byte(0xFFC0 + (op.value & 63));
        case OperandType::RamByte:   return spc.read_byte(static_cast<uint16_t>(op.value));
        case OperandType::RamWord:   return spc.read_word(static_cast<uint16_t>(op.value));
        case OperandType::RamDword: {
            uint32_t lo = spc.read_word(static_cast<uint16_t>(op.value));
            uint32_t hi = spc.read_word(static_cast<uint16_t>(op.value + 2));
            return lo | (hi << 16);
        }
        case OperandType::DataByte:
            if (op.value < embedded_data.size()) return embedded_data[op.value];
            return 0;
        case OperandType::Label:     return state.labels[op.value & 1023];
        default: return 0;
    }
}

void Script700::set_operand_val(const ScriptOperand& op, uint32_t val, Spc700& spc) {
    switch (op.type) {
        case OperandType::InPort:
            spc.write_port(static_cast<uint8_t>(op.value & 3), static_cast<uint8_t>(val));
            break;
        case OperandType::OutPort:
            spc.write_byte(0x00F4 + (op.value & 3), static_cast<uint8_t>(val));
            break;
        case OperandType::WorkVar:
            state.work_vars[op.value & 7] = val;
            break;
        case OperandType::RamByte:
            spc.write_byte(static_cast<uint16_t>(op.value), static_cast<uint8_t>(val));
            break;
        case OperandType::RamWord:
            spc.write_word(static_cast<uint16_t>(op.value), static_cast<uint16_t>(val));
            break;
        case OperandType::Label:
            state.labels[op.value & 1023] = val;
            break;
        default: break;
    }
}

uint32_t Script700::calculate(char op, uint32_t a, uint32_t b) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return b != 0 ? a / b : 0;
        case '%': return b != 0 ? a % b : 0;
        case '&': return a & b;
        case '|': return a | b;
        case '^': return a ^ b;
        case '<': return a << (b & 31);
        case '>': return a >> (b & 31);
        case '_': return static_cast<int32_t>(a) >> (b & 31);
        case '!': return ~a;
        default:  return b;
    }
}

void Script700::step(uint32_t elapsed_ticks, Spc700& spc, Dsp& dsp) {
    (void)dsp;
    if (!state.enabled || instructions.empty()) return;

    if (state.wait_counter > elapsed_ticks) {
        state.wait_counter -= elapsed_ticks;
        return;
    }
    state.wait_counter = 0;

    while (state.prog_ptr < instructions.size()) {
        const auto& inst = instructions[state.prog_ptr++];

        switch (inst.cmd) {
            case ScriptCommand::Wait:
                state.wait_counter = get_operand_val(inst.op1, spc);
                return;

            case ScriptCommand::WaitIn:
                state.interrupt_ports[0] = static_cast<uint8_t>(0x80 | (get_operand_val(inst.op1, spc) & 3));
                return;

            case ScriptCommand::WaitOut:
                state.interrupt_ports[1] = static_cast<uint8_t>(0x80 | (get_operand_val(inst.op1, spc) & 3));
                return;

            case ScriptCommand::Move: {
                uint32_t val = get_operand_val(inst.op1, spc);
                set_operand_val(inst.op2, val, spc);
                break;
            }

            case ScriptCommand::Compare:
                state.cmp_params[0] = get_operand_val(inst.op1, spc);
                state.cmp_params[1] = get_operand_val(inst.op2, spc);
                break;

            case ScriptCommand::Calc: {
                uint32_t a = get_operand_val(inst.op1, spc);
                uint32_t b = get_operand_val(inst.op2, spc);
                uint32_t res = calculate(inst.calc_op, a, b);
                set_operand_val(inst.op2, res, spc);
                break;
            }

            case ScriptCommand::Branch:
                if ((state.status_flags & 0x01) && state.stack_ptr < state.stack.size()) {
                    state.stack[state.stack_ptr++] = state.prog_ptr;
                }
                state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Beq:
                if (state.cmp_params[0] == state.cmp_params[1])
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Bne:
                if (state.cmp_params[0] != state.cmp_params[1])
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Bge:
                if (static_cast<int32_t>(state.cmp_params[0]) >= static_cast<int32_t>(state.cmp_params[1]))
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Ble:
                if (static_cast<int32_t>(state.cmp_params[0]) <= static_cast<int32_t>(state.cmp_params[1]))
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Bgt:
                if (static_cast<int32_t>(state.cmp_params[0]) > static_cast<int32_t>(state.cmp_params[1]))
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Blt:
                if (static_cast<int32_t>(state.cmp_params[0]) < static_cast<int32_t>(state.cmp_params[1]))
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Bcc:
                if (state.cmp_params[0] >= state.cmp_params[1])
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Blo:
                if (state.cmp_params[0] <= state.cmp_params[1])
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Bhi:
                if (state.cmp_params[0] > state.cmp_params[1])
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Bcs:
                if (state.cmp_params[0] < state.cmp_params[1])
                    state.prog_ptr = get_operand_val(inst.op1, spc);
                break;

            case ScriptCommand::Ret:
                if (state.stack_ptr > 0) {
                    state.prog_ptr = state.stack[--state.stack_ptr];
                }
                break;

            case ScriptCommand::Flush:
                for (uint8_t p = 0; p < 4; ++p) {
                    spc.write_port(p, spc.read_port(p));
                }
                break;

            case ScriptCommand::Quit:
                state.enabled = false;
                return;

            default: break;
        }
    }
}

void Script700::on_port_write(uint8_t port, uint8_t val) {
    (void)val;
    if (state.interrupt_ports[1] == (0x80 | (port & 3))) {
        state.interrupt_ports[1] = 0;
        state.wait_counter = 0;
    }
}

ScriptOperand Script700::parse_operand(std::string_view token) {
    ScriptOperand op;
    if (token.empty()) return op;

    if (token[0] == '#') {
        op.type = OperandType::Immediate;
        std::string_view num_str = token.substr(1);
        if (num_str.starts_with("$")) {
            op.value = static_cast<uint32_t>(std::stoul(std::string(num_str.substr(1)), nullptr, 16));
        } else if (num_str.starts_with("0x") || num_str.starts_with("0X")) {
            op.value = static_cast<uint32_t>(std::stoul(std::string(num_str.substr(2)), nullptr, 16));
        } else {
            op.value = static_cast<uint32_t>(std::stoul(std::string(num_str), nullptr, 10));
        }
    } else if (token[0] == 'i' && token.size() > 1) {
        op.type = OperandType::InPort;
        op.value = token[1] - '0';
    } else if (token[0] == 'o' && token.size() > 1) {
        op.type = OperandType::OutPort;
        op.value = token[1] - '0';
    } else if (token[0] == 'w' && token.size() > 1) {
        op.type = OperandType::WorkVar;
        op.value = token[1] - '0';
    } else if (token.starts_with("rb[") || token.starts_with("r[")) {
        op.type = OperandType::RamByte;
        auto open = token.find('[');
        auto close = token.find(']');
        auto inner = token.substr(open + 1, close - open - 1);
        op.value = static_cast<uint32_t>(std::stoul(std::string(inner), nullptr, 16));
    }
    return op;
}

bool Script700::load_script(std::string_view text) {
    reset();
    std::string line;
    std::stringstream ss{std::string(text)};

    while (std::getline(ss, line)) {
        size_t comment = line.find(';');
        if (comment != std::string::npos) line.erase(comment);

        std::stringstream line_stream(line);
        std::string cmd;
        if (!(line_stream >> cmd)) continue;

        ScriptInstruction inst;
        if (cmd == "w") {
            inst.cmd = ScriptCommand::Wait;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "wi") {
            inst.cmd = ScriptCommand::WaitIn;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "wo") {
            inst.cmd = ScriptCommand::WaitOut;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "m") {
            inst.cmd = ScriptCommand::Move;
            std::string op1, op2; line_stream >> op1 >> op2;
            inst.op1 = parse_operand(op1);
            inst.op2 = parse_operand(op2);
        } else if (cmd == "c") {
            inst.cmd = ScriptCommand::Compare;
            std::string op1, op2; line_stream >> op1 >> op2;
            inst.op1 = parse_operand(op1);
            inst.op2 = parse_operand(op2);
        } else if (cmd == "bra") {
            inst.cmd = ScriptCommand::Branch;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "beq") {
            inst.cmd = ScriptCommand::Beq;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "bne") {
            inst.cmd = ScriptCommand::Bne;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "bge") {
            inst.cmd = ScriptCommand::Bge;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "ble") {
            inst.cmd = ScriptCommand::Ble;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "bgt") {
            inst.cmd = ScriptCommand::Bgt;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "blt") {
            inst.cmd = ScriptCommand::Blt;
            std::string op1; line_stream >> op1;
            inst.op1 = parse_operand(op1);
        } else if (cmd == "r") {
            inst.cmd = ScriptCommand::Ret;
        } else if (cmd == "f") {
            inst.cmd = ScriptCommand::Flush;
        } else if (cmd == "q") {
            inst.cmd = ScriptCommand::Quit;
        }

        instructions.push_back(inst);
    }

    state.enabled = !instructions.empty();
    return state.enabled;
}