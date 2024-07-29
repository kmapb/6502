#include "6502.hpp"
#include <cassert>

void test_brk() {
    RegisterFile regs;
    Memory mem;

    mem[0] = OPCODE_BRK_IMPLIED;
    mem[1] = 0xff;
    regs.SP = 0xf8;

    run_instr(regs, mem);
    assert(regs.SP == 0xf6);
}

void test_instrs() {
}

int main(int argc, char** argv) {
    printf("woot\n");
    test_brk();
    return 0;
}
