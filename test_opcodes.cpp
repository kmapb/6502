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

// CMP tests
TEST(Opcode, CMP_equal) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x42;

    a.org(0x300)
    (CMP, IMMEDIATE, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x42);  // A unchanged
    EXPECT_EQ(regs.flags.Z, 1);
    EXPECT_EQ(regs.flags.C, 1);  // A >= M
    EXPECT_EQ(regs.flags.N, 0);
}

TEST(Opcode, CMP_greater) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x50;

    a.org(0x300)
    (CMP, IMMEDIATE, 0x30);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.flags.C, 1);  // A >= M
    EXPECT_EQ(regs.flags.N, 0);  // 0x50 - 0x30 = 0x20
}

TEST(Opcode, CMP_less) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x10;

    a.org(0x300)
    (CMP, IMMEDIATE, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.flags.C, 0);  // A < M, borrow
    EXPECT_EQ(regs.flags.N, 1);  // 0x10 - 0x20 = 0xF0
}

TEST(Opcode, CMP_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x42;
    mem[0x1234] = 0x42;

    a.org(0x300)
    (CMP, ABS, 0x1234);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.Z, 1);
    EXPECT_EQ(regs.flags.C, 1);
}

// CPX tests
TEST(Opcode, CPX_equal) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x10;

    a.org(0x300)
    (CPX, IMMEDIATE, 0x10);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.Z, 1);
    EXPECT_EQ(regs.flags.C, 1);
}

TEST(Opcode, CPX_less) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x05;

    a.org(0x300)
    (CPX, IMMEDIATE, 0x10);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.flags.C, 0);
}

// CPY tests
TEST(Opcode, CPY_equal) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0x10;

    a.org(0x300)
    (CPY, IMMEDIATE, 0x10);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.Z, 1);
    EXPECT_EQ(regs.flags.C, 1);
}

TEST(Opcode, CPY_greater) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0x20;

    a.org(0x300)
    (CPY, IMMEDIATE, 0x10);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.flags.C, 1);
}

// PHA / PLA tests
TEST(Opcode, PHA_PLA_roundtrip) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.SP = 0xff;
    regs.A = 0x42;

    a.org(0x300)
    (PHA, IMPLIED, 0)
    (LDA, IMMEDIATE, 0x00)  // Clear A
    (PLA, IMPLIED, 0);

    run_instr(regs, mem);  // PHA
    EXPECT_EQ(regs.SP, 0xfe);

    run_instr(regs, mem);  // LDA #$00
    EXPECT_EQ(regs.A, 0x00);

    run_instr(regs, mem);  // PLA
    EXPECT_EQ(regs.A, 0x42);
    EXPECT_EQ(regs.SP, 0xff);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, PLA_flags) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.SP = 0xff;
    regs.A = 0x00;

    // Push 0, pull it back to test Z flag
    a.org(0x300)
    (PHA, IMPLIED, 0)
    (PLA, IMPLIED, 0);

    run_instr(regs, mem);  // PHA
    run_instr(regs, mem);  // PLA

    EXPECT_EQ(regs.A, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
}

// PHP / PLP tests
TEST(Opcode, PHP_PLP_roundtrip) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.SP = 0xff;
    regs.flags.C = 1;
    regs.flags.Z = 0;
    regs.flags.I = 1;
    regs.flags.D = 0;
    regs.flags.V = 1;
    regs.flags.N = 0;

    a.org(0x300)
    (PHP, IMPLIED, 0)
    (PLP, IMPLIED, 0);

    run_instr(regs, mem);  // PHP

    // Scramble all flags
    regs.flags.C = 0;
    regs.flags.Z = 1;
    regs.flags.I = 0;
    regs.flags.D = 1;
    regs.flags.V = 0;
    regs.flags.N = 1;

    run_instr(regs, mem);  // PLP - should restore original flags

    EXPECT_EQ(regs.flags.C, 1);
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.flags.I, 1);
    EXPECT_EQ(regs.flags.D, 0);
    EXPECT_EQ(regs.flags.V, 1);
    EXPECT_EQ(regs.flags.N, 0);
}

// BIT tests
TEST(Opcode, BIT_zero_result) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x0f;
    mem[0x42] = 0xf0;  // No bits in common with A

    a.org(0x300)
    (BIT, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x0f);  // A unchanged
    EXPECT_EQ(regs.flags.Z, 1);  // A & M == 0
    EXPECT_EQ(regs.flags.N, 1);  // M bit 7 set
    EXPECT_EQ(regs.flags.V, 1);  // M bit 6 set
}

TEST(Opcode, BIT_nonzero_result) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0xff;
    mem[0x42] = 0x3f;  // bits 6,7 clear

    a.org(0x300)
    (BIT, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.Z, 0);  // A & M != 0
    EXPECT_EQ(regs.flags.N, 0);  // M bit 7 clear
    EXPECT_EQ(regs.flags.V, 0);  // M bit 6 clear
}

TEST(Opcode, BIT_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x01;
    mem[0x1234] = 0xc1;  // bits 7,6 set, bit 0 set

    a.org(0x300)
    (BIT, ABS, 0x1234);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.Z, 0);  // A & M = 0x01
    EXPECT_EQ(regs.flags.N, 1);  // M bit 7
    EXPECT_EQ(regs.flags.V, 1);  // M bit 6
}

// NOP test
TEST(Opcode, NOP) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x42;
    regs.X = 0x10;
    regs.flags.C = 1;
    regs.flags.Z = 0;

    a.org(0x300)
    (NOP, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.PC, 0x301);
    EXPECT_EQ(regs.A, 0x42);  // Nothing changed
    EXPECT_EQ(regs.X, 0x10);
    EXPECT_EQ(regs.flags.C, 1);
    EXPECT_EQ(regs.flags.Z, 0);
}

