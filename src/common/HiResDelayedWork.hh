#ifndef HIRESDELAYEDWORK_HH
#define HIRESDELAYEDWORK_HH

#include <stdint.h>

#include "List.hh"

class HiResDelayedWork : private ListItem
{
private:
    enum {
        IDLE = 0,
        RUNNING = 1,
        SCHEDULED = 2,
    } state;

    uint16_t timestamp;

public:
    typedef void (*Callback)(HiResDelayedWork*);
    Callback callback;

    void run(int16_t relativeTimeUs, bool reschedule = true);
    void runAbs(uint16_t absoluteTimeUs, bool reschedule = true);
    void cancel();

    static uint16_t getTimeUs();

    static void process();
};

#endif // HIRESDELAYEDWORK_HH
