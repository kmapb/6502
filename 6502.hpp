#pragma once

#include <cstdio>
#include <cstdint>
#include <cstdlib>

#define NOT_IMPLEMENTED() abort()
#define NOT_REACHED() abort()


struct Memory {
    uint8_t bytes[1 << 16];
   
    uint8_t& operator[](uint16_t addr);
    const uint8_t& operator[](uint16_t addr) const;
    void reset();
    uint16_t read16(uint16_t addr) const;
    void write16(uint16_t addr, uint16_t val);
};

struct Flags {
    unsigned int C : 1;
    unsigned int Z : 1;
    unsigned int I : 1;
    unsigned int D : 1;
    unsigned int V : 1;
    unsigned int N : 1;

    // B flag isn't memory: just reflects cause of the flag spill
};

// For sets of allowed flags
enum flags {
    C = (1 << 0),
    Z = (1 << 1),
    I = (1 << 2),
    D = (1 << 3),
    V = (1 << 4),
    N = (1 << 5),
};

struct RegisterFile {
    uint16_t PC;
    uint8_t A;
    uint8_t X;
    uint8_t Y;
    uint8_t SP;

    RegisterFile() {
        reset();
    }

    // Architected flags;
    Flags flags;

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

extern void run_instr(RegisterFile&, Memory&);

#define MNEMONICS() \
 MNEMONIC(BRK, 0) \
 MNEMONIC(ORA, (flags::Z | flags::N)) \
 MNEMONIC(ASL, 0) \

enum Mnemonic {
#define MNEMONIC(mnem, _flags)\
        mnem,
MNEMONICS()
#undef MNEMONIC
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

extern uint8_t addressing_mode_to_length(AddressingMode mode);

struct Opcode {
    Mnemonic mnem;
    uint8_t byte;
    AddressingMode mode;
};

extern const Opcode opcodeTable[];
extern const Opcode& mnem_addr_to_opcode(Mnemonic mnem, AddressingMode mode);
