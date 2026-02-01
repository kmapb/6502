#include "6502.hpp"
#include <cstring>


uint8_t
Bus::read(uint16_t addr) const {
    auto* dev = page_device[addr >> 8];
    if (dev)
        return const_cast<Device*>(dev)->read(addr);
    return ram[addr];
}

void
Bus::write(uint16_t addr, uint8_t val) {
    auto* dev = page_device[addr >> 8];
    if (dev) {
        dev->write(addr, val);
        return;
    }
    ram[addr] = val;
}

uint16_t
Bus::read16(uint16_t addr) const {
    auto ll = read(addr);
    auto hh = read((addr + 1) & 0xffff);
    return (hh << 8) | ll;
}

void
Bus::write16(uint16_t addr, uint16_t val) {
    write(addr, val & 0xff);
    write((addr + 1) & 0xffff, (val >> 8) & 0xff);
}

void
Bus::reset() {
    memset(ram, 0, sizeof(ram));
    memset(page_device, 0, sizeof(page_device));
}

void
Bus::map(uint8_t page, Device* dev) {
    page_device[page] = dev;
}

void
Bus::map(uint8_t page_start, uint8_t page_end, Device* dev) {
    for (unsigned p = page_start; p <= page_end; ++p)
        page_device[p] = dev;
}

uint8_t&
Bus::operator[](uint16_t addr) {
    return ram[addr];
}

const uint8_t&
Bus::operator[](uint16_t addr) const {
    return ram[addr];
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
        auto ll = mem.read(regs.PC + 1);
        auto hh = mem.read(regs.PC + 2);
        return mem.read(ll + (hh << 8));
    }
    case ABS_X: {
        auto ll = mem.read(regs.PC + 1);
        auto hh = mem.read(regs.PC + 2);
        return mem.read(regs.X + ll + (hh << 8));
    }
    case ABS_Y: {
        auto ll = mem.read(regs.PC + 1);
        auto hh = mem.read(regs.PC + 2);
        return mem.read(regs.Y + ll + (hh << 8));
    }
    case IMMEDIATE: {
        return mem.read(regs.PC + 1);
    }
    case IMPLIED: {
        return 0; // Operand not used for this opcode
    }
    case INDIRECT: {
        auto ll = mem.read(regs.PC + 1);
        auto hh = mem.read(regs.PC + 2);
        return mem.read(ll + (hh << 8));
    }
    case X_IND: {
        auto addr_low = mem.read(regs.PC + 1);
        auto ll = mem.read((addr_low + regs.X) & 0xff);
        auto hh = mem.read((addr_low + regs.X + 1) & 0xff);
        return mem.read(ll + (hh << 8));
    }
    case IND_Y: {
        auto zp_addr = mem.read(regs.PC + 1);
        auto ll = mem.read(zp_addr);
        auto hh = mem.read((zp_addr + 1) & 0xff);
        return mem.read((ll + (hh << 8) + regs.Y) & 0xffff);
    }
    case REL: {
        int8_t disp = int8_t(mem.read(regs.PC + 1));
        return regs.PC + disp;
    }
    case ZPG: {
        uint8_t addr = mem.read(regs.PC + 1);
        return mem.read(addr);
    }
    case ZPG_X: {
        uint8_t addr = mem.read(regs.PC + 1) + regs.X;
        return mem.read(addr);
    }
    case ZPG_Y: {
        uint8_t addr = mem.read(regs.PC + 1) + regs.Y;
        return mem.read(addr);
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
        auto ll = mem.read(regs.PC + 1);
        auto hh = mem.read(regs.PC + 2);
        return ll + (hh << 8);
    }
    case ABS_X: {
        auto ll = mem.read(regs.PC + 1);
        auto hh = mem.read(regs.PC + 2);
        return (regs.X + ll + (hh << 8)) & 0xffff;
    }
    case ABS_Y: {
        auto ll = mem.read(regs.PC + 1);
        auto hh = mem.read(regs.PC + 2);
        return (regs.Y + ll + (hh << 8)) & 0xffff;
    }
    case X_IND: {
        auto addr_low = mem.read(regs.PC + 1);
        auto ll = mem.read((addr_low + regs.X) & 0xff);
        auto hh = mem.read((addr_low + regs.X + 1) & 0xff);
        return ll + (hh << 8);
    }
    case IND_Y: {
        auto zp_addr = mem.read(regs.PC + 1);
        auto ll = mem.read(zp_addr);
        auto hh = mem.read((zp_addr + 1) & 0xff);
        return (ll + (hh << 8) + regs.Y) & 0xffff;
    }
    case ZPG: {
        return mem.read(regs.PC + 1);
    }
    case ZPG_X: {
        return (mem.read(regs.PC + 1) + regs.X) & 0xff;
    }
    case ZPG_Y: {
        return (mem.read(regs.PC + 1) + regs.Y) & 0xff;
    }
    default: return 0;
    }
}

