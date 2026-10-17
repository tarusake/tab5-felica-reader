#include "../src/balance_display.h"
#include <cassert>

int main() {
    BalanceDisplay display;
    int balance = 1234;
    assert(display.update(ReadResult::Balance, balance, 100) == ReadResult::Balance);
    balance = -1;
    assert(display.update(ReadResult::NoCard, balance, 600) == ReadResult::Balance);
    assert(balance == 1234);
    balance = -1;
    assert(display.update(ReadResult::Unsupported, balance, 5099) == ReadResult::Balance);
    assert(display.update(ReadResult::NoCard, balance, 5100) == ReadResult::NoCard);
    balance = 5678;
    display.update(ReadResult::Balance, balance, 6000);
    balance = 999;
    display.update(ReadResult::Balance, balance, 7000);
    balance = -1;
    assert(display.update(ReadResult::NoCard, balance, 11000) == ReadResult::Balance);
    assert(balance == 999);
    assert(display.update(ReadResult::TransportError, balance, 11001) == ReadResult::TransportError);
    assert(display.update(ReadResult::NoCard, balance, 11002) == ReadResult::NoCard);
    balance = 42;
    display.update(ReadResult::Balance, balance, UINT32_MAX - 1000);
    balance = -1;
    assert(display.update(ReadResult::NoCard, balance, 1000) == ReadResult::Balance);
    assert(balance == 42);
    display.reset();
    assert(display.update(ReadResult::NoCard, balance, 1001) == ReadResult::NoCard);
}
