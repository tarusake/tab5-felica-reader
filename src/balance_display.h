#pragma once
#include "rf_response.h"

// Keep a successful result visible even when the card is quickly removed.
class BalanceDisplay {
    bool hasBalance = false;
    int heldBalance = -1;
    uint32_t lastSuccess = 0;
public:
    void reset() { hasBalance = false; }
    ReadResult update(ReadResult result, int& balance, uint32_t now) {
        if (result == ReadResult::TransportError) {
            reset();
        } else if (result == ReadResult::Balance) {
            hasBalance = true;
            heldBalance = balance;
            lastSuccess = now;
        } else if (hasBalance && uint32_t(now - lastSuccess) < 5000) {
            balance = heldBalance;
            return ReadResult::Balance;
        } else {
            reset();
        }
        return result;
    }
};
