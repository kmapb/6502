#pragma once
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#define NOT_IMPLEMENTED() abort()
#define NOT_REACHED() abort()


struct Memory {
    uint8_t bytes[1 << 16];
    uint8_t& operator[](uint16_t addr) {
        return bytes[addr];
    }
    const uint8_t& operator[](uint16_t addr) const {
        return bytes[addr];
    }

    void reset() {
        memset(bytes, 0, sizeof(bytes));
    }
};

struct RegisterFile {
    uint16_t PC;
    uint8_t A;
    uint8_t X;
    uint8_t Y;
    uint8_t SP;

    // Architected flags;
    struct {
        unsigned int C : 1;
        unsigned int Z : 1;
        unsigned int I : 1;
        unsigned int D : 1;
        unsigned int V : 1;
        unsigned int N : 1;

        // B flag isn't memory: just reflects cause of the flag spill
    } flags;

    uint8_t read_flags(bool breakp=false) const {
        return flags.C |
          (flags.Z << 1) |
          (flags.I << 2) |
          (flags.D << 3) |
          (breakp << 4)  |
          (1      << 5)  | // Always pushed as 1
          (flags.V << 6) |
          (flags.N << 7);
    }

    uint16_t stack_address() const {
        return 0x100 | SP;
    }

    void reset() {
        PC = A = X = Y = SP = 0;

        flags.C = 0;
        flags.Z = 0;
        flags.I = 0;
        flags.D = 0;
        flags.V = 0;
        flags.N = 0;
    }
};

// Name, bytes of instruction stream consumned
#define ADDRESSING_MODES() \
    ADDRESSING_MODE(ACCUMULATOR, 1) \
    ADDRESSING_MODE(ABS, 3) \
    ADDRESSING_MODE(ABS_X, 3) \
    ADDRESSING_MODE(ABS_Y, 3) \
    ADDRESSING_MODE(IMMEDIATE, 2) \
    ADDRESSING_MODE(IMPLIED, 1) \
    ADDRESSING_MODE(INDIRECT,3) \
    ADDRESSING_MODE(X_IND, 2) \
    ADDRESSING_MODE(IND_Y, 2) \
    ADDRESSING_MODE(REL, 2) \
    ADDRESSING_MODE(ZPG, 2) \
    ADDRESSING_MODE(ZPG_X, 2) \
    ADDRESSING_MODE(ZPG_Y, 2)

enum AddressingMode {
#define ADDRESSING_MODE(mode, len) \
    mode,
    ADDRESSING_MODES()
#undef ADDRESSING_MODE
};

static inline uint8_t
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

// See https://www.masswerk.at/6502/6502_instruction_set.html
#define OPCODES() \
 OPCODE(BRK, 0x00, IMPLIED)       \
 OPCODE(ORA, 0x01, X_IND)         \
 OPCODE(ORA, 0x05, ZPG)           \
 OPCODE(ORA, 0x09, IMMEDIATE)     \
 OPCODE(ORA, 0x0d, ABS)           \
 OPCODE(ORA, 0x11, IND_Y)         \
 OPCODE(ORA, 0x15, ZPG_X)         \
 OPCODE(ORA, 0x19, ABS_Y)         \
 OPCODE(ORA, 0x1d, ABS_X)         \

uint8_t pop8(RegisterFile& regs, const Memory& mem) {
    return mem[regs.SP++];
}

uint16_t pop16(RegisterFile& regs, const Memory& mem) {
    uint8_t ll = pop8(regs, mem);
    uint8_t hh = pop8(regs, mem);
    return ll | (hh << 8);
}
void push8(RegisterFile& regs, Memory& mem, uint8_t val) {
    mem[--regs.SP] = val;
}

void push16(RegisterFile& regs, Memory& mem, uint16_t val) {
    push8(regs, mem, val & 0xff);
    push8(regs, mem, (val & 0xff00) >> 8);
}

void push_status(RegisterFile& regs, Memory& mem, bool breakp=false) {
    push8(regs, mem, regs.read_flags(breakp));
}

// Opcode implementations
uint16_t
op_BRK(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    push16(regs, mem, regs.PC + 2); // Excess byte after BRK 
    push_status(regs, mem, true);
    NOT_IMPLEMENTED();
    return 0;
};

uint16_t
op_ORA(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    auto newA = regs.A | operand(regs, mem, mode);
    regs.A = newA;
    regs.flags.Z = newA >> 7;
    // Return PC?
    return regs.PC + addressing_mode_to_length(mode);
};

void
run_instr(RegisterFile& regs, Memory& mem) {
    auto opcode = mem[regs.PC];
    switch(opcode) {
#define OPCODE(name, opcode, mode) \
    case opcode: \
    regs.PC = op_ ## name(regs, mem, mode); \
    return;

OPCODES()
#undef OPCODE
    }
}

enum opcode_bytes {
#define OPCODE(name, opcode, mode) \
    OPCODE_ ## name ## _ ## mode = opcode,
    OPCODES()
#undef OPCODE
};

