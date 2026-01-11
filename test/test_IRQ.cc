
#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test_common.hh"
#include "stub_CMSIS.hh"

BEGIN_ISOLATED_NAMESPACE

#define private public
#define protected public

#include "src/IRQ.hh"
#include "src/IRQ.cc"

TEST(IRQ, Guard) {
    {
        IRQ::Guard guard;
    }
}

END_ISOLATED_NAMESPACE
