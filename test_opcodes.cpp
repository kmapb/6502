#include <gtest/gtest.h>

#include "6502.hpp"
#include "assembler.hpp"

namespace {
TEST(Opcode, JMP_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    a.org(0x300)
    (JMP, ABS, 0x1234);

    EXPECT_EQ(mem[0x300], 0x4c);  // JMP absolute opcode
    EXPECT_EQ(mem[0x301], 0x34);  // Low byte
    EXPECT_EQ(mem[0x302], 0x12);  // High byte

    regs.PC = 0x300;
    run_instr(regs, mem);

    EXPECT_EQ(regs.PC, 0x1234);
}

TEST(Opcode, JMP_INDIRECT) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Set up indirect pointer at $2000 pointing to $1234
    mem[0x2000] = 0x34;  // Low byte of target
    mem[0x2001] = 0x12;  // High byte of target

    a.org(0x300)
    (JMP, INDIRECT, 0x2000);

    EXPECT_EQ(mem[0x300], 0x6c);  // JMP indirect opcode

    regs.PC = 0x300;
    run_instr(regs, mem);

    EXPECT_EQ(regs.PC, 0x1234);
}

// Test the infamous NMOS 6502 JMP indirect bug at page boundary
TEST(Opcode, JMP_INDIRECT_page_bug) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Set up indirect pointer at $20FF (page boundary)
    // Low byte at $20FF, high byte should come from $2000 (not $2100) due to bug
    mem[0x20ff] = 0x34;  // Low byte of target
    mem[0x2100] = 0x56;  // This would be high byte if bug didn't exist
    mem[0x2000] = 0x12;  // This is where high byte actually comes from (bug)

    a.org(0x300)
    (JMP, INDIRECT, 0x20ff);

    regs.PC = 0x300;
    run_instr(regs, mem);

    // Due to bug: low from $20FF (0x34), high from $2000 (0x12)
    EXPECT_EQ(regs.PC, 0x1234);  // NOT 0x5634
}

TEST(Opcode, JSR) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    a.org(0x300)
    (JSR, ABS, 0x1234);

    EXPECT_EQ(mem[0x300], 0x20);  // JSR opcode

    regs.PC = 0x300;
    regs.SP = 0xff;
    run_instr(regs, mem);

    EXPECT_EQ(regs.PC, 0x1234);
    EXPECT_EQ(regs.SP, 0xfd);    // Pushed 2 bytes

    // Verify stack contains return address (PC+2 = 0x302)
    EXPECT_EQ(mem[0x1ff], 0x03);  // High byte of 0x302
    EXPECT_EQ(mem[0x1fe], 0x02);  // Low byte of 0x302
}

TEST(Opcode, RTS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Set up stack with return address 0x1233
    // RTS will add 1 to get 0x1234
    mem[0x1ff] = 0x12;  // High byte
    mem[0x1fe] = 0x33;  // Low byte
    regs.SP = 0xfd;     // Points below pushed data

    a.org(0x400)
    (RTS, IMPLIED, 0);

    EXPECT_EQ(mem[0x400], 0x60);  // RTS opcode

    regs.PC = 0x400;
    run_instr(regs, mem);

    EXPECT_EQ(regs.PC, 0x1234);  // 0x1233 + 1
    EXPECT_EQ(regs.SP, 0xff);    // Pulled 2 bytes
}

TEST(Opcode, JSR_RTS_roundtrip) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // JSR at 0x300, subroutine at 0x400, RTS returns to 0x303
    a.org(0x300)
    (JSR, ABS, 0x400);
    // Next instruction would be at 0x303

    a.org(0x400)
    (RTS, IMPLIED, 0);

    regs.PC = 0x300;
    regs.SP = 0xff;

    // Execute JSR
    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x400);
    EXPECT_EQ(regs.SP, 0xfd);

    // Execute RTS
    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x303);  // Returns to instruction after JSR
    EXPECT_EQ(regs.SP, 0xff);
}

