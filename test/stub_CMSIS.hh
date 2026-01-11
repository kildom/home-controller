#ifndef STUB_CMSIS_HH
#define STUB_CMSIS_HH

#include <stdint.h>

#include "gtest/gtest.h"

static inline uint32_t __get_PRIMASK() { return 0x7E; }
static inline void __disable_irq() {}
static inline void __set_PRIMASK(uint32_t key) { EXPECT_EQ(key, 0x7E); }

#endif // STUB_CMSIS_HH
