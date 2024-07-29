#include <gtest/gtest.h>

#include "6502.hpp"

namespace {
TEST(Opcode, BRK) {
    RegisterFile regs;
    Memory mem;

    regs.PC = 0x300;
    mem[0x300] = OPCODE_BRK_IMPLIED;
    mem[0x301] = 0xff;
    mem.write16(0xfffe, 0xcafe);
    regs.SP = 0xf8;

    run_instr(regs, mem);
    ASSERT_EQ(regs.SP, 0xf5);
    ASSERT_EQ(regs.PC, 0xcafe);
}

}
