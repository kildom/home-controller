#ifndef IRQ_HH
#define IRQ_HH

#include <stdint.h>
#include "HW.hh"

#define REENTRY_GUARD_BEGIN do {                                  \
    static volatile bool _rg_active = false;                      \
    {                                                             \
        IRQ::Guard guard;                                         \
        if (_rg_active) {                                         \
            break;                                                \
        }                                                         \
        _rg_active = true;                                        \
    }                                                             \
    struct _rg_TMP {                                              \
        ~_rg_TMP() { _rg_active = false; __COMPILER_BARRIER(); }  \
    } _rg_tmp_guard;

#define REENTRY_GUARD_END } while (0)

        
class IRQ
{
public:
    static uint32_t disable() {
        auto key = __get_PRIMASK();
        __disable_irq();
        return key;
    }

    static void enable(uint32_t key) {
        __set_PRIMASK(key);
    }

    class Guard {
    public:
        Guard() : key(IRQ::disable()) { }
        ~Guard() { IRQ::enable(key); }
        void enable() { IRQ::enable(key); }
    private:
        uint32_t key;
    };

};


#endif // IRQ_HH
