
#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test_common.hh"

BEGIN_ISOLATED_NAMESPACE

#define private public
#define protected public

#include "src/CRC.hh"
#include "src/CRC.cc"

END_ISOLATED_NAMESPACE