TEST(Opcode, RTI) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Set up stack as if BRK had pushed PC=0x1234 and status
    // Stack layout (growing down from 0x1ff):
    //   0x1ff: PC high (0x12)
    //   0x1fe: PC low (0x34)
    //   0x1fd: Status
    // Status byte format: NV1BDIZC
    // We want: N=1, V=1, D=0, I=0, Z=1, C=1 = 0b11100011 = 0xe3
    mem[0x1ff] = 0x12;
    mem[0x1fe] = 0x34;
    mem[0x1fd] = 0xe3;  // N=1,V=1,(1=1),(B=0),D=0,I=0,Z=1,C=1
    regs.SP = 0xfc;     // Points below the pushed data

    // Set all flags to opposite values to verify they get restored
    regs.flags.N = 0;
    regs.flags.V = 0;
    regs.flags.D = 1;  // Set to verify it gets cleared
    regs.flags.I = 1;  // Set to verify it gets cleared
    regs.flags.Z = 0;
    regs.flags.C = 0;

    a.org(0x300)
    (RTI, IMPLIED, 0);

    regs.PC = 0x300;
    run_instr(regs, mem);

    EXPECT_EQ(regs.PC, 0x1234);
    EXPECT_EQ(regs.SP, 0xff);    // SP restored (incremented 3 times)
    EXPECT_EQ(regs.flags.N, 1);
    EXPECT_EQ(regs.flags.V, 1);
    EXPECT_EQ(regs.flags.D, 0);
    EXPECT_EQ(regs.flags.I, 0);
    EXPECT_EQ(regs.flags.Z, 1);
    EXPECT_EQ(regs.flags.C, 1);
}

// Test BRK followed by RTI returns to correct location
TEST(Opcode, BRK_RTI_roundtrip) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Set up BRK at 0x300
    a.org(0x300)
    (BRK, IMPLIED, 0);

    // Set up RTI at interrupt handler location 0x400
    a.org(0x400)
    (RTI, IMPLIED, 0);

    // Set interrupt vector to point to our handler
    mem.write16(0xfffe, 0x400);

    regs.PC = 0x300;
    regs.SP = 0xff;
    regs.flags.C = 1;
    regs.flags.N = 1;

    // Execute BRK
    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x400);
    EXPECT_EQ(regs.SP, 0xfc);

    // Execute RTI
    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x302);  // BRK pushes PC+2
    EXPECT_EQ(regs.SP, 0xff);
    EXPECT_EQ(regs.flags.C, 1);
    EXPECT_EQ(regs.flags.N, 1);
}

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
    regs.flags.C = 1;
    regs.flags.Z = 0;
    regs.flags.N = 1;

    run_instr(regs, mem);
    ASSERT_EQ(regs.SP, 0xf5);
    ASSERT_EQ(regs.PC, 0xcafe);

    // Verify stack layout: PC pushed as high byte first, then low byte, then status
    // PC+2 = 0x302, so high=0x03, low=0x02
    // Stack grows down, so after pushing:
    //   mem[0x1f8] = PC high (0x03)
    //   mem[0x1f7] = PC low (0x02)
    //   mem[0x1f6] = Status with B flag set
    EXPECT_EQ(mem[0x1f8], 0x03);  // PC high byte
    EXPECT_EQ(mem[0x1f7], 0x02);  // PC low byte
    // Status: N=1, V=0, (1), B=1, D=0, I=0, Z=0, C=1 = 0b10110001 = 0xb1
    EXPECT_EQ(mem[0x1f6], 0xb1);  // Status with B=1
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

TEST(Opcode, ORA_IND_Y) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x0f;
    regs.Y = 0x10;
    // ($20),Y -> read pointer from $20,$21, then add Y
    mem[0x20] = 0x00;  // Low byte of base address
    mem[0x21] = 0x12;  // High byte of base address -> $1200
    mem[0x1210] = 0xf0;  // Value at $1200 + Y ($10) = $1210

    a.org(0x300)
    (ORA, IND_Y, 0x20);  // ORA ($20),Y

    EXPECT_EQ(mem[0x300], 0x11);  // ORA IND_Y opcode
    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xff);      // 0x0f | 0xf0 = 0xff
    EXPECT_EQ(regs.PC, 0x302);    // Advanced by 2 bytes
}

TEST(Opcode, ORA_X_IND) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x0f;
    regs.X = 0x10;
    // ($20,X) with X=0x10 -> read pointer from $30,$31
    mem[0x30] = 0x34;  // Low byte of target address
    mem[0x31] = 0x12;  // High byte of target address
    mem[0x1234] = 0xf0;  // Value at target address

    a.org(0x300)
    (ORA, X_IND, 0x20);  // ORA ($20,X)

    EXPECT_EQ(mem[0x300], 0x01);  // ORA X_IND opcode
    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xff);      // 0x0f | 0xf0 = 0xff
    EXPECT_EQ(regs.PC, 0x302);    // Advanced by 2 bytes
}

