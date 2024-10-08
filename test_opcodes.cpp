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
    (BRK, IMPLIED, 12);
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
        .org(regs.PC)
        (ORA, IMMEDIATE, or_value);
        printf("or value: %d\n", or_value);
        regs.PC = 0x300;
        EXPECT_EQ(mem[0x300], 0x09); // ORA IMD
        run_instr(regs, mem);

        EXPECT_EQ(regs.A, or_value);
        EXPECT_EQ(regs.flags.Z, or_value == 0);
        EXPECT_EQ(regs.flags.N, (or_value & 0x80) != 0);
    }
}

TEST(Opcode, ASL) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    auto programStart = 0x300;
    regs.PC = programStart;
    a
    .org(0x300)
    (ORA, IMMEDIATE, 0x01)
    (ASL, ACCUMULATOR, 0x0);

    EXPECT_EQ(regs.A, 0x0);
    EXPECT_EQ(regs.PC, 0x300);
    run_instr(regs, mem); // ORA #0x01
    EXPECT_EQ(regs.A, 0x1);
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.C, 0);

    run_instr(regs, mem); // ASL *0x01
    EXPECT_EQ(regs.A, 0x2);
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.C, 0);
}

}
