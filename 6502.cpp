#include <cstdio>
#include <cstdint>

struct Memory {
    uint8_t bytes[1 << 16];
};

struct RegisterFile {
    uint16_t PC;
    uint8_t A;
    uint8_t X;
    uint8_t Y;
    uint8_t SP;
};

enum AddressingMode {
    ACCUMULATOR,
    ABS,
    ABS_X,
    ABS_Y,
    IMMEDIATE,
    IMPLIED,
    INDIRECT,
    X_IND,
    IND_Y,
    REL,
    ZPG,
    ZPG_X,
    ZPG_Y,
};

uint16_t
operand(RegisterFile& regs, Memory& mem, AddressingMode mode) {
   switch(mode) {
      case ACCUMULATOR: return regs.A;
      default: return 0;
   }
}

#define OPCODES() \
 OPCODE(BRK, IMPLIED) /* 0 */ \
 OPCODE(ORA

int main(int argc, char** argv) {
    printf("woot\n");
    return 0;
}