TEST(Opcode, ORA_ABS_X) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x0f;
    regs.X = 0x10;
    mem[0x1244] = 0xf0;  // Value at $1234 + X ($10) = $1244

    a.org(0x300)
    (ORA, ABS_X, 0x1234);  // ORA $1234,X

    EXPECT_EQ(mem[0x300], 0x1d);  // ORA ABS_X opcode
    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xff);      // 0x0f | 0xf0 = 0xff
    EXPECT_EQ(regs.PC, 0x303);    // Advanced by 3 bytes
}

TEST(Opcode, ORA_ABS_Y) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x0f;
    regs.Y = 0x20;
    mem[0x1254] = 0xf0;  // Value at $1234 + Y ($20) = $1254

    a.org(0x300)
    (ORA, ABS_Y, 0x1234);  // ORA $1234,Y

    EXPECT_EQ(mem[0x300], 0x19);  // ORA ABS_Y opcode
    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xff);      // 0x0f | 0xf0 = 0xff
    EXPECT_EQ(regs.PC, 0x303);    // Advanced by 3 bytes
}

TEST(Opcode, ORA_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x0f;
    mem[0x1234] = 0xf0;  // Value at absolute address $1234

    a.org(0x300)
    (ORA, ABS, 0x1234);  // ORA $1234

    EXPECT_EQ(mem[0x300], 0x0d);  // ORA ABS opcode
    EXPECT_EQ(mem[0x301], 0x34);  // Low byte
    EXPECT_EQ(mem[0x302], 0x12);  // High byte
    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xff);      // 0x0f | 0xf0 = 0xff
    EXPECT_EQ(regs.PC, 0x303);    // Advanced by 3 bytes
}

TEST(Opcode, ORA_ZPG_X) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x0f;
    regs.X = 0x10;
    mem[0x52] = 0xf0;  // Value at zero page address $42 + X ($10) = $52

    a.org(0x300)
    (ORA, ZPG_X, 0x42);  // ORA $42,X

    EXPECT_EQ(mem[0x300], 0x15);  // ORA ZPG_X opcode
    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xff);      // 0x0f | 0xf0 = 0xff
    EXPECT_EQ(regs.flags.N, 1);
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.PC, 0x302);    // Advanced by 2 bytes
}

// Test ZPG_X wraps within zero page (doesn't cross into page 1)
TEST(Opcode, ORA_ZPG_X_wrap) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x00;
    regs.X = 0x20;
    mem[0x10] = 0x42;   // $f0 + $20 = $110, but should wrap to $10
    mem[0x110] = 0xff;  // This should NOT be read

    a.org(0x300)
    (ORA, ZPG_X, 0xf0);  // ORA $f0,X

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x42);  // Should read from $10, not $110
}

TEST(Opcode, ORA_ZPG) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x0f;
    mem[0x42] = 0xf0;  // Value at zero page address $42

    a.org(0x300)
    (ORA, ZPG, 0x42);  // ORA $42

    EXPECT_EQ(mem[0x300], 0x05);  // ORA ZPG opcode
    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xff);      // 0x0f | 0xf0 = 0xff
    EXPECT_EQ(regs.flags.N, 1);   // Negative (bit 7 set)
    EXPECT_EQ(regs.flags.Z, 0);   // Not zero
    EXPECT_EQ(regs.PC, 0x302);    // Advanced by 2 bytes
}

// Test that ORA does not modify the carry flag (exposes bug at 6502.cpp:133)
TEST(Opcode, ORA_does_not_modify_carry) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.C = 0;  // Carry is clear
    regs.A = 0x80;     // A has bit 7 set

    a.org(0x300)
    (ORA, IMMEDIATE, 0x00);  // ORA #0 - should not change A or C

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x80);     // A unchanged
    EXPECT_EQ(regs.flags.N, 1);  // Negative flag set (bit 7)
    EXPECT_EQ(regs.flags.Z, 0);  // Not zero
    EXPECT_EQ(regs.flags.C, 0);  // Carry should NOT be modified by ORA
}

// LDA tests
TEST(Opcode, LDA_IMM) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x00;

    a.org(0x300)
    (LDA, IMMEDIATE, 0x42);

    EXPECT_EQ(mem[0x300], 0xa9);  // LDA immediate opcode
    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x42);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.PC, 0x302);
}

TEST(Opcode, LDA_zero) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0xff;

    a.org(0x300)
    (LDA, IMMEDIATE, 0x00);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
    EXPECT_EQ(regs.flags.N, 0);
}

TEST(Opcode, LDA_negative) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;

    a.org(0x300)
    (LDA, IMMEDIATE, 0x80);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x80);
    EXPECT_EQ(regs.flags.N, 1);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, LDA_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x1234] = 0x55;

    a.org(0x300)
    (LDA, ABS, 0x1234);

    EXPECT_EQ(mem[0x300], 0xad);  // LDA absolute opcode
    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x55);
    EXPECT_EQ(regs.PC, 0x303);
}

