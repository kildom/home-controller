#include "IRQ.hh"

IRQGuard::IRQGuard() : key(IRQ::disable()) {
}

IRQGuard::~IRQGuard() {
    IRQ::enable(key);
}
