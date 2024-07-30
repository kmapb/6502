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
