#ifndef TIME_HH
#define TIME_HH

#include <stdint.h>

class Time
{
private:
    static uint64_t cachedTime;
public:
    static uint64_t get64() { return cachedTime; }
    static uint32_t get32() { return (uint32_t)cachedTime; }
    static uint64_t getPrecise64() { update(); return cachedTime; }
    static uint32_t getPrecise32() { update(); return (uint32_t)cachedTime; }
    static void scheduleWakeUp(uint32_t time);
    static void update();
};

#endif // TIME_HH