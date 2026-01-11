#ifndef IRQ_HH
#define IRQ_HH

#include <cstdint>

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