// CMP + BEQ integration: compare and branch pattern
TEST(Opcode, CMP_BEQ_pattern) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x42;

    a.org(0x300)
    (CMP, IMMEDIATE, 0x42)
    (BEQ, REL, 0x10);

    run_instr(regs, mem);  // CMP
    EXPECT_EQ(regs.flags.Z, 1);

    run_instr(regs, mem);  // BEQ - should be taken
    EXPECT_EQ(regs.PC, 0x314);  // 0x304 + 0x10
}

// Transfer instruction tests
TEST(Opcode, TAX_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x42;
    regs.X = 0x00;

    a.org(0x300)
    (TAX, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0x42);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, TAX_zero) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x00;
    regs.X = 0xff;

    a.org(0x300)
    (TAX, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
}

TEST(Opcode, TAY_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x80;

    a.org(0x300)
    (TAY, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.Y, 0x80);
    EXPECT_EQ(regs.flags.N, 1);
}

TEST(Opcode, TXA_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x42;
    regs.A = 0x00;

    a.org(0x300)
    (TXA, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0x42);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, TYA_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0xff;
    regs.A = 0x00;

    a.org(0x300)
    (TYA, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.A, 0xff);
    EXPECT_EQ(regs.flags.N, 1);
}

TEST(Opcode, TSX_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.SP = 0xfd;

    a.org(0x300)
    (TSX, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0xfd);
    EXPECT_EQ(regs.flags.N, 1);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, TXS_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0xff;
    regs.SP = 0x00;

    a.org(0x300)
    (TXS, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.SP, 0xff);
}

TEST(Opcode, TXS_no_flags) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x00;
    regs.flags.Z = 0;
    regs.flags.N = 1;

    a.org(0x300)
    (TXS, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.SP, 0x00);
    // TXS must NOT affect flags
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.flags.N, 1);
}

// Flag instruction tests
TEST(Opcode, CLC) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.C = 1;

    a.org(0x300)
    (CLC, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.C, 0);
    EXPECT_EQ(regs.PC, 0x301);
}

TEST(Opcode, SEC) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.C = 0;

    a.org(0x300)
    (SEC, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.C, 1);
}

TEST(Opcode, CLD) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.D = 1;

    a.org(0x300)
    (CLD, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.D, 0);
}

TEST(Opcode, SED) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.D = 0;

    a.org(0x300)
    (SED, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.D, 1);
}

TEST(Opcode, CLI) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.I = 1;

    a.org(0x300)
    (CLI, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.I, 0);
}

TEST(Opcode, SEI) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.I = 0;

    a.org(0x300)
    (SEI, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.I, 1);
}

TEST(Opcode, CLV) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.V = 1;

    a.org(0x300)
    (CLV, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.flags.V, 0);
}

// SEC + SBC integration: verify SEC before SBC pattern
TEST(Opcode, SEC_SBC_pattern) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x50;
    regs.flags.C = 0;  // Start with carry clear

    a.org(0x300)
    (SEC, IMPLIED, 0)
    (SBC, IMMEDIATE, 0x10);

    run_instr(regs, mem);  // SEC
    EXPECT_EQ(regs.flags.C, 1);

    run_instr(regs, mem);  // SBC
    EXPECT_EQ(regs.A, 0x40);  // 0x50 - 0x10 = 0x40
    EXPECT_EQ(regs.flags.C, 1);  // No borrow
}

// CLC + ADC integration: verify CLC before ADC pattern
TEST(Opcode, CLC_ADC_pattern) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x50;
    regs.flags.C = 1;  // Start with carry set

    a.org(0x300)
    (CLC, IMPLIED, 0)
    (ADC, IMMEDIATE, 0x10);

    run_instr(regs, mem);  // CLC
    EXPECT_EQ(regs.flags.C, 0);

    run_instr(regs, mem);  // ADC
    EXPECT_EQ(regs.A, 0x60);  // 0x50 + 0x10 = 0x60
}

// INC tests
TEST(Opcode, INC_ZPG) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x42] = 0x10;

    a.org(0x300)
    (INC, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x42], 0x11);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, INC_wrap) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x42] = 0xff;

    a.org(0x300)
    (INC, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x42], 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
    EXPECT_EQ(regs.flags.N, 0);
}

TEST(Opcode, INC_negative) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x42] = 0x7f;

    a.org(0x300)
    (INC, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x42], 0x80);
    EXPECT_EQ(regs.flags.N, 1);
}

TEST(Opcode, INC_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x1234] = 0x05;

    a.org(0x300)
    (INC, ABS, 0x1234);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x1234], 0x06);
}

// DEC tests
TEST(Opcode, DEC_ZPG) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x42] = 0x10;

    a.org(0x300)
    (DEC, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x42], 0x0f);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, DEC_wrap) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x42] = 0x00;

    a.org(0x300)
    (DEC, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x42], 0xff);
    EXPECT_EQ(regs.flags.N, 1);
}

TEST(Opcode, DEC_to_zero) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    mem[0x42] = 0x01;

    a.org(0x300)
    (DEC, ZPG, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x42], 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
}

// INX tests
TEST(Opcode, INX_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x10;

    a.org(0x300)
    (INX, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0x11);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
    EXPECT_EQ(regs.PC, 0x301);
}

TEST(Opcode, INX_wrap) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0xff;

    a.org(0x300)
    (INX, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
}

// INY tests
TEST(Opcode, INY_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0x10;

    a.org(0x300)
    (INY, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.Y, 0x11);
}

TEST(Opcode, INY_wrap) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0xff;

    a.org(0x300)
    (INY, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.Y, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
}

// DEX tests
TEST(Opcode, DEX_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x10;

    a.org(0x300)
    (DEX, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0x0f);
}

