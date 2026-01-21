// filepath: /home/dok/my/home-controller/src/common/WorkQueue.cc

// #include <stdio.h>

#include "Utils.hh"
#include "HW.hh"
#include "IRQ.hh"
#include "Time.hh"
#include "WorkQueue.hh"


static List queues[4];
static List delayed[2];
static List idleWork;

extern DelayedWork flushWork;


Work::Work(Callback callback, Priority priority)
    : state(IDLE), priority(priority), callback(callback)
{
}

void Work::run()
{
    List& list = queues[priority];

    __SEV();

    IRQ::Guard guard;

    if (state != QUEUED) {
        list.addLast(this);
        state = QUEUED;
    }
}

void Work::cancel()
{
    IRQ::Guard guard;

    if (state == QUEUED) {
        remove();
        state = IDLE;
    }
}

void DelayedWork::runAbsNoIRQ(uint32_t absoluteTime, bool reschedule)
{
    if (state == QUEUED || state == SCHEDULED) {
        if (reschedule) {
            remove();
            state = IDLE;
        } else {
            return;
        }
    }

    List &list = delayed[priority == DELAYED_IRQ ? 1 : 0];

    auto item = list.first();
    auto end = list.listEnd();
    while (item != end && (int32_t)(((DelayedWork*)item)->timestamp - absoluteTime) < 0) {
        item = item->next;
    }
    item->addBefore(this);
    this->timestamp = absoluteTime;
    this->state = SCHEDULED;
    // if (this != &flushWork) myprintf("Scheduled work at %d, now %d\n", absoluteTime, Time::get32());
    // item = delayed.first();
    // end = delayed.listEnd();
    // printf("---\n");
    // while (item != end) {
    //     printf(" - delayed work at %d @ 0x%08X\n", ((DelayedWork*)item)->timestamp, (uint32_t)item);
    //     item = item->next;
    // }
    // printf("---\n");

}

void DelayedWork::run(int32_t relativeTime, bool reschedule)
{
    __SEV();

    uint32_t absoluteTime = Time::get32() + relativeTime;

    IRQ::Guard guard;

    if (state == RUNNING) {
        absoluteTime = timestamp + relativeTime;
    }
    runAbsNoIRQ(absoluteTime, reschedule);
}

void DelayedWork::runAbs(uint32_t absoluteTime, bool reschedule)
{
    __SEV();

    IRQ::Guard guard;

    runAbsNoIRQ(absoluteTime, reschedule);
}

void DelayedWork::cancel()
{
    IRQ::Guard guard;

    if (state == SCHEDULED || state == QUEUED) {
        remove();
        state = IDLE;
    }
}

void DelayedWork::process()
{
    uint32_t now = Time::getPrecise32();
    uint32_t timestamp = now + 16384;
    auto end = (DelayedWork*)delayed[0].listEnd();

    while (true) {
        IRQ::Guard guard;
        auto work = (DelayedWork*)delayed[0].first();
        if (work == end) {
            break;
        }
        if ((int32_t)(work->timestamp - now) > 0) {
            timestamp = work->timestamp;
            // if (work != &flushWork) myprintf("Next delayed work at %d, now %d\n", timestamp, now);
            break;
        }
        // if (work != &flushWork) myprintf("Delayed work ready %d, now %d\n", work->timestamp, now);
        work->remove();
        List& list = queues[work->priority];
        list.addLast(work);
        work->state = QUEUED;
    }

    {
        IRQ::Guard guard;
        auto work = (DelayedWork*)delayed[1].first();
        if (work != delayed[1].listEnd() && (int32_t)(work->timestamp - timestamp) < 0) {
            timestamp = work->timestamp;
        }
    }

    Time::scheduleWakeUp(timestamp);
}

void DelayedWork::processIRQ()
{
    uint32_t now = Time::getPrecise32();
    uint32_t timestamp = now + 16384;
    auto end = (DelayedWork*)delayed[1].listEnd();

    while (true) {
        DelayedWork* work;

        {
            IRQ::Guard guard;
            work = (DelayedWork*)delayed[1].first();
            if (work == end) {
                break;
            }
            if ((int32_t)(work->timestamp - now) > 0) {
                timestamp = work->timestamp;
                break;
            }
            work->remove();
            work->state = RUNNING;
        }

        work->callback(work);

        {
            IRQ::Guard guard;
            if (work->state == RUNNING) {
                work->state = IDLE;
            }
        }
    }

    {
        IRQ::Guard guard;
        auto work = (DelayedWork*)delayed[0].first();
        if (work != delayed[0].listEnd() && (int32_t)(work->timestamp - timestamp) < 0) {
            timestamp = work->timestamp;
        }
    }

    Time::scheduleWakeUp(timestamp);
}

Work *Work::getNext()
{
    IRQ::Guard guard;

    for (int index = ARRAY_SIZE(queues) - 1; index >= 0; --index) {

        List &queue = queues[index];
        Work* work = (Work*)queue.first();

        if (work != queue.listEnd()) {
            work->remove();
            work->state = Work::RUNNING;
            return work;
        }
    }

    return nullptr;
}

void IdleWork::run()
{
    IRQ::Guard guard;

    if (next == nullptr) {
        idleWork.addLast(this);
    }
}

void IdleWork::cancel()
{
    IRQ::Guard guard;

    if (next != nullptr) {
        remove();
        next = nullptr;
    }
}

void IdleWork::executeAll()
{
    auto end = (IdleWork*)idleWork.listEnd();
    auto work = (IdleWork*)idleWork.first();

    while (work != end && work != nullptr) {
        auto last = work;
        work = (IdleWork*)work->next;
        last->callback(last);
    }
}

void Work::mainLoop()
{
    while (true) {
        // Clear event flag - ensure we will go to sleep if no work is available
        __SEV();
        __WFE();
        // Make sure delayed work is added to main queue if it is ready
        Time::update();
        DelayedWork::process();
        // Get next work item
        Work* work = Work::getNext();
        if (work == nullptr) {
            // No work available and no event flag was set in the meantime - go to sleep
            IdleWork::executeAll();
            __WFE();
        } else {
            // Run work item
            work->callback(work);
            // If work item was not re-queued, set it to IDLE
            IRQ::Guard guard;
            if (work->state == RUNNING) {
                work->state = IDLE;
            }
        }
    }
}
