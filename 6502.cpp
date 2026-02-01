#include "6502.hpp"
#include <cstring>


uint8_t&
Memory::operator[](uint16_t addr) {
    return bytes[addr];
}

const uint8_t&
Memory::operator[](uint16_t addr) const {
    return bytes[addr];
}

void
Memory::reset() {
    memset(bytes, 0, sizeof(bytes));
}

uint16_t
Memory::read16(uint16_t addr) const {
    auto ll = bytes[addr];
    auto hh = bytes[(addr + 1) & 0xffff];
    return (hh << 8) | ll;
}

void
Memory::write16(uint16_t addr, uint16_t val) {
    auto ll = val & 0xff;
    auto hh = (val & 0xff00) >> 8;
    bytes[addr] = ll;
    bytes[(addr + 1) & 0xffff] = hh;
}

uint8_t
addressing_mode_to_length(AddressingMode mode) {
    switch (mode) {
#define ADDRESSING_MODE(mode, len) case (mode): return len;
        ADDRESSING_MODES()
#undef ADDRESSING_MODE
    }
    NOT_REACHED();
    return 0;
}

static inline uint16_t
operand(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    switch(mode) {
    case ACCUMULATOR: return regs.A;
    case ABS: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return mem.bytes[ll + (hh << 8)];
    }
    case ABS_X: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return mem.bytes[regs.X + ll + (hh << 8)];
    }
    case ABS_Y: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return mem.bytes[regs.Y + ll + (hh << 8)];
    }
    case IMMEDIATE: {
        return mem.bytes[regs.PC + 1];
    }
    case IMPLIED: {
        return 0; // Operand not used for this opcode
    }
    case INDIRECT: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return mem.bytes[ll + (hh << 8)];
    }
    case X_IND: {
        auto addr_low = mem.bytes[regs.PC + 1];
        auto ll = mem.bytes[(addr_low + regs.X) & 0xff];
        auto hh = mem.bytes[(addr_low + regs.X + 1) & 0xff];
        return mem.bytes[ll + (hh << 8)];
    }
    case IND_Y: {
        auto zp_addr = mem.bytes[regs.PC + 1];
        auto ll = mem.bytes[zp_addr];
        auto hh = mem.bytes[(zp_addr + 1) & 0xff];
        return mem.bytes[(ll + (hh << 8) + regs.Y) & 0xffff];
    }
    case REL: {
        int8_t disp = int8_t(mem.bytes[regs.PC + 1]);
        return regs.PC + disp;
    }
    case ZPG: {
        uint8_t addr = mem.bytes[regs.PC + 1];
        return mem.bytes[addr];
    }
    case ZPG_X: {
        uint8_t addr = mem.bytes[regs.PC + 1] + regs.X;
        return mem.bytes[addr];
    }
    case ZPG_Y: {
        uint8_t addr = mem.bytes[regs.PC + 1] + regs.Y;
        return mem.bytes[addr];
    }
    default: return 0;
    }
}

// Returns the effective address for memory-based addressing modes
// For ACCUMULATOR/IMMEDIATE/IMPLIED, returns 0 (not meaningful)
static inline uint16_t
effective_address(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    switch(mode) {
    case ABS: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return ll + (hh << 8);
    }
    case ABS_X: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return (regs.X + ll + (hh << 8)) & 0xffff;
    }
    case ABS_Y: {
        auto ll = mem.bytes[regs.PC + 1];
        auto hh = mem.bytes[regs.PC + 2];
        return (regs.Y + ll + (hh << 8)) & 0xffff;
    }
    case X_IND: {
        auto addr_low = mem.bytes[regs.PC + 1];
        auto ll = mem.bytes[(addr_low + regs.X) & 0xff];
        auto hh = mem.bytes[(addr_low + regs.X + 1) & 0xff];
        return ll + (hh << 8);
    }
    case IND_Y: {
        auto zp_addr = mem.bytes[regs.PC + 1];
        auto ll = mem.bytes[zp_addr];
        auto hh = mem.bytes[(zp_addr + 1) & 0xff];
        return (ll + (hh << 8) + regs.Y) & 0xffff;
    }
    case ZPG: {
        return mem.bytes[regs.PC + 1];
    }
    case ZPG_X: {
        return (mem.bytes[regs.PC + 1] + regs.X) & 0xff;
    }
    case ZPG_Y: {
        return (mem.bytes[regs.PC + 1] + regs.Y) & 0xff;
    }
    default: return 0;
    }
}

uint8_t pop8(RegisterFile& regs, const Memory& mem) {
    ++regs.SP;
    return mem[0x100 | regs.SP];
}

uint16_t pop16(RegisterFile& regs, const Memory& mem) {
    uint8_t ll = pop8(regs, mem);
    uint8_t hh = pop8(regs, mem);
    return ll | (hh << 8);
}