TEST(Opcode, DEX_to_zero) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x01;

    a.org(0x300)
    (DEX, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
}

TEST(Opcode, DEX_wrap) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x00;

    a.org(0x300)
    (DEX, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0xff);
    EXPECT_EQ(regs.flags.N, 1);
}

// DEY tests
TEST(Opcode, DEY_basic) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0x10;

    a.org(0x300)
    (DEY, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.Y, 0x0f);
}

TEST(Opcode, DEY_wrap) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0x00;

    a.org(0x300)
    (DEY, IMPLIED, 0);

    run_instr(regs, mem);

    EXPECT_EQ(regs.Y, 0xff);
    EXPECT_EQ(regs.flags.N, 1);
}

// Multi-instruction: count down loop with DEX + BNE
TEST(Opcode, DEX_BNE_loop) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Simple loop: DEX; BNE -2 (loop back to DEX)
    regs.PC = 0x300;
    regs.X = 0x03;

    a.org(0x300)
    (DEX, IMPLIED, 0)         // $300
    (BNE, REL, 0xfd);         // $301: branch back -3 -> $300

    // Run 3 iterations: X goes 3->2->1->0
    run_instr(regs, mem);  // DEX -> X=2
    run_instr(regs, mem);  // BNE -> taken, back to $300
    run_instr(regs, mem);  // DEX -> X=1
    run_instr(regs, mem);  // BNE -> taken, back to $300
    run_instr(regs, mem);  // DEX -> X=0
    run_instr(regs, mem);  // BNE -> not taken, fall through

    EXPECT_EQ(regs.X, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
    EXPECT_EQ(regs.PC, 0x303);  // Past the BNE
}

// BCC tests (Branch if Carry Clear)
TEST(Opcode, BCC_taken_forward) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.C = 0;  // Carry clear - branch should be taken

    a.org(0x300)
    (BCC, REL, 0x10);  // Branch forward 16 bytes

    EXPECT_EQ(mem[0x300], 0x90);  // BCC opcode
    run_instr(regs, mem);

    // Target = PC + 2 + offset = 0x300 + 2 + 0x10 = 0x312
    EXPECT_EQ(regs.PC, 0x312);
}

TEST(Opcode, BCC_taken_backward) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x320;
    regs.flags.C = 0;  // Carry clear - branch should be taken

    a.org(0x320)
    (BCC, REL, 0xf0);  // Branch backward 16 bytes (-16 = 0xf0 in signed)

    run_instr(regs, mem);

    // Target = PC + 2 + offset = 0x320 + 2 + (-16) = 0x312
    EXPECT_EQ(regs.PC, 0x312);
}

TEST(Opcode, BCC_not_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.C = 1;  // Carry set - branch should NOT be taken

    a.org(0x300)
    (BCC, REL, 0x10);

    run_instr(regs, mem);

    // Not taken: PC = PC + 2 (instruction length)
    EXPECT_EQ(regs.PC, 0x302);
}

TEST(Opcode, BCC_zero_offset) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.C = 0;

    a.org(0x300)
    (BCC, REL, 0x00);  // Branch with zero offset

    run_instr(regs, mem);

    // Target = PC + 2 + 0 = 0x302
    EXPECT_EQ(regs.PC, 0x302);
}

// BCS tests (Branch if Carry Set)
TEST(Opcode, BCS_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.C = 1;

    a.org(0x300)
    (BCS, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x312);
}

TEST(Opcode, BCS_not_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.C = 0;

    a.org(0x300)
    (BCS, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x302);
}

// BEQ tests (Branch if Equal / Zero set)
TEST(Opcode, BEQ_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.Z = 1;

    a.org(0x300)
    (BEQ, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x312);
}

TEST(Opcode, BEQ_not_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.Z = 0;

    a.org(0x300)
    (BEQ, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x302);
}

// BNE tests (Branch if Not Equal / Zero clear)
TEST(Opcode, BNE_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.Z = 0;

    a.org(0x300)
    (BNE, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x312);
}

TEST(Opcode, BNE_not_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.Z = 1;

    a.org(0x300)
    (BNE, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x302);
}

// BMI tests (Branch if Minus / N set)
TEST(Opcode, BMI_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.N = 1;

    a.org(0x300)
    (BMI, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x312);
}

TEST(Opcode, BMI_not_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.N = 0;

    a.org(0x300)
    (BMI, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x302);
}

// BPL tests (Branch if Plus / N clear)
TEST(Opcode, BPL_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.N = 0;

    a.org(0x300)
    (BPL, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x312);
}

TEST(Opcode, BPL_not_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.N = 1;

    a.org(0x300)
    (BPL, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x302);
}

// BVC tests (Branch if Overflow Clear)
TEST(Opcode, BVC_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.V = 0;

    a.org(0x300)
    (BVC, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x312);
}

TEST(Opcode, BVC_not_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.V = 1;

    a.org(0x300)
    (BVC, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x302);
}

// BVS tests (Branch if Overflow Set)
TEST(Opcode, BVS_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.V = 1;

    a.org(0x300)
    (BVS, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x312);
}

TEST(Opcode, BVS_not_taken) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.flags.V = 0;

    a.org(0x300)
    (BVS, REL, 0x10);

    run_instr(regs, mem);
    EXPECT_EQ(regs.PC, 0x302);
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

// LDX tests
TEST(Opcode, LDX_IMM) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    a.org(0x300)
    (LDX, IMMEDIATE, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0x42);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, LDX_zero_flag) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0xff;
    a.org(0x300)
    (LDX, IMMEDIATE, 0x00);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0x00);
    EXPECT_EQ(regs.flags.Z, 1);
}

TEST(Opcode, LDX_negative_flag) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    a.org(0x300)
    (LDX, IMMEDIATE, 0x80);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0x80);
    EXPECT_EQ(regs.flags.N, 1);
}