TEST(Opcode, LDA_ZPG) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x42] = 0xaa;

    a.org(0x300)
    (LDA, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xaa);
}

TEST(Opcode, LDA_IND_Y) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0x10;
    mem[0x20] = 0x00;   // Low byte of base
    mem[0x21] = 0x12;   // High byte -> $1200
    mem[0x1210] = 0x77; // Value at $1200 + Y

    a.org(0x300)
    (LDA, IND_Y, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x77);
}

// AND tests
TEST(Opcode, AND_IMM) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0b11110000;

    a.org(0x300)
    (AND, IMMEDIATE, 0b10101010);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0b10100000);
    EXPECT_EQ(regs.flags.N, 1);  // Bit 7 set
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, AND_zero_result) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0b11110000;

    a.org(0x300)
    (AND, IMMEDIATE, 0b00001111);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
    EXPECT_EQ(regs.flags.N, 0);
}

// EOR tests
TEST(Opcode, EOR_IMM) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0b11110000;

    a.org(0x300)
    (EOR, IMMEDIATE, 0b10101010);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0b01011010);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, EOR_self_zeros) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x55;

    a.org(0x300)
    (EOR, IMMEDIATE, 0x55);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
}

// ADC tests
TEST(Opcode, ADC_simple) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x10;
    regs.flags.C = 0;

    a.org(0x300)
    (ADC, IMMEDIATE, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x30);
    EXPECT_EQ(regs.flags.C, 0);
    EXPECT_EQ(regs.flags.V, 0);
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.flags.N, 0);
}

TEST(Opcode, ADC_with_carry_in) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x10;
    regs.flags.C = 1;

    a.org(0x300)
    (ADC, IMMEDIATE, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x31);  // 0x10 + 0x20 + 1
}

TEST(Opcode, ADC_carry_out) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0xff;
    regs.flags.C = 0;

    a.org(0x300)
    (ADC, IMMEDIATE, 0x01);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x00);
    EXPECT_EQ(regs.flags.C, 1);
    EXPECT_EQ(regs.flags.Z, 1);
}

TEST(Opcode, ADC_overflow_positive) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // 0x7f + 0x01 = 0x80 (127 + 1 = -128 in signed, overflow!)
    regs.PC = 0x300;
    regs.A = 0x7f;
    regs.flags.C = 0;

    a.org(0x300)
    (ADC, IMMEDIATE, 0x01);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x80);
    EXPECT_EQ(regs.flags.V, 1);  // Overflow: positive + positive = negative
    EXPECT_EQ(regs.flags.N, 1);
    EXPECT_EQ(regs.flags.C, 0);
}

TEST(Opcode, ADC_overflow_negative) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // 0x80 + 0x80 = 0x00 (-128 + -128 = 0 in signed, overflow!)
    regs.PC = 0x300;
    regs.A = 0x80;
    regs.flags.C = 0;

    a.org(0x300)
    (ADC, IMMEDIATE, 0x80);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x00);
    EXPECT_EQ(regs.flags.V, 1);  // Overflow: negative + negative = positive
    EXPECT_EQ(regs.flags.C, 1);
    EXPECT_EQ(regs.flags.Z, 1);
}

// SBC tests
TEST(Opcode, SBC_simple) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x30;
    regs.flags.C = 1;  // No borrow

    a.org(0x300)
    (SBC, IMMEDIATE, 0x10);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x20);
    EXPECT_EQ(regs.flags.C, 1);  // No borrow
    EXPECT_EQ(regs.flags.V, 0);
}

TEST(Opcode, SBC_with_borrow) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x30;
    regs.flags.C = 0;  // Borrow

    a.org(0x300)
    (SBC, IMMEDIATE, 0x10);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x1f);  // 0x30 - 0x10 - 1
    EXPECT_EQ(regs.flags.C, 1);
}

TEST(Opcode, SBC_borrow_out) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x00;
    regs.flags.C = 1;

    a.org(0x300)
    (SBC, IMMEDIATE, 0x01);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xff);
    EXPECT_EQ(regs.flags.C, 0);  // Borrow occurred
    EXPECT_EQ(regs.flags.N, 1);
}

TEST(Opcode, SBC_overflow) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // 0x80 - 0x01 = 0x7f (-128 - 1 = 127 in signed, overflow!)
    regs.PC = 0x300;
    regs.A = 0x80;
    regs.flags.C = 1;

    a.org(0x300)
    (SBC, IMMEDIATE, 0x01);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x7f);
    EXPECT_EQ(regs.flags.V, 1);  // Overflow
    EXPECT_EQ(regs.flags.N, 0);
}

