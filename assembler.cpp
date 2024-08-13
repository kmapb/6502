#include <cassert>

#include "6502.hpp"
#include "assembler.hpp"

extern size_t
assemble_instr(Mnemonic mnem, AddressingMode mode, uint16_t dir16, uint8_t* destination) {
    auto len = addressing_mode_to_length(mode);
    const Opcode& opcode = mnem_addr_to_opcode(mnem, mode);
    *destination++ = opcode.byte;
    if (len > 1) {
        *destination++ = dir16 & 0xff;
        if (len == 2) {
            *destination++ = (dir16 & 0xff00) >> 8;
        }
    }
    assert(len > 0 && len <= 3);
    return len;
}


void
Assembler::Label::resolve(MemLoc loc) {
    if (this->state == RESOLVED) assert(location == location);
    state = Label::RESOLVED;
    location = loc;
}

