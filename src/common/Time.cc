
#include "HW.hh"
#include "IRQ.hh"

#include "Time.hh"

uint64_t Time::cachedTime;

void Time::scheduleWakeUp(uint32_t timestamp)
{
    IRQ::Guard guard;
    if ((int32_t)(timestamp - cachedTime) >= 16384) {
        uint16_t cnt = __HAL_TIM_GET_COUNTER(MAIN_TIMER);
        __HAL_TIM_SET_COMPARE(MAIN_TIMER, TIM_CHANNEL_1, cnt + 16384);
    } else {
        uint16_t exp = (uint16_t)timestamp;
        __HAL_TIM_SET_COMPARE(MAIN_TIMER, TIM_CHANNEL_1, exp);
        uint16_t cnt = __HAL_TIM_GET_COUNTER(MAIN_TIMER);
        if ((int16_t)(exp - cnt) < 1) {
            __SEV();
        }
    }
}

void Time::update()
{
    IRQ::Guard guard;
    uint16_t timerValue = __HAL_TIM_GET_COUNTER(MAIN_TIMER);
    uint16_t prevValue = (uint16_t)cachedTime;
    if (timerValue < prevValue) {
        cachedTime += 0x10000uLL;
    }
    cachedTime = (cachedTime & 0xFFFFFFFFFFFF0000uLL) | (uint64_t)timerValue;
}
