#include <cassert>

#include "6502.hpp"
#include "assembler.hpp"

extern size_t
assemble_instr(Mnemonic mnem, AddressingMode mode, uint16_t dir16, uint8_t* destination) {
    auto len = addressing_mode_to_length(mode);
#define OPCODE(_mnem, _byte, _mode) \
        if (mnem == _mnem) { \
            *destination++ = _byte; \
            if (len > 1) { \
                *destination++ = dir16 & 0xff; \
                if (len == 3) { \
                    *destination++ = (dir16 & 0xff00) >> 8; \
                } \
            } \
            return len; \
        }
OPCODES()
#undef OPCODE
    NOT_REACHED();
    return size_t(-1);
}


void
Assembler::Label::resolve(MemLoc loc) {
    if (this->state == RESOLVED) assert(location == location);
    state = Label::RESOLVED;
    location = loc;
}

