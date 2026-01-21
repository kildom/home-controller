#ifndef WORKQUEUE_HH
#define WORKQUEUE_HH

#include <stdint.h>

#include "List.hh"

class Work;


class Work : protected ListItem
{
public:
    enum Priority: int8_t {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        DELAYED_IRQ = 3, // Disabled in IRQ handler, but can be enabled if needed
    };

protected:
    enum {
        IDLE = 0,
        RUNNING = 1,
        QUEUED = 2,
        SCHEDULED = 3,
    } state;
    Priority priority;

    static Work* getNext();

public:
    typedef void (*Callback)(Work*);
    Callback callback;

    Work(Callback callback, Priority priority = NORMAL);

    void run();
    void cancel();
    // get/setPriority if needed (they must remove and requeue the work if queued)

    static void mainLoop();
};


class DelayedWork : public Work
{
private:
    uint32_t timestamp;

    void runAbsNoIRQ(uint32_t absoluteTime, bool reschedule);
    static void process();

public:
    typedef void (*Callback)(DelayedWork*);

    DelayedWork(Callback callback, Priority priority = NORMAL) : Work((Work::Callback)(void*)callback, priority) { }

    void run() = delete;
    void run(int32_t relativeTime, bool reschedule = true); // IDLE - from now, RUNNING - from timestamp, QUEUED - cancel and from now, SCHEDULED - cancel and from now
    void runAbs(uint32_t absoluteTime, bool reschedule = true);
    void cancel();

    static void processIRQ();

    friend void Work::mainLoop();
};


class IdleWork : private ListItem
{
private:
    static void executeAll();

public:
    typedef void (*Callback)(IdleWork*);
    Callback callback;

    IdleWork(Callback callback) : callback(callback) { next = nullptr; prev = nullptr; }

    void run();
    void cancel();

    friend void Work::mainLoop();
};


#endif // WORKQUEUE_HH
