#pragma once

#include <cstdio>
#include <cstdint>
#include <cstdlib>

#define NOT_IMPLEMENTED() abort()
#define NOT_REACHED() abort()


struct Device {
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t val) = 0;
    virtual ~Device() = default;
};

struct Bus {
    uint8_t ram[1 << 16] = {};
    Device* page_device[256] = {};

    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t val);
    uint16_t read16(uint16_t addr) const;
    void write16(uint16_t addr, uint16_t val);
    void reset();

    void map(uint8_t page, Device* dev);
    void map(uint8_t page_start, uint8_t page_end, Device* dev);

    uint8_t& operator[](uint16_t addr);
    const uint8_t& operator[](uint16_t addr) const;
};

using Memory = Bus;

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
 MNEMONIC(AND, (flags::Z | flags::N)) \
 MNEMONIC(EOR, (flags::Z | flags::N)) \
 MNEMONIC(ADC, 0) \
 MNEMONIC(SBC, 0) \
 MNEMONIC(ASL, 0) \
 MNEMONIC(LSR, 0) \
 MNEMONIC(ROL, 0) \
 MNEMONIC(ROR, 0) \
 MNEMONIC(RTI, 0) \
 MNEMONIC(JMP, 0) \
 MNEMONIC(JSR, 0) \
 MNEMONIC(RTS, 0) \
 MNEMONIC(LDA, (flags::Z | flags::N)) \
 MNEMONIC(LDX, 0) \
 MNEMONIC(LDY, 0) \
 MNEMONIC(STA, 0) \
 MNEMONIC(STX, 0) \
 MNEMONIC(STY, 0) \
 MNEMONIC(BCC, 0) \
 MNEMONIC(BCS, 0) \
 MNEMONIC(BEQ, 0) \
 MNEMONIC(BMI, 0) \
 MNEMONIC(BNE, 0) \
 MNEMONIC(BPL, 0) \
 MNEMONIC(BVC, 0) \
 MNEMONIC(BVS, 0) \
 MNEMONIC(INC, 0) \
 MNEMONIC(DEC, 0) \
 MNEMONIC(INX, 0) \
 MNEMONIC(INY, 0) \
 MNEMONIC(DEX, 0) \
 MNEMONIC(DEY, 0) \
 MNEMONIC(CLC, 0) \
 MNEMONIC(CLD, 0) \
 MNEMONIC(CLI, 0) \
 MNEMONIC(CLV, 0) \
 MNEMONIC(SEC, 0) \
 MNEMONIC(SED, 0) \
 MNEMONIC(SEI, 0) \
 MNEMONIC(TAX, 0) \
 MNEMONIC(TAY, 0) \
 MNEMONIC(TXA, 0) \
 MNEMONIC(TYA, 0) \
 MNEMONIC(TSX, 0) \
 MNEMONIC(TXS, 0) \
 MNEMONIC(CMP, 0) \
 MNEMONIC(CPX, 0) \
 MNEMONIC(CPY, 0) \
 MNEMONIC(PHA, 0) \
 MNEMONIC(PHP, 0) \
 MNEMONIC(PLA, 0) \
 MNEMONIC(PLP, 0) \
 MNEMONIC(BIT, 0) \
 MNEMONIC(NOP, 0) \

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