TEST(Opcode, LDX_ZPG_Y) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0x10;
    mem[0x52] = 0xab;  // $42 + Y

    a.org(0x300)
    (LDX, ZPG_Y, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(regs.X, 0xab);
}

// LDY tests
TEST(Opcode, LDY_IMM) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    a.org(0x300)
    (LDY, IMMEDIATE, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(regs.Y, 0x42);
    EXPECT_EQ(regs.flags.N, 0);
    EXPECT_EQ(regs.flags.Z, 0);
}

TEST(Opcode, LDY_ZPG_X) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x10;
    mem[0x52] = 0xcd;  // $42 + X

    a.org(0x300)
    (LDY, ZPG_X, 0x42);

    run_instr(regs, mem);

    EXPECT_EQ(regs.Y, 0xcd);
}

// STA tests
TEST(Opcode, STA_ZPG) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x42;
    mem[0x20] = 0x00;

    a.org(0x300)
    (STA, ZPG, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x20], 0x42);
    EXPECT_EQ(regs.PC, 0x302);
}

TEST(Opcode, STA_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x55;

    a.org(0x300)
    (STA, ABS, 0x1234);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x1234], 0x55);
    EXPECT_EQ(regs.PC, 0x303);
}

TEST(Opcode, STA_ABS_X) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0xaa;
    regs.X = 0x10;

    a.org(0x300)
    (STA, ABS_X, 0x1234);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x1244], 0xaa);  // $1234 + X
}

TEST(Opcode, STA_IND_Y) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.A = 0x77;
    regs.Y = 0x10;
    mem[0x20] = 0x00;
    mem[0x21] = 0x12;  // Points to $1200

    a.org(0x300)
    (STA, IND_Y, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x1210], 0x77);  // $1200 + Y
}

// STX tests
TEST(Opcode, STX_ZPG) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0x42;

    a.org(0x300)
    (STX, ZPG, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x20], 0x42);
}

TEST(Opcode, STX_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0xef;

    a.org(0x300)
    (STX, ABS, 0x1234);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x1234], 0xef);
}

TEST(Opcode, STX_ZPG_Y) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.X = 0xbb;
    regs.Y = 0x10;

    a.org(0x300)
    (STX, ZPG_Y, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x30], 0xbb);  // $20 + Y
}

// STY tests
TEST(Opcode, STY_ZPG) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0x42;

    a.org(0x300)
    (STY, ZPG, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x20], 0x42);
}

TEST(Opcode, STY_ABS) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0xdc;

    a.org(0x300)
    (STY, ABS, 0x1234);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x1234], 0xdc);
}

TEST(Opcode, STY_ZPG_X) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    regs.PC = 0x300;
    regs.Y = 0xcc;
    regs.X = 0x10;

    a.org(0x300)
    (STY, ZPG_X, 0x20);

    run_instr(regs, mem);

    EXPECT_EQ(mem[0x30], 0xcc);  // $20 + X
}

// Load/Store roundtrip
TEST(Opcode, LDA_STA_roundtrip) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    mem[0x1000] = 0x42;

    a.org(0x300)
    (LDA, ABS, 0x1000)
    (STA, ABS, 0x2000);

    regs.PC = 0x300;
    run_instr(regs, mem);  // LDA
    run_instr(regs, mem);  // STA

    EXPECT_EQ(mem[0x2000], 0x42);
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

// Helper: run until BRK (opcode $00), with a cycle limit to avoid infinite loops
void run_until_brk(RegisterFile& regs, Memory& mem, int limit = 10000) {
    for (int i = 0; i < limit; i++) {
        if (mem[regs.PC] == 0x00) return;  // BRK opcode
        run_instr(regs, mem);
    }
    FAIL() << "Exceeded instruction limit";
}

// ============================================================
// Integration tests: small 6502 programs
// ============================================================

// 8-bit multiply: result = $10 * $05 = $50 (80)
// Uses the classic shift-and-add algorithm.
//
//   multiplier = $10, multiplicand = $05
//   result (16-bit) in $02:$03
//
// multiply:
//   LDA #$00       ; clear result high byte
//   STA $03
//   LDA #$05       ; multiplicand
//   STA $02        ; result low = multiplicand
//   LDX #$08       ; 8-bit counter
// loop:
//   LSR $03        ; shift multiplier right (use $03 as multiplier)
//   ... actually let me use a simpler layout
//
// Simpler: accumulate result in A using repeated addition.
// multiply $10 * $05 by adding $10 five times.
TEST(Integration, Multiply_by_addition) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Multiply: compute $10 * Y where Y = 5
    // Result in A
    //
    //        LDA #$00      ; result = 0
    //        LDY #$05      ; counter = 5
    // loop:  CLC
    //        ADC #$10      ; result += $10
    //        DEY
    //        BNE loop
    //        BRK
    a.org(0x300)
    (LDA, IMMEDIATE, 0x00)      // $300
    (LDY, IMMEDIATE, 0x05)      // $302
    (CLC, IMPLIED, 0)           // $304
    (ADC, IMMEDIATE, 0x10)      // $305
    (DEY, IMPLIED, 0)           // $307
    (BNE, REL, 0xfa);           // $308 -> back to $304

    // After loop ends, BRK at $30a
    mem[0x30a] = 0x00;  // BRK

    regs.PC = 0x300;
    regs.SP = 0xff;
    run_until_brk(regs, mem);

    EXPECT_EQ(regs.A, 0x50);  // $10 * 5 = $50 (80)
    EXPECT_EQ(regs.Y, 0x00);
}

