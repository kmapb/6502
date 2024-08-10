#include "6502.hpp"
#include <cstring>


uint8_t&
Memory::operator[](uint16_t addr) {
    return bytes[addr];
}

const uint8_t&
Memory::operator[](uint16_t addr) const {
    return bytes[addr];
}

void
Memory::reset() {
    memset(bytes, 0, sizeof(bytes));
}

uint16_t
Memory::read16(uint16_t addr) const {
    auto ll = bytes[addr];
    auto hh = bytes[(addr + 1) & 0xffff];
    return (hh << 8) | ll;
}

void
Memory::write16(uint16_t addr, uint16_t val) {
    auto ll = val & 0xff;
    auto hh = (val & 0xff00) >> 8;
    bytes[addr] = ll;
    bytes[(addr + 1) & 0xffff] = hh;
}

uint8_t
addressing_mode_to_length(AddressingMode mode) {
    switch (mode) {
#define ADDRESSING_MODE(mode, len) case (mode): return len;
        ADDRESSING_MODES()
#undef ADDRESSING_MODE
    }
    NOT_REACHED();
    return 0;
}

static inline uint16_t
operand(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    switch(mode) {
    case ACCUMULATOR: return regs.A;
    case ABS: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return ll + (hh << 8);
    }
    case ABS_X: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return regs.X + ll + (hh << 8);
    }
    case ABS_Y: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return regs.Y + ll + (hh << 8);
    }
    case IMMEDIATE: {
        return mem.bytes[regs.PC + 1];
    }
    case IMPLIED: {
        return 0; // Operand not used for this opcode
    }
    case INDIRECT: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return mem.bytes[ll + (hh << 8)];
    }
    case X_IND: {
        auto addr_low = mem.bytes[regs.PC + 1];
        auto ll = mem.bytes[(addr_low + regs.X) & 0xff];
        auto hh = mem.bytes[(addr_low + regs.X + 1) & 0xff];
        return ll + (hh << 8);
    }
    case IND_Y: {
        auto addr_low = mem.bytes[regs.PC + 1];
        return mem.bytes[addr_low] + mem.bytes[addr_low + 1] + regs.Y;
    }
    case REL: {
        int8_t disp = int8_t(mem.bytes[regs.PC + 1]);
        return regs.PC + disp;
    }
    case ZPG: {
        uint8_t addr = mem.bytes[regs.PC + 1];
        return mem.bytes[addr];
    }
    case ZPG_X: {
        uint8_t addr = mem.bytes[regs.PC + 1] + regs.X;
        return mem.bytes[addr];
    }
    case ZPG_Y: {
        uint8_t addr = mem.bytes[regs.PC + 1] + regs.Y;
        return mem.bytes[addr];
    }
    default: return 0;
    }
}

uint8_t pop8(RegisterFile& regs, const Memory& mem) {
    return mem[++regs.SP];
}

uint16_t pop16(RegisterFile& regs, const Memory& mem) {
    uint8_t ll = pop8(regs, mem);
    uint8_t hh = pop8(regs, mem);
    return ll | (hh << 8);
}

void push8(RegisterFile& regs, Memory& mem, uint8_t val) {
    mem[regs.SP--] = val;
}

void push16(RegisterFile& regs, Memory& mem, uint16_t val) {
    push8(regs, mem, val & 0xff);
    push8(regs, mem, (val & 0xff00) >> 8);
}

void push_status(RegisterFile& regs, Memory& mem, bool breakp=false) {
    push8(regs, mem, regs.read_flags(breakp));
}

void
set_flags(RegisterFile& regs, uint8_t oldA, uint8_t newA, uint8_t mask) {
    if (mask & flags::N) regs.flags.N = (newA >> 7);
    if (mask & flags::Z) regs.flags.Z = (newA == 0);
    if (mask && flags::C) regs.flags.C = (oldA >> 7);
}

// Opcode implementations
uint16_t
op_BRK(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    push16(regs, mem, regs.PC + 2); // Excess byte after BRK 
    push_status(regs, mem, true);
    return mem.read16(0xfffe);
};

uint16_t
op_ORA(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.A |= operand(regs, mem, mode);
    return regs.PC + addressing_mode_to_length(mode);
};

uint16_t
op_ASL(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.A = uint8_t(regs.A << operand(regs, mem, mode));
    return regs.PC + addressing_mode_to_length(mode);
}

static int instrs_to_flags[] = {
#define MNEMONIC(mnem, flags) \
    flags,
MNEMONICS()
#undef MNEMONIC
};

void
run_instr(RegisterFile& regs, Memory& mem) {
    auto opcode = mem[regs.PC];
    switch(opcode) {
#define OPCODE(mnem, opcode, mode) \
    case opcode: {\
    auto oldA = regs.A; \
    regs.PC = op_ ## mnem(regs, mem, mode); \
    auto newA = regs.A; \
    set_flags(regs, oldA, newA, instrs_to_flags[mnem]); \
    } \
    return;

OPCODES()
#undef OPCODE
    }
}