uint8_t pop8(RegisterFile& regs, Memory& mem) {
    ++regs.SP;
    return mem.read(0x100 | regs.SP);
}

uint16_t pop16(RegisterFile& regs, Memory& mem) {
    uint8_t ll = pop8(regs, mem);
    uint8_t hh = pop8(regs, mem);
    return ll | (hh << 8);
}

void push8(RegisterFile& regs, Memory& mem, uint8_t val) {
    mem.write(0x100 | regs.SP, val);
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
}

uint16_t
op_AND(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.A &= operand(regs, mem, mode);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_EOR(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.A ^= operand(regs, mem, mode);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_ADC(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t m = operand(regs, mem, mode);
    uint8_t a = regs.A;
    uint16_t sum = a + m + regs.flags.C;

    // Overflow: set if sign of result differs from sign of both operands
    // (only happens when adding two positives gives negative or vice versa)
    regs.flags.V = ((a ^ sum) & (m ^ sum) & 0x80) != 0;
    regs.flags.C = (sum > 0xff);
    regs.A = sum & 0xff;
    regs.flags.N = (regs.A >> 7) & 1;
    regs.flags.Z = (regs.A == 0);

    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_SBC(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t m = operand(regs, mem, mode);
    uint8_t a = regs.A;
    // SBC is A - M - (1 - C) = A + ~M + C
    uint16_t diff = a + (m ^ 0xff) + regs.flags.C;

    // Overflow: similar logic to ADC but with inverted M
    uint8_t m_inv = m ^ 0xff;
    regs.flags.V = ((a ^ diff) & (m_inv ^ diff) & 0x80) != 0;
    regs.flags.C = (diff > 0xff);  // Borrow is inverse of carry
    regs.A = diff & 0xff;
    regs.flags.N = (regs.A >> 7) & 1;
    regs.flags.Z = (regs.A == 0);

    return regs.PC + addressing_mode_to_length(mode);
}

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
op_LDA(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.A = operand(regs, mem, mode);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_LDX(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.X = operand(regs, mem, mode);
    regs.flags.N = (regs.X >> 7) & 1;
    regs.flags.Z = (regs.X == 0);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_LDY(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.Y = operand(regs, mem, mode);
    regs.flags.N = (regs.Y >> 7) & 1;
    regs.flags.Z = (regs.Y == 0);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_STA(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint16_t addr = effective_address(regs, mem, mode);
    mem.write(addr, regs.A);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_STX(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint16_t addr = effective_address(regs, mem, mode);
    mem.write(addr, regs.X);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_STY(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint16_t addr = effective_address(regs, mem, mode);
    mem.write(addr, regs.Y);
    return regs.PC + addressing_mode_to_length(mode);
}

// Helper for compare instructions: sets N, Z, C based on reg - M
static inline void
compare(RegisterFile& regs, uint8_t reg, uint8_t m) {
    uint16_t result = reg - m;
    regs.flags.C = (reg >= m);
    regs.flags.Z = (reg == m);
    regs.flags.N = (result >> 7) & 1;
}

uint16_t
op_CMP(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    compare(regs, regs.A, operand(regs, mem, mode));
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_CPX(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    compare(regs, regs.X, operand(regs, mem, mode));
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_CPY(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    compare(regs, regs.Y, operand(regs, mem, mode));
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_PHA(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    push8(regs, mem, regs.A);
    return regs.PC + 1;
}

uint16_t
op_PHP(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    // PHP always pushes with B flag set (like BRK)
    push_status(regs, mem, true);
    return regs.PC + 1;
}

uint16_t
op_PLA(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.A = pop8(regs, mem);
    regs.flags.N = (regs.A >> 7) & 1;
    regs.flags.Z = (regs.A == 0);
    return regs.PC + 1;
}

uint16_t
op_PLP(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    // Same restore logic as RTI
    uint8_t status = pop8(regs, mem);
    regs.flags.C = (status >> 0) & 1;
    regs.flags.Z = (status >> 1) & 1;
    regs.flags.I = (status >> 2) & 1;
    regs.flags.D = (status >> 3) & 1;
    regs.flags.V = (status >> 6) & 1;
    regs.flags.N = (status >> 7) & 1;
    return regs.PC + 1;
}

uint16_t
op_BIT(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t m = operand(regs, mem, mode);
    regs.flags.Z = ((regs.A & m) == 0);
    regs.flags.N = (m >> 7) & 1;
    regs.flags.V = (m >> 6) & 1;
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_NOP(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    return regs.PC + 1;
}

uint16_t
op_TAX(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.X = regs.A;
    regs.flags.N = (regs.X >> 7) & 1;
    regs.flags.Z = (regs.X == 0);
    return regs.PC + 1;
}

uint16_t
op_TAY(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.Y = regs.A;
    regs.flags.N = (regs.Y >> 7) & 1;
    regs.flags.Z = (regs.Y == 0);
    return regs.PC + 1;
}

uint16_t
op_TXA(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.A = regs.X;
    regs.flags.N = (regs.A >> 7) & 1;
    regs.flags.Z = (regs.A == 0);
    return regs.PC + 1;
}

uint16_t
op_TYA(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.A = regs.Y;
    regs.flags.N = (regs.A >> 7) & 1;
    regs.flags.Z = (regs.A == 0);
    return regs.PC + 1;
}

uint16_t
op_TSX(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.X = regs.SP;
    regs.flags.N = (regs.X >> 7) & 1;
    regs.flags.Z = (regs.X == 0);
    return regs.PC + 1;
}

uint16_t
op_TXS(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.SP = regs.X;
    // TXS does NOT affect flags
    return regs.PC + 1;
}

uint16_t
op_CLC(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.flags.C = 0;
    return regs.PC + 1;
}

uint16_t
op_CLD(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.flags.D = 0;
    return regs.PC + 1;
}

uint16_t
op_CLI(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.flags.I = 0;
    return regs.PC + 1;
}

uint16_t
op_CLV(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.flags.V = 0;
    return regs.PC + 1;
}

uint16_t
op_SEC(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.flags.C = 1;
    return regs.PC + 1;
}

uint16_t
op_SED(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.flags.D = 1;
    return regs.PC + 1;
}

uint16_t
op_SEI(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    regs.flags.I = 1;
    return regs.PC + 1;
}

uint16_t
op_INC(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint16_t addr = effective_address(regs, mem, mode);
    uint8_t val = mem.read(addr) + 1;
    mem.write(addr, val);
    regs.flags.N = (val >> 7) & 1;
    regs.flags.Z = (val == 0);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_DEC(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint16_t addr = effective_address(regs, mem, mode);
    uint8_t val = mem.read(addr) - 1;
    mem.write(addr, val);
    regs.flags.N = (val >> 7) & 1;
    regs.flags.Z = (val == 0);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_INX(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t val = ++regs.X;
    regs.flags.N = (val >> 7) & 1;
    regs.flags.Z = (val == 0);
    return regs.PC + 1;
}

uint16_t
op_INY(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t val = ++regs.Y;
    regs.flags.N = (val >> 7) & 1;
    regs.flags.Z = (val == 0);
    return regs.PC + 1;
}

uint16_t
op_DEX(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t val = --regs.X;
    regs.flags.N = (val >> 7) & 1;
    regs.flags.Z = (val == 0);
    return regs.PC + 1;
}

uint16_t
op_DEY(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t val = --regs.Y;
    regs.flags.N = (val >> 7) & 1;
    regs.flags.Z = (val == 0);
    return regs.PC + 1;
}

// Helper for branch instructions: calculates target address
// Offset is signed and relative to PC+2 (address after the branch instruction)
static inline uint16_t
branch_target(RegisterFile& regs, Memory& mem) {
    int8_t offset = int8_t(mem.read(regs.PC + 1));
    return (regs.PC + 2 + offset) & 0xffff;
}

uint16_t
op_BCC(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    if (regs.flags.C == 0) {
        return branch_target(regs, mem);
    }
    return regs.PC + 2;
}

uint16_t
op_BCS(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    if (regs.flags.C == 1) {
        return branch_target(regs, mem);
    }
    return regs.PC + 2;
}

uint16_t
op_BEQ(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    if (regs.flags.Z == 1) {
        return branch_target(regs, mem);
    }
    return regs.PC + 2;
}

uint16_t
op_BMI(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    if (regs.flags.N == 1) {
        return branch_target(regs, mem);
    }
    return regs.PC + 2;
}

uint16_t
op_BNE(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    if (regs.flags.Z == 0) {
        return branch_target(regs, mem);
    }
    return regs.PC + 2;
}

uint16_t
op_BPL(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    if (regs.flags.N == 0) {
        return branch_target(regs, mem);
    }
    return regs.PC + 2;
}

uint16_t
op_BVC(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    if (regs.flags.V == 0) {
        return branch_target(regs, mem);
    }
    return regs.PC + 2;
}

uint16_t
op_BVS(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    if (regs.flags.V == 1) {
        return branch_target(regs, mem);
    }
    return regs.PC + 2;
}

uint16_t
op_JMP(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    if (mode == ABS) {
        auto ll = mem.read(regs.PC + 1);
        auto hh = mem.read(regs.PC + 2);
        return ll | (hh << 8);
    } else if (mode == INDIRECT) {
        auto ptr_ll = mem.read(regs.PC + 1);
        auto ptr_hh = mem.read(regs.PC + 2);
        uint16_t ptr = ptr_ll | (ptr_hh << 8);
        // NMOS 6502 bug: doesn't increment high byte at page boundary
        auto ll = mem.read(ptr);
        auto hh = mem.read((ptr & 0xff00) | ((ptr + 1) & 0x00ff));
        return ll | (hh << 8);
    }
    NOT_REACHED();
    return 0;
}

uint16_t
op_JSR(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    // Push address of last byte of JSR instruction (PC+2)
    // RTS will pull this and add 1
    push16(regs, mem, regs.PC + 2);
    auto ll = mem.read(regs.PC + 1);
    auto hh = mem.read(regs.PC + 2);
    return ll | (hh << 8);
}

uint16_t
op_RTS(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    // Pull return address and add 1
    return pop16(regs, mem) + 1;
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
        old_val = mem.read(addr);
        new_val = uint8_t(old_val << 1);
        mem.write(addr, new_val);
    }
    regs.flags.C = (old_val >> 7) & 1;
    regs.flags.N = (new_val >> 7) & 1;
    regs.flags.Z = (new_val == 0);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_LSR(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t old_val, new_val;
    if (mode == ACCUMULATOR) {
        old_val = regs.A;
        new_val = old_val >> 1;
        regs.A = new_val;
    } else {
        uint16_t addr = effective_address(regs, mem, mode);
        old_val = mem.read(addr);
        new_val = old_val >> 1;
        mem.write(addr, new_val);
    }
    regs.flags.C = old_val & 1;
    regs.flags.N = 0;  // Always 0 after right shift
    regs.flags.Z = (new_val == 0);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_ROL(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t old_val, new_val;
    uint8_t old_carry = regs.flags.C;
    if (mode == ACCUMULATOR) {
        old_val = regs.A;
        new_val = uint8_t((old_val << 1) | old_carry);
        regs.A = new_val;
    } else {
        uint16_t addr = effective_address(regs, mem, mode);
        old_val = mem.read(addr);
        new_val = uint8_t((old_val << 1) | old_carry);
        mem.write(addr, new_val);
    }
    regs.flags.C = (old_val >> 7) & 1;
    regs.flags.N = (new_val >> 7) & 1;
    regs.flags.Z = (new_val == 0);
    return regs.PC + addressing_mode_to_length(mode);
}

uint16_t
op_ROR(RegisterFile& regs, Memory& mem, AddressingMode mode) {
    uint8_t old_val, new_val;
    uint8_t old_carry = regs.flags.C;
    if (mode == ACCUMULATOR) {
        old_val = regs.A;
        new_val = (old_val >> 1) | (old_carry << 7);
        regs.A = new_val;
    } else {
        uint16_t addr = effective_address(regs, mem, mode);
        old_val = mem.read(addr);
        new_val = (old_val >> 1) | (old_carry << 7);
        mem.write(addr, new_val);
    }
    regs.flags.C = old_val & 1;
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

 {AND, 0x21, X_IND},
 {AND, 0x25, ZPG},
 {AND, 0x29, IMMEDIATE},
 {AND, 0x2d, ABS},
 {AND, 0x31, IND_Y},
 {AND, 0x35, ZPG_X},
 {AND, 0x39, ABS_Y},
 {AND, 0x3d, ABS_X},

 {EOR, 0x41, X_IND},
 {EOR, 0x45, ZPG},
 {EOR, 0x49, IMMEDIATE},
 {EOR, 0x4d, ABS},
 {EOR, 0x51, IND_Y},
 {EOR, 0x55, ZPG_X},
 {EOR, 0x59, ABS_Y},
 {EOR, 0x5d, ABS_X},

 {ADC, 0x61, X_IND},
 {ADC, 0x65, ZPG},
 {ADC, 0x69, IMMEDIATE},
 {ADC, 0x6d, ABS},
 {ADC, 0x71, IND_Y},
 {ADC, 0x75, ZPG_X},
 {ADC, 0x79, ABS_Y},
 {ADC, 0x7d, ABS_X},

 {SBC, 0xe1, X_IND},
 {SBC, 0xe5, ZPG},
 {SBC, 0xe9, IMMEDIATE},
 {SBC, 0xed, ABS},
 {SBC, 0xf1, IND_Y},
 {SBC, 0xf5, ZPG_X},
 {SBC, 0xf9, ABS_Y},
 {SBC, 0xfd, ABS_X},

 {ASL, 0x0a, ACCUMULATOR},
 {ASL, 0x06, ZPG},
 {ASL, 0x16, ZPG_X},
 {ASL, 0x0e, ABS},
 {ASL, 0x1e, ABS_X},

 {LSR, 0x4a, ACCUMULATOR},
 {LSR, 0x46, ZPG},
 {LSR, 0x56, ZPG_X},
 {LSR, 0x4e, ABS},
 {LSR, 0x5e, ABS_X},

 {ROL, 0x2a, ACCUMULATOR},
 {ROL, 0x26, ZPG},
 {ROL, 0x36, ZPG_X},
 {ROL, 0x2e, ABS},
 {ROL, 0x3e, ABS_X},

 {ROR, 0x6a, ACCUMULATOR},
 {ROR, 0x66, ZPG},
 {ROR, 0x76, ZPG_X},
 {ROR, 0x6e, ABS},
 {ROR, 0x7e, ABS_X},

 {RTI, 0x40, IMPLIED},

 {JMP, 0x4c, ABS},
 {JMP, 0x6c, INDIRECT},

 {JSR, 0x20, ABS},

 {RTS, 0x60, IMPLIED},

 {LDA, 0xa1, X_IND},
 {LDA, 0xa5, ZPG},
 {LDA, 0xa9, IMMEDIATE},
 {LDA, 0xad, ABS},
 {LDA, 0xb1, IND_Y},
 {LDA, 0xb5, ZPG_X},
 {LDA, 0xb9, ABS_Y},
 {LDA, 0xbd, ABS_X},

 {LDX, 0xa2, IMMEDIATE},
 {LDX, 0xa6, ZPG},
 {LDX, 0xb6, ZPG_Y},
 {LDX, 0xae, ABS},
 {LDX, 0xbe, ABS_Y},

 {LDY, 0xa0, IMMEDIATE},
 {LDY, 0xa4, ZPG},
 {LDY, 0xb4, ZPG_X},
 {LDY, 0xac, ABS},
 {LDY, 0xbc, ABS_X},

 {STA, 0x81, X_IND},
 {STA, 0x85, ZPG},
 {STA, 0x8d, ABS},
 {STA, 0x91, IND_Y},
 {STA, 0x95, ZPG_X},
 {STA, 0x99, ABS_Y},
 {STA, 0x9d, ABS_X},

 {STX, 0x86, ZPG},
 {STX, 0x96, ZPG_Y},
 {STX, 0x8e, ABS},

 {STY, 0x84, ZPG},
 {STY, 0x94, ZPG_X},
 {STY, 0x8c, ABS},

 {BCC, 0x90, REL},
 {BCS, 0xb0, REL},
 {BEQ, 0xf0, REL},
 {BMI, 0x30, REL},
 {BNE, 0xd0, REL},
 {BPL, 0x10, REL},
 {BVC, 0x50, REL},
 {BVS, 0x70, REL},

 {INC, 0xe6, ZPG},
 {INC, 0xf6, ZPG_X},
 {INC, 0xee, ABS},
 {INC, 0xfe, ABS_X},

 {DEC, 0xc6, ZPG},
 {DEC, 0xd6, ZPG_X},
 {DEC, 0xce, ABS},
 {DEC, 0xde, ABS_X},

 {INX, 0xe8, IMPLIED},
 {INY, 0xc8, IMPLIED},
 {DEX, 0xca, IMPLIED},
 {DEY, 0x88, IMPLIED},

 {CLC, 0x18, IMPLIED},
 {CLD, 0xd8, IMPLIED},
 {CLI, 0x58, IMPLIED},
 {CLV, 0xb8, IMPLIED},
 {SEC, 0x38, IMPLIED},
 {SED, 0xf8, IMPLIED},
 {SEI, 0x78, IMPLIED},

 {TAX, 0xaa, IMPLIED},
 {TAY, 0xa8, IMPLIED},
 {TXA, 0x8a, IMPLIED},
 {TYA, 0x98, IMPLIED},
 {TSX, 0xba, IMPLIED},
 {TXS, 0x9a, IMPLIED},

 {CMP, 0xc1, X_IND},
 {CMP, 0xc5, ZPG},
 {CMP, 0xc9, IMMEDIATE},
 {CMP, 0xcd, ABS},
 {CMP, 0xd1, IND_Y},
 {CMP, 0xd5, ZPG_X},
 {CMP, 0xd9, ABS_Y},
 {CMP, 0xdd, ABS_X},

 {CPX, 0xe0, IMMEDIATE},
 {CPX, 0xe4, ZPG},
 {CPX, 0xec, ABS},

 {CPY, 0xc0, IMMEDIATE},
 {CPY, 0xc4, ZPG},
 {CPY, 0xcc, ABS},

 {PHA, 0x48, IMPLIED},
 {PHP, 0x08, IMPLIED},
 {PLA, 0x68, IMPLIED},
 {PLP, 0x28, IMPLIED},

 {BIT, 0x24, ZPG},
 {BIT, 0x2c, ABS},

 {NOP, 0xea, IMPLIED},
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
        case AND:
            regs.PC = op_AND(regs, mem, opcode.mode);
            break;
        case EOR:
            regs.PC = op_EOR(regs, mem, opcode.mode);
            break;
        case ADC:
            regs.PC = op_ADC(regs, mem, opcode.mode);
            break;
        case SBC:
            regs.PC = op_SBC(regs, mem, opcode.mode);
            break;
        case ASL:
            regs.PC = op_ASL(regs, mem, opcode.mode);
            break;
        case LSR:
            regs.PC = op_LSR(regs, mem, opcode.mode);
            break;
        case ROL:
            regs.PC = op_ROL(regs, mem, opcode.mode);
            break;
        case ROR:
            regs.PC = op_ROR(regs, mem, opcode.mode);
            break;
        case RTI:
            regs.PC = op_RTI(regs, mem, opcode.mode);
            break;
        case JMP:
            regs.PC = op_JMP(regs, mem, opcode.mode);
            break;
        case JSR:
            regs.PC = op_JSR(regs, mem, opcode.mode);
            break;
        case RTS:
            regs.PC = op_RTS(regs, mem, opcode.mode);
            break;
        case LDA:
            regs.PC = op_LDA(regs, mem, opcode.mode);
            break;
        case LDX:
            regs.PC = op_LDX(regs, mem, opcode.mode);
            break;
        case LDY:
            regs.PC = op_LDY(regs, mem, opcode.mode);
            break;
        case STA:
            regs.PC = op_STA(regs, mem, opcode.mode);
            break;
        case STX:
            regs.PC = op_STX(regs, mem, opcode.mode);
            break;
        case STY:
            regs.PC = op_STY(regs, mem, opcode.mode);
            break;
        case BCC:
            regs.PC = op_BCC(regs, mem, opcode.mode);
            break;
        case BCS:
            regs.PC = op_BCS(regs, mem, opcode.mode);
            break;
        case BEQ:
            regs.PC = op_BEQ(regs, mem, opcode.mode);
            break;
        case BMI:
            regs.PC = op_BMI(regs, mem, opcode.mode);
            break;
        case BNE:
            regs.PC = op_BNE(regs, mem, opcode.mode);
            break;
        case BPL:
            regs.PC = op_BPL(regs, mem, opcode.mode);
            break;
        case BVC:
            regs.PC = op_BVC(regs, mem, opcode.mode);
            break;
        case BVS:
            regs.PC = op_BVS(regs, mem, opcode.mode);
            break;
        case INC:
            regs.PC = op_INC(regs, mem, opcode.mode);
            break;
        case DEC:
            regs.PC = op_DEC(regs, mem, opcode.mode);
            break;
        case INX:
            regs.PC = op_INX(regs, mem, opcode.mode);
            break;
        case INY:
            regs.PC = op_INY(regs, mem, opcode.mode);
            break;
        case DEX:
            regs.PC = op_DEX(regs, mem, opcode.mode);
            break;
        case DEY:
            regs.PC = op_DEY(regs, mem, opcode.mode);
            break;
        case CLC:
            regs.PC = op_CLC(regs, mem, opcode.mode);
            break;
        case CLD:
            regs.PC = op_CLD(regs, mem, opcode.mode);
            break;
        case CLI:
            regs.PC = op_CLI(regs, mem, opcode.mode);
            break;
        case CLV:
            regs.PC = op_CLV(regs, mem, opcode.mode);
            break;
        case SEC:
            regs.PC = op_SEC(regs, mem, opcode.mode);
            break;
        case SED:
            regs.PC = op_SED(regs, mem, opcode.mode);
            break;
        case SEI:
            regs.PC = op_SEI(regs, mem, opcode.mode);
            break;
        case TAX:
            regs.PC = op_TAX(regs, mem, opcode.mode);
            break;
        case TAY:
            regs.PC = op_TAY(regs, mem, opcode.mode);
            break;
        case TXA:
            regs.PC = op_TXA(regs, mem, opcode.mode);
            break;
        case TYA:
            regs.PC = op_TYA(regs, mem, opcode.mode);
            break;
        case TSX:
            regs.PC = op_TSX(regs, mem, opcode.mode);
            break;
        case TXS:
            regs.PC = op_TXS(regs, mem, opcode.mode);
            break;
        case CMP:
            regs.PC = op_CMP(regs, mem, opcode.mode);
            break;
        case CPX:
            regs.PC = op_CPX(regs, mem, opcode.mode);
            break;
        case CPY:
            regs.PC = op_CPY(regs, mem, opcode.mode);
            break;
        case PHA:
            regs.PC = op_PHA(regs, mem, opcode.mode);
            break;
        case PHP:
            regs.PC = op_PHP(regs, mem, opcode.mode);
            break;
        case PLA:
            regs.PC = op_PLA(regs, mem, opcode.mode);
            break;
        case PLP:
            regs.PC = op_PLP(regs, mem, opcode.mode);
            break;
        case BIT:
            regs.PC = op_BIT(regs, mem, opcode.mode);
            break;
        case NOP:
            regs.PC = op_NOP(regs, mem, opcode.mode);
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