// 8-bit multiply using shift-and-add
// Computes multiplicand ($20) * multiplier ($21) -> result in $22:$23
TEST(Integration, Multiply_shift_and_add) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Input: 13 * 11 = 143 ($8F)
    mem[0x20] = 13;   // multiplicand
    mem[0x21] = 11;   // multiplier
    mem[0x22] = 0;    // result low
    mem[0x23] = 0;    // result high

    //        LDX #$08       ; 8 bits
    // loop:  LSR $21        ; shift multiplier right, bit 0 -> C
    //        BCC skip       ; if bit was 0, skip addition
    //        CLC
    //        LDA $22        ; result_lo += multiplicand
    //        ADC $20
    //        STA $22
    //        LDA $23        ; result_hi += carry
    //        ADC #$00
    //        STA $23
    // skip:  ASL $20        ; shift multiplicand left
    //        DEX
    //        BNE loop
    //        BRK
    a.org(0x400)
    (LDX, IMMEDIATE, 0x08)     // $400
    (LSR, ZPG, 0x21)           // $402: shift multiplier
    (BCC, REL, 0x0c)           // $404: skip ahead to ASL if C=0 -> $412
    (CLC, IMPLIED, 0)          // $406
    (LDA, ZPG, 0x22)           // $407
    (ADC, ZPG, 0x20)           // $409
    (STA, ZPG, 0x22)           // $40b
    (LDA, ZPG, 0x23)           // $40d
    (ADC, IMMEDIATE, 0x00)     // $40f
    (STA, ZPG, 0x23)           // $411
    (ASL, ZPG, 0x20)           // $413: shift multiplicand (skip target = $412... wait)
    ;

    // The BCC skip target needs to land on the ASL.
    // BCC at $404, instruction length 2, so PC after = $406
    // offset $0c -> $406 + $0c = $412
    // But ASL $20 is at $413. Let me recalculate...
    //
    // $400: LDX #$08  (2 bytes)
    // $402: LSR $21   (2 bytes)
    // $404: BCC +xx   (2 bytes)
    // $406: CLC       (1 byte)
    // $407: LDA $22   (2 bytes)
    // $409: ADC $20   (2 bytes)
    // $40b: STA $22   (2 bytes)
    // $40d: LDA $23   (2 bytes)
    // $40f: ADC #$00  (2 bytes)
    // $411: STA $23   (2 bytes)
    // $413: ASL $20   (2 bytes)
    // $415: DEX       (1 byte)
    // $416: BNE loop  (2 bytes) -> back to $402
    // $418: BRK

    // BCC at $404: target $413, offset = $413 - $406 = $0d

    // Redo with correct offset:
    a.org(0x400)
    (LDX, IMMEDIATE, 0x08)     // $400
    (LSR, ZPG, 0x21)           // $402
    (BCC, REL, 0x0d)           // $404: skip to $413
    (CLC, IMPLIED, 0)          // $406
    (LDA, ZPG, 0x22)           // $407
    (ADC, ZPG, 0x20)           // $409
    (STA, ZPG, 0x22)           // $40b
    (LDA, ZPG, 0x23)           // $40d
    (ADC, IMMEDIATE, 0x00)     // $40f
    (STA, ZPG, 0x23)           // $411
    (ASL, ZPG, 0x20)           // $413
    (DEX, IMPLIED, 0)          // $415
    (BNE, REL, 0xea);          // $416: back to $402 (offset = $402 - $418 = -22 = $ea)

    mem[0x418] = 0x00;  // BRK

    regs.PC = 0x400;
    regs.SP = 0xff;
    run_until_brk(regs, mem);

    uint16_t result = mem[0x22] | (mem[0x23] << 8);
    EXPECT_EQ(result, 143);  // 13 * 11
}

// Fibonacci: compute first 10 fibonacci numbers into memory
// fib(0)=1, fib(1)=1, fib(2)=2, ... fib(9)=55
TEST(Integration, Fibonacci) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Store fib sequence at $40-$49
    // $40 = 1, $41 = 1, then each = sum of prior two
    //
    //        LDA #$01
    //        STA $40         ; fib[0] = 1
    //        STA $41         ; fib[1] = 1
    //        LDX #$02        ; i = 2
    // loop:  LDA $3e,X       ; fib[i-2]
    //        CLC
    //        ADC $3f,X       ; + fib[i-1]
    //        STA $40,X       ; fib[i] = sum
    //        INX
    //        CPX #$0a        ; i < 10?
    //        BNE loop
    //        BRK
    a.org(0x500)
    (LDA, IMMEDIATE, 0x01)     // $500
    (STA, ZPG, 0x40)           // $502
    (STA, ZPG, 0x41)           // $504
    (LDX, IMMEDIATE, 0x02)     // $506
    (LDA, ZPG_X, 0x3e)        // $508: fib[i-2]
    (CLC, IMPLIED, 0)          // $50a
    (ADC, ZPG_X, 0x3f)        // $50b: + fib[i-1]
    (STA, ZPG_X, 0x40)        // $50d: store fib[i]
    (INX, IMPLIED, 0)          // $50f
    (CPX, IMMEDIATE, 0x0a)     // $510
    (BNE, REL, 0xf6);          // $512: back to $50a... wait

    // $508: LDA $3e,X (2 bytes)
    // $50a: CLC       (1 byte)
    // $50b: ADC $3f,X (2 bytes)
    // $50d: STA $40,X (2 bytes)
    // $50f: INX       (1 byte)
    // $510: CPX #$0a  (2 bytes)
    // $512: BNE xx    (2 bytes) -> target $508
    // $514: BRK
    //
    // BNE at $512: target $508, offset = $508 - $514 = -12 = $f4

    a.org(0x500)
    (LDA, IMMEDIATE, 0x01)
    (STA, ZPG, 0x40)
    (STA, ZPG, 0x41)
    (LDX, IMMEDIATE, 0x02)
    (LDA, ZPG_X, 0x3e)
    (CLC, IMPLIED, 0)
    (ADC, ZPG_X, 0x3f)
    (STA, ZPG_X, 0x40)
    (INX, IMPLIED, 0)
    (CPX, IMMEDIATE, 0x0a)
    (BNE, REL, 0xf4);

    mem[0x514] = 0x00;  // BRK

    regs.PC = 0x500;
    regs.SP = 0xff;
    run_until_brk(regs, mem);

    // Fibonacci: 1 1 2 3 5 8 13 21 34 55
    EXPECT_EQ(mem[0x40], 1);
    EXPECT_EQ(mem[0x41], 1);
    EXPECT_EQ(mem[0x42], 2);
    EXPECT_EQ(mem[0x43], 3);
    EXPECT_EQ(mem[0x44], 5);
    EXPECT_EQ(mem[0x45], 8);
    EXPECT_EQ(mem[0x46], 13);
    EXPECT_EQ(mem[0x47], 21);
    EXPECT_EQ(mem[0x48], 34);
    EXPECT_EQ(mem[0x49], 55);
}

