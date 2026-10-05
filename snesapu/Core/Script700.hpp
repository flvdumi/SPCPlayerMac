#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <string_view>
#include "Script700Types.hpp"

class Spc700;
class Dsp;

enum class OperandType : uint8_t {
    Immediate,
    InPort,
    OutPort,
    WorkVar,
    ExtraRam,
    RamByte,
    RamWord,
    RamDword,
    DataByte,
    DataWord,
    DataDword,
    Label
};

struct ScriptOperand {
    OperandType type = OperandType::Immediate;
    uint32_t value = 0;
};

enum class ScriptCommand : uint8_t {
    Wait,
    WaitIn,
    WaitOut,
    Move,
    Compare,
    Calc,
    Branch,
    Beq, Bne, Bge, Ble, Bgt, Blt, Bcc, Blo, Bhi, Bcs,
    Ret,
    Flush,
    Breakpoint,
    Quit
};

struct ScriptInstruction {
    ScriptCommand cmd = ScriptCommand::Quit;
    char calc_op = 0;
    ScriptOperand op1;
    ScriptOperand op2;
};

class Script700 {
public:
    Script700();

    void reset();
    bool load_script(std::string_view script_text);
    void step(uint32_t elapsed_ticks, Spc700& spc, Dsp& dsp);
    void on_port_write(uint8_t port, uint8_t val);

    bool is_active() const { return state.enabled && !instructions.empty(); }

private:
    Script700State state;
    std::vector<ScriptInstruction> instructions;
    std::vector<uint8_t> embedded_data;

    uint32_t get_operand_val(const ScriptOperand& op, Spc700& spc);
    void set_operand_val(const ScriptOperand& op, uint32_t val, Spc700& spc);
    uint32_t calculate(char op, uint32_t a, uint32_t b);

    ScriptOperand parse_operand(std::string_view token);
};