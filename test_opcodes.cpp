#include <gtest/gtest.h>

#include "6502.hpp"
#include "assembler.hpp"

namespace {
TEST(Opcode, BRK) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    a
    .org(0x300)
    (BRK, IMMEDIATE, 12);
    ASSERT_EQ(mem[0x300], 0x00); // BRK
    mem.write16(0xfffe, 0xcafe);
    regs.PC = 0x300;
    regs.SP = 0xf8;

    run_instr(regs, mem);
    ASSERT_EQ(regs.SP, 0xf5);
    ASSERT_EQ(regs.PC, 0xcafe);
}

TEST(Opcode, ORA) {
    for (auto or_value : { 0, 1, 0xff }) {
        RegisterFile regs;
        Memory mem;
        Assembler a(mem);

        regs.PC = 0x300;
        a
            .org(regs.PC)(ORA, X_IND, 0xff);
        regs.PC = 0x300;
        EXPECT_EQ(mem[0x300], 0x01); // ORA X_IMD
        run_instr(regs, mem);

        EXPECT_EQ(regs.flags.Z, or_value == 0);
        EXPECT_EQ(regs.flags.N, (or_value & 0x80)== 1);
    }
}

}