// Memory copy: copy 16 bytes from $80 to $C0 using indexed addressing
TEST(Integration, Memcpy) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Fill source block with test pattern
    for (int i = 0; i < 16; i++) {
        mem[0x80 + i] = (i * 7 + 3) & 0xff;
    }

    //        LDY #$00
    // loop:  LDA $80,Y
    //        STA $c0,Y
    //        INY
    //        CPY #$10
    //        BNE loop
    //        BRK
    a.org(0x600)
    (LDY, IMMEDIATE, 0x00)     // $600
    (LDA, ABS_Y, 0x0080)       // $602
    (STA, ABS_Y, 0x00c0)       // $605
    (INY, IMPLIED, 0)           // $608
    (CPY, IMMEDIATE, 0x10)      // $609
    (BNE, REL, 0xf7);          // $60b: back to $602 (offset = $602 - $60d = -11 = $f5)

    // Actually let me recalculate:
    // $600: LDY #$00  (2 bytes)
    // $602: LDA $0080,Y (3 bytes)
    // $605: STA $00c0,Y (3 bytes)
    // $608: INY        (1 byte)
    // $609: CPY #$10   (2 bytes)
    // $60b: BNE xx     (2 bytes) -> target $602
    // $60d: BRK
    // offset = $602 - $60d = -11 = $f5

    a.org(0x600)
    (LDY, IMMEDIATE, 0x00)
    (LDA, ABS_Y, 0x0080)
    (STA, ABS_Y, 0x00c0)
    (INY, IMPLIED, 0)
    (CPY, IMMEDIATE, 0x10)
    (BNE, REL, 0xf5);

    mem[0x60d] = 0x00;  // BRK

    regs.PC = 0x600;
    regs.SP = 0xff;
    run_until_brk(regs, mem);

    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(mem[0xc0 + i], mem[0x80 + i])
            << "Mismatch at offset " << i;
    }
}

// Subroutine call: JSR to a helper that doubles A, then returns
TEST(Integration, Subroutine_double) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Main:
    //        LDA #$15       ; A = 21
    //        JSR double     ; call subroutine
    //        STA $40        ; store result
    //        BRK
    //
    // double:
    //        ASL A          ; A = A * 2
    //        RTS
    a.org(0x700)
    (LDA, IMMEDIATE, 0x15)     // $700
    (JSR, ABS, 0x0708)         // $702
    (STA, ZPG, 0x40)           // $705
    ;
    mem[0x707] = 0x00;         // BRK

    a.org(0x708)
    (ASL, ACCUMULATOR, 0)      // $708: double subroutine
    (RTS, IMPLIED, 0);         // $709

    regs.PC = 0x700;
    regs.SP = 0xff;
    run_until_brk(regs, mem);

    EXPECT_EQ(mem[0x40], 42);  // 21 * 2
}