// LSR tests
TEST(Opcode, LSR_ACC) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0b10000010;

    a.org(0x300)
    (LSR, ACCUMULATOR, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0b01000001);
    EXPECT_EQ(regs.flags.C, 0);  // Bit 0 was 0
    EXPECT_EQ(regs.flags.N, 0);  // Always 0 after LSR
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, LSR_carry_out) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0b00000001;

    a.org(0x300)
    (LSR, ACCUMULATOR, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x00);
    EXPECT_EQ(regs.flags.C, 1);  // Bit 0 shifted out
    EXPECT_EQ(regs.flags.Z, 1);
}

TEST(Opcode, LSR_ZPG) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x42] = 0b11111110;

    a.org(0x300)
    (LSR, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x42], 0b01111111);
    EXPECT_EQ(regs.flags.C, 0);
}

// ROL tests
TEST(Opcode, ROL_ACC) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0b10000001;
    regs.flags.C = 0;

    a.org(0x300)
    (ROL, ACCUMULATOR, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0b00000010);  // Shifted left, C=0 shifted in
    EXPECT_EQ(regs.flags.C, 1);     // Bit 7 shifted out
    EXPECT_EQ(regs.flags.N, 0);
}

TEST(Opcode, ROL_with_carry) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0b00000001;
    regs.flags.C = 1;

    a.org(0x300)
    (ROL, ACCUMULATOR, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0b00000011);  // C=1 shifted into bit 0
    EXPECT_EQ(regs.flags.C, 0);     // Bit 7 was 0
}

// ROR tests
TEST(Opcode, ROR_ACC) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0b10000001;
    regs.flags.C = 0;

    a.org(0x300)
    (ROR, ACCUMULATOR, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0b01000000);  // Shifted right, C=0 shifted into bit 7
    EXPECT_EQ(regs.flags.C, 1);     // Bit 0 shifted out
    EXPECT_EQ(regs.flags.N, 0);
}

TEST(Opcode, ROR_with_carry) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0b00000010;
    regs.flags.C = 1;

    a.org(0x300)
    (ROR, ACCUMULATOR, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0b10000001);  // C=1 shifted into bit 7
    EXPECT_EQ(regs.flags.C, 0);     // Bit 0 was 0
    EXPECT_EQ(regs.flags.N, 1);     // Bit 7 now set
}

TEST(Opcode, ASL_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x1234] = 0x40;  // Value to shift

    a.org(0x300)
    (ASL, ABS, 0x1234);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x1234], 0x80);  // 0x40 << 1 = 0x80
    EXPECT_EQ(regs.flags.C, 0);
    EXPECT_EQ(regs.flags.N, 1);    // Bit 7 set
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.PC, 0x303);
}

TEST(Opcode, ASL_ABS_X) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x10;
    mem[0x1244] = 0x01;  // Value at $1234 + X

    a.org(0x300)
    (ASL, ABS_X, 0x1234);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x1244], 0x02);  // 0x01 << 1 = 0x02
    EXPECT_EQ(regs.flags.C, 0);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.PC, 0x303);
}

TEST(Opcode, ASL_ZPG_X) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x10;
    mem[0x52] = 0x80;  // Value at $42 + X, will shift to 0 with carry

    a.org(0x300)
    (ASL, ZPG_X, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x52], 0x00);   // 0x80 << 1 = 0x100, truncated to 0x00
    EXPECT_EQ(regs.flags.C, 1);   // Carry set
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 1);   // Zero
    EXPECT_EQ(regs.PC, 0x302);
}

TEST(Opcode, ASL_ZPG) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x00;       // A should NOT be modified
    mem[0x42] = 0x81;    // Value to shift (bit 7 set, bit 0 set)

    a.org(0x300)
    (ASL, ZPG, 0x42);    // ASL $42

    EXPECT_EQ(mem[0x300], 0x06);  // ASL ZPG opcode
    run_instr(regs, mem);

    EXPECT_EQ(mem[0x42], 0x02);   // 0x81 << 1 = 0x102, truncated to 0x02
    EXPECT_EQ(regs.A, 0x00);      // A should be unchanged
    EXPECT_EQ(regs.flags.C, 1);   // Carry set (bit 7 shifted out)
    EXPECT_EQ(regs.flags.Z, 0);   // Not zero
    EXPECT_EQ(regs.flags.N, 0);   // Not negative
    EXPECT_EQ(regs.PC, 0x302);    // Advanced by 2 bytes
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