void push8(RegisterFile& regs, Memory& mem, uint8_t val) {
    mem[0x100 | regs.SP] = val;
    --regs.SP;
}

void push16(RegisterFile& regs, Memory& mem, uint16_t val) {
    push8(regs, mem, (val >> 8) & 0xff);  // High byte first
    push8(regs, mem, val & 0xff);          // Low byte second
}

void push_status(RegisterFile& regs, Memory& mem, bool breakp=false) {
    push8(regs, mem, regs.read_flags(breakp));
}

void
set_flags(RegisterFile& regs, uint8_t oldA, uint8_t newA, uint8_t mask) {
    if (mask & flags::N) regs.flags.N = (newA >> 7);
    if (mask & flags::Z) regs.flags.Z = (newA == 0);
    if (mask & flags::C) regs.flags.C = (oldA >> 7);
}

// Opcode implementations
uint16_t
op_BRK(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    push16(regs, mem, regs.PC + 2); // Excess byte after BRK 
    push_status(regs, mem, true);
    return mem.read16(0xfffe);
};

uint16_t
op_ORA(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.A |= operand(regs, mem, mode);
    return regs.PC + addressing_mode_to_length(mode);
};

uint16_t
op_RTI(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    // Pull status register (B flag and bit 5 are ignored)
    uint8_t status = pop8(regs, mem);
    regs.flags.C = (status >> 0) & 1;
    regs.flags.Z = (status >> 1) & 1;
    regs.flags.I = (status >> 2) & 1;
    regs.flags.D = (status >> 3) & 1;
    // Bit 4 (B) is not a real flag, ignored
    // Bit 5 is always 1, ignored
    regs.flags.V = (status >> 6) & 1;
    regs.flags.N = (status >> 7) & 1;

    // Pull PC (low byte first, then high byte)
    return pop16(regs, mem);
}

uint16_t
op_ASL(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t old_val, new_val;
    if (mode == ACCUMULATOR) {
        old_val = regs.A;
        new_val = uint8_t(old_val << 1);
        regs.A = new_val;
    } else {
        uint16_t addr = effective_address(regs, mem, mode);
        old_val = mem.bytes[addr];
        new_val = uint8_t(old_val << 1);
        mem.bytes[addr] = new_val;
    }
    regs.flags.C = (old_val >> 7) & 1;
    regs.flags.N = (new_val >> 7) & 1;
    regs.flags.Z = (new_val == 0);
    return regs.PC + addressing_mode_to_length(mode);
}

static int instrs_to_flags[] = {
#define MNEMONIC(mnem, flags) \
    flags,
MNEMONICS()
#undef MNEMONIC
};

const Opcode opcodeTable[] = {
 {BRK, 0x00, IMPLIED},

 {ORA, 0x01, X_IND},
 {ORA, 0x05, ZPG},
 {ORA, 0x09, IMMEDIATE},
 {ORA, 0x0d, ABS},
 {ORA, 0x11, IND_Y},
 {ORA, 0x15, ZPG_X},
 {ORA, 0x19, ABS_Y},
 {ORA, 0x1d, ABS_X},

 {ASL, 0x0a, ACCUMULATOR},
 {ASL, 0x06, ZPG},
 {ASL, 0x16, ZPG_X},
 {ASL, 0x0e, ABS},
 {ASL, 0x1e, ABS_X},

 {RTI, 0x40, IMPLIED},
};

const Opcode&
byte_to_opcode(uint8_t byte)
{
    // XXX
    for (const auto &op : opcodeTable)
    {
        if (op.byte == byte)
            return op;
    }
    NOT_REACHED();
}

const Opcode &mnem_addr_to_opcode(Mnemonic mnem, AddressingMode mode)
{
    // XXX
    for (const auto &op : opcodeTable)
    {
        if (op.mnem == mnem && op.mode == mode)
        {
            return op;
        }
    }
    NOT_REACHED();
}

static void
execute_opcode(const Opcode& opcode, RegisterFile& regs, Memory& mem) {
    auto oldA = regs.A;
    switch(opcode.mnem) {
        case BRK:
            regs.PC = op_BRK(regs, mem, opcode.mode);
            break;
        case ORA:
            regs.PC = op_ORA(regs, mem, opcode.mode);
            break;
        case ASL:
            regs.PC = op_ASL(regs, mem, opcode.mode);
            break;
        case RTI:
            regs.PC = op_RTI(regs, mem, opcode.mode);
            break;
        default:
            NOT_REACHED();
    }
    set_flags(regs, oldA, regs.A, instrs_to_flags[opcode.mnem]);
}

void
run_instr(RegisterFile& regs, Memory& mem) {
    execute_opcode(byte_to_opcode(mem[regs.PC]), regs, mem);
}