// Bubble sort: sort 5 bytes in memory
TEST(Integration, Bubble_sort) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);

    // Data at $50-$54: unsorted
    mem[0x50] = 5;
    mem[0x51] = 3;
    mem[0x52] = 4;
    mem[0x53] = 1;
    mem[0x54] = 2;

    // Bubble sort (ascending), 5 elements at $50
    // outer: LDX #$04       ; passes = n-1
    // oloop: LDY #$00       ; inner index
    //        CLC             ; clear swap flag (carry = no swap)
    //        SEC
    // iloop: LDA $50,Y      ; load element
    //        CMP $51,Y      ; compare with next
    //        BCC noswap     ; if A < next, no swap needed (unsigned)
    //        BEQ noswap     ; if A == next, no swap
    //        ; swap
    //        PHA             ; save A (= $50+Y)
    //        LDA $51,Y      ; load next element
    //        STA $50,Y      ; store in current position
    //        PLA             ; restore original
    //        STA $51,Y      ; store in next position
    // noswap:
    //        INY
    //        CPY #$04       ; compare against n-1
    //        BNE iloop
    //        DEX
    //        BNE oloop
    //        BRK

    // Let me lay this out carefully:
    // $800: LDX #$04        (2)
    // $802: LDY #$00        (2)  <- oloop
    // $804: LDA $50,Y       (2)  <- iloop
    // $806: CMP $51,Y       (2)
    // $808: BCC noswap      (2)  -> $814
    // $80a: BEQ noswap      (2)  -> $814
    // $80c: PHA              (1)
    // $80d: LDA $51,Y       (2)
    // $80f: STA $50,Y       (2)
    // $811: PLA              (1)
    // $812: STA $51,Y       (2)
    // $814: INY              (1)  <- noswap
    // $815: CPY #$04        (2)
    // $817: BNE iloop       (2)  -> $804
    // $819: DEX              (1)
    // $81a: BNE oloop       (2)  -> $802
    // $81c: BRK

    // BCC at $808: target $814, offset = $814 - $80a = $0a
    // BEQ at $80a: target $814, offset = $814 - $80c = $08
    // BNE at $817: target $804, offset = $804 - $819 = -21 = $eb
    // BNE at $81a: target $802, offset = $802 - $81c = -26 = $e6

    a.org(0x800)
    (LDX, IMMEDIATE, 0x04)
    (LDY, IMMEDIATE, 0x00)
    (LDA, ZPG_X, 0x50)        // Oops, need ZPG_Y not ZPG_X
    ;

    // Actually, LDA $50,Y and CMP $51,Y need the ZPG_X mode but with Y...
    // Wait: ZPG,Y only exists for LDX and STX. For LDA we need ABS_Y.
    // LDA zpg,X works but not LDA zpg,Y.
    // Let me use ABS_Y: LDA $0050,Y and CMP $0051,Y

    a.org(0x800)
    (LDX, IMMEDIATE, 0x04)     // $800 (2)
    (LDY, IMMEDIATE, 0x00)     // $802 (2) <- oloop
    (LDA, ABS_Y, 0x0050)       // $804 (3) <- iloop
    (CMP, ABS_Y, 0x0051)       // $807 (3)
    (BCC, REL, 0x09)           // $80a (2) -> noswap
    (BEQ, REL, 0x07)           // $80c (2) -> noswap
    (PHA, IMPLIED, 0)          // $80e (1)
    (LDA, ABS_Y, 0x0051)       // $80f (3)
    (STA, ABS_Y, 0x0050)       // $812 (3)
    (PLA, IMPLIED, 0)          // $815 (1)
    (STA, ABS_Y, 0x0051)       // $816 (3)
    (INY, IMPLIED, 0)          // $819 (1) <- noswap
    (CPY, IMMEDIATE, 0x04)     // $81a (2)
    (BNE, REL, 0xe8)           // $81c (2) -> iloop at $804... wait
    (DEX, IMPLIED, 0)          // $81e (1)
    (BNE, REL, 0xe2);          // $81f (2) -> oloop at $802

    // BCC at $80a: target $819, offset = $819 - $80c = $0d
    // BEQ at $80c: target $819, offset = $819 - $80e = $0b
    // BNE at $81c: target $804, offset = $804 - $81e = -26 = $e6
    // BNE at $81f: target $802, offset = $802 - $821 = -31 = $e1

    // Let me recalculate everything carefully and redo:
    a.org(0x800)
    (LDX, IMMEDIATE, 0x04)     // $800: 2 bytes
    (LDY, IMMEDIATE, 0x00)     // $802: 2 bytes  <- oloop
    (LDA, ABS_Y, 0x0050)       // $804: 3 bytes  <- iloop
    (CMP, ABS_Y, 0x0051)       // $807: 3 bytes
    (BCC, REL, 0x0d)           // $80a: 2 bytes  -> $819
    (BEQ, REL, 0x0b)           // $80c: 2 bytes  -> $819
    (PHA, IMPLIED, 0)          // $80e: 1 byte
    (LDA, ABS_Y, 0x0051)       // $80f: 3 bytes
    (STA, ABS_Y, 0x0050)       // $812: 3 bytes
    (PLA, IMPLIED, 0)          // $815: 1 byte
    (STA, ABS_Y, 0x0051)       // $816: 3 bytes
    (INY, IMPLIED, 0)          // $819: 1 byte   <- noswap
    (CPY, IMMEDIATE, 0x04)     // $81a: 2 bytes
    (BNE, REL, 0xe8)           // $81c: 2 bytes  -> $806... no
    (DEX, IMPLIED, 0)          // $81e: 1 byte
    (BNE, REL, 0xe2);          // $81f: 2 bytes  -> $803... no

    // OK let me be really precise:
    // BNE at $81c: PC after = $81e, target = $804, offset = $804 - $81e = -(0x1a) = -26
    //   -26 as uint8 = 256-26 = 230 = $e6
    // BNE at $81f: PC after = $821, target = $802, offset = $802 - $821 = -(0x1f) = -31
    //   -31 as uint8 = 256-31 = 225 = $e1

    a.org(0x800)
    (LDX, IMMEDIATE, 0x04)
    (LDY, IMMEDIATE, 0x00)
    (LDA, ABS_Y, 0x0050)
    (CMP, ABS_Y, 0x0051)
    (BCC, REL, 0x0d)
    (BEQ, REL, 0x0b)
    (PHA, IMPLIED, 0)
    (LDA, ABS_Y, 0x0051)
    (STA, ABS_Y, 0x0050)
    (PLA, IMPLIED, 0)
    (STA, ABS_Y, 0x0051)
    (INY, IMPLIED, 0)
    (CPY, IMMEDIATE, 0x04)
    (BNE, REL, 0xe6)
    (DEX, IMPLIED, 0)
    (BNE, REL, 0xe1);

    mem[0x821] = 0x00;  // BRK

    regs.PC = 0x800;
    regs.SP = 0xff;
    run_until_brk(regs, mem);

    EXPECT_EQ(mem[0x50], 1);
    EXPECT_EQ(mem[0x51], 2);
    EXPECT_EQ(mem[0x52], 3);
    EXPECT_EQ(mem[0x53], 4);
    EXPECT_EQ(mem[0x54], 5);
}

// ============================================================
// Classic 6502 subroutine from "Roots of the Atari" (Atari Archives)
// 16-bit dividend / 8-bit divisor -> 8-bit quotient + remainder
// Source: https://www.atariarchives.org/roots/chapter_10.php
//
// Variables (zero page):
//   DVDL = $C0  (low byte of dividend)
//   DVDH = $C1  (high byte of dividend)
//   QUOT = $C2  (quotient result)
//   DIVS = $C3  (divisor)
//   RMDR = $C4  (remainder result)
// ============================================================

