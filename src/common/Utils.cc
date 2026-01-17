
#include <stdio.h>

#include "HW.hh"
#include "Utils.hh"


void assertImpl(const char* file, int line)
{
    while (1) {
        myprintf("Assertion failed at %s:%d\n", file, line);
    }
}
