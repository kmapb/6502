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

extern void run_instr(RegisterFile&, Memory&);