void setup_division(Memory& mem, Assembler& a) {
    // $900: LDA $C1        (2)
    // $902: LDX #$08       (2)
    // $904: SEC             (1)
    // $905: SBC $C3         (2)
    // DLOOP:
    // $907: PHP             (1)
    // $908: ROL $C2         (2)
    // $90a: ASL $C0         (2)
    // $90c: ROL A           (1)
    // $90d: PLP             (1)
    // $90e: BCC ADDIT       (2) -> $915
    // $910: SBC $C3         (2)
    // $912: JMP NEXT        (3) -> $917
    // ADDIT:
    // $915: ADC $C3         (2)
    // NEXT:
    // $917: DEX             (1)
    // $918: BNE DLOOP       (2) -> $907
    // $91a: BCS FINI        (2) -> $91f
    // $91c: ADC $C3         (2)
    // $91e: CLC             (1)
    // FINI:
    // $91f: ROL $C2         (2)
    // $921: STA $C4         (2)
    // $923: RTS             (1)

    a.org(0x900)
    (LDA, ZPG, 0xc1)           // LDA DVDH
    (LDX, IMMEDIATE, 0x08)     // LDX #8
    (SEC, IMPLIED, 0)          // SEC
    (SBC, ZPG, 0xc3)           // SBC DIVS
    // DLOOP:
    (PHP, IMPLIED, 0)          // PHP
    (ROL, ZPG, 0xc2)           // ROL QUOT
    (ASL, ZPG, 0xc0)           // ASL DVDL
    (ROL, ACCUMULATOR, 0)      // ROL A
    (PLP, IMPLIED, 0)          // PLP
    (BCC, REL, 0x05)           // BCC ADDIT -> $915
    (SBC, ZPG, 0xc3)           // SBC DIVS
    (JMP, ABS, 0x0917)         // JMP NEXT
    // ADDIT:
    (ADC, ZPG, 0xc3)           // ADC DIVS
    // NEXT:
    (DEX, IMPLIED, 0)          // DEX
    (BNE, REL, 0xed)           // BNE DLOOP -> $907
    (BCS, REL, 0x03)           // BCS FINI -> $91f
    (ADC, ZPG, 0xc3)           // ADC DIVS
    (CLC, IMPLIED, 0)          // CLC
    // FINI:
    (ROL, ZPG, 0xc2)           // ROL QUOT
    (STA, ZPG, 0xc4)           // STA RMDR
    (RTS, IMPLIED, 0);         // RTS
}

void run_division(RegisterFile& regs, Memory& mem,
                  uint16_t dividend, uint8_t divisor,
                  uint8_t& quotient, uint8_t& remainder) {
    mem[0xc0] = dividend & 0xff;        // DVDL
    mem[0xc1] = (dividend >> 8) & 0xff; // DVDH
    mem[0xc2] = 0;                      // QUOT
    mem[0xc3] = divisor;                // DIVS
    mem[0xc4] = 0;                      // RMDR

    // JSR to the routine at $900, followed by BRK
    mem[0x800] = 0x20;  // JSR
    mem[0x801] = 0x00;  // low byte of $0900
    mem[0x802] = 0x09;  // high byte of $0900
    mem[0x803] = 0x00;  // BRK

    regs.PC = 0x800;
    regs.SP = 0xff;
    run_until_brk(regs, mem);

    quotient = mem[0xc2];
    remainder = mem[0xc4];
}

// Book's example: $021C / $05 = 540 / 5 = 108 remainder 0
TEST(Integration, Division_book_example) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);
    setup_division(mem, a);

    uint8_t quot, rem;
    run_division(regs, mem, 0x021c, 0x05, quot, rem);

    EXPECT_EQ(quot, 108);  // 540 / 5 = 108
    EXPECT_EQ(rem, 0);
}

// 255 / 16 = 15 remainder 15
TEST(Integration, Division_with_remainder) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);
    setup_division(mem, a);

    uint8_t quot, rem;
    run_division(regs, mem, 0x00ff, 0x10, quot, rem);

    EXPECT_EQ(quot, 15);   // 255 / 16 = 15
    EXPECT_EQ(rem, 15);    // remainder 15
}

// 100 / 10 = 10 remainder 0
TEST(Integration, Division_100_by_10) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);
    setup_division(mem, a);

    uint8_t quot, rem;
    run_division(regs, mem, 0x0064, 0x0a, quot, rem);

    EXPECT_EQ(quot, 10);
    EXPECT_EQ(rem, 0);
}

// 1000 / 7 = 142 remainder 6
TEST(Integration, Division_1000_by_7) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);
    setup_division(mem, a);

    uint8_t quot, rem;
    run_division(regs, mem, 1000, 7, quot, rem);

    EXPECT_EQ(quot, 142);  // 1000 / 7 = 142
    EXPECT_EQ(rem, 6);     // 1000 - 142*7 = 6
}

// 0 / 1 = 0 remainder 0
TEST(Integration, Division_zero) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);
    setup_division(mem, a);

    uint8_t quot, rem;
    run_division(regs, mem, 0, 1, quot, rem);

    EXPECT_EQ(quot, 0);
    EXPECT_EQ(rem, 0);
}

// 255 / 1 = 255 remainder 0
TEST(Integration, Division_by_one) {
    RegisterFile regs;
    Memory mem;
    Assembler a(mem);
    setup_division(mem, a);

    uint8_t quot, rem;
    run_division(regs, mem, 255, 1, quot, rem);

    EXPECT_EQ(quot, 255);
    EXPECT_EQ(rem, 0);
}

}
