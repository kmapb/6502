#include <gtest/gtest.h>

#include "6502.hpp"

namespace {
TEST(Mem, readWrite) {
    Memory mem;
    for (auto i = 0; i < 256; i++) {
        ASSERT_EQ(mem[i], 0);
    }

    mem[0] = 0xff;
    ASSERT_EQ(mem[0], 0xff);

    mem.write16(0xfffe, 0xcafe);
    ASSERT_EQ(mem.read16(0xfffe), 0xcafe);
}

}
