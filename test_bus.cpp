#include <gtest/gtest.h>

#include "6502.hpp"
#include "assembler.hpp"

namespace {

struct TestDevice : Device {
    uint8_t last_write_addr_lo = 0;
    uint8_t last_write_val = 0;
    uint8_t read_val = 0x42;
    int read_count = 0;
    int write_count = 0;

    uint8_t read(uint16_t addr) override {
        ++read_count;
        return read_val;
    }

    void write(uint16_t addr, uint8_t val) override {
        ++write_count;
        last_write_addr_lo = addr & 0xff;
        last_write_val = val;
    }
};

TEST(Bus, DeviceReadDispatch) {
    Bus bus;
    TestDevice dev;
    dev.read_val = 0xAB;

    bus.map(0xC0, &dev);

    ASSERT_EQ(bus.read(0xC000), 0xAB);
    ASSERT_EQ(dev.read_count, 1);
    ASSERT_EQ(bus.read(0xC0FF), 0xAB);
    ASSERT_EQ(dev.read_count, 2);
}

TEST(Bus, DeviceWriteDispatch) {
    Bus bus;
    TestDevice dev;

    bus.map(0xC0, &dev);

    bus.write(0xC010, 0x77);
    ASSERT_EQ(dev.write_count, 1);
    ASSERT_EQ(dev.last_write_addr_lo, 0x10);
    ASSERT_EQ(dev.last_write_val, 0x77);
}

TEST(Bus, UnmappedPagesUseRAM) {
    Bus bus;
    TestDevice dev;
    bus.map(0xC0, &dev);

    // Page 0x00 is not mapped, should use RAM
    bus.write(0x0050, 0xEE);
    ASSERT_EQ(bus.read(0x0050), 0xEE);
    ASSERT_EQ(bus.ram[0x0050], 0xEE);
    ASSERT_EQ(dev.read_count, 0);
    ASSERT_EQ(dev.write_count, 0);
}

TEST(Bus, MapPageRange) {
    Bus bus;
    TestDevice dev;
    dev.read_val = 0x99;

    bus.map(0xC0, 0xCF, &dev);

    ASSERT_EQ(bus.read(0xC000), 0x99);
    ASSERT_EQ(bus.read(0xCF00), 0x99);
    // Page 0xBF should still be RAM
    bus.ram[0xBF00] = 0x11;
    ASSERT_EQ(bus.read(0xBF00), 0x11);
}

TEST(Bus, OperatorBracketBypassesDevice) {
    Bus bus;
    TestDevice dev;
    dev.read_val = 0xAB;

    bus.map(0xC0, &dev);

    // operator[] accesses RAM directly, not the device
    bus[0xC000] = 0x55;
    ASSERT_EQ(bus.ram[0xC000], 0x55);
    ASSERT_EQ(bus[0xC000], 0x55);
    ASSERT_EQ(dev.write_count, 0);
    ASSERT_EQ(dev.read_count, 0);

    // But read() goes through the device
    ASSERT_EQ(bus.read(0xC000), 0xAB);
    ASSERT_EQ(dev.read_count, 1);
}

TEST(Bus, CPUReadsFromDevice) {
    Bus bus;
    RegisterFile regs;
    TestDevice dev;
    dev.read_val = 0x42;

    bus.map(0xC0, &dev);

    // LDA $C000 (absolute)
    Assembler a(bus);
    a(LDA, ABS, 0xC000);

    run_instr(regs, bus);
    ASSERT_EQ(regs.A, 0x42);
    ASSERT_EQ(dev.read_count, 1);
}

TEST(Bus, CPUWritesToDevice) {
    Bus bus;
    RegisterFile regs;
    TestDevice dev;

    bus.map(0xC0, &dev);

    regs.A = 0x37;

    // STA $C010 (absolute)
    Assembler a(bus);
    a(STA, ABS, 0xC010);

    run_instr(regs, bus);
    ASSERT_EQ(dev.write_count, 1);
    ASSERT_EQ(dev.last_write_addr_lo, 0x10);
    ASSERT_EQ(dev.last_write_val, 0x37);
}

TEST(Bus, ResetClearsDeviceMappings) {
    Bus bus;
    TestDevice dev;
    dev.read_val = 0xAB;

    bus.map(0xC0, &dev);
    ASSERT_EQ(bus.read(0xC000), 0xAB);

    bus.reset();
    // After reset, page should be unmapped (reads RAM which is zeroed)
    ASSERT_EQ(bus.read(0xC000), 0x00);
}

}
