

#include <stdio.h>
#include <stdarg.h>

#include "HW.hh"
#include "UART.hh"
#include "Time.hh"
#include "WorkQueue.hh"


IdleWork demo([](IdleWork*) {
//    auto t = Time::get32();
//    auto tt = Time::getPrecise32();
//    printf("- %d %d\n", t, tt);
});


UART uart(&huart2);

extern "C"
void commonMain()
{
    uart.init();
    //demo.run();
    Work::mainLoop();
}

__attribute__((naked))
uint32_t semiCall(uint32_t funcCode, uint32_t param0)
{
    asm __volatile__ ("BKPT 0xAB\nBX lr":::"memory");
}

static char buffer[512];
static int bufferPos = 0;

static void flush(DelayedWork* x = nullptr)
{
    // Flush buffer
    if (bufferPos > 0) {
        //semiCall(0x04, (uint32_t)"----FLUSH3----\r\n");
        buffer[bufferPos] = 0;
        semiCall(0x04, (uint32_t)buffer);
        bufferPos = 0;
    }
}


DelayedWork flushWork(flush, Work::LOW);


void myprintf(const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(&buffer[bufferPos], sizeof(buffer) - bufferPos, fmt, args);
    va_end(args);

    if (len < 0) {
        buffer[bufferPos] = 0;
    } else if (len < (int)(sizeof(buffer) - bufferPos)) {
        bufferPos += len;
    } else {
        va_list args;
        //semiCall(0x04, (uint32_t)"----FLUSH2----\r\n");
        flush();
        va_start(args, fmt);
        len = vsnprintf(&buffer[bufferPos], sizeof(buffer) - bufferPos, fmt, args);
        va_end(args);
        if (len < 0) {
            bufferPos = 0;
        } else if (len < (int)(sizeof(buffer) - bufferPos)) {
            bufferPos += len;
        } else {
            buffer[sizeof(buffer) - 1] = 0;
            bufferPos = sizeof(buffer) - 1;
        }
    }

    if (sizeof(buffer) - bufferPos < 32) {
        //semiCall(0x04, (uint32_t)"----FLUSH1----\r\n");
        flushWork.cancel();
        flush();
    } else {
        Time::update();
        flushWork.run(500, false);
    }
}
