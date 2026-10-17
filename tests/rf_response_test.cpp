#include "../src/rf_response.h"
#include <cassert>

int main() {
    const uint8_t timeout[] = {0x80, 0, 0, 0};
    assert(classifyRfResponse(timeout, sizeof(timeout)) == ReadResult::NoCard);
    const uint8_t crcError[] = {4, 0, 0, 0};
    assert(classifyRfResponse(crcError, sizeof(crcError)) == ReadResult::Unsupported);
    const uint8_t highError[] = {0, 0, 0, 0x80};
    assert(classifyRfResponse(highError, sizeof(highError)) == ReadResult::Unsupported);
    assert(classifyRfResponse(nullptr, 0) == ReadResult::TransportError);
    const uint8_t truncated[] = {0, 0, 0, 0};
    assert(classifyRfResponse(truncated, sizeof(truncated)) == ReadResult::TransportError);
    const uint8_t success[] = {0, 0, 0, 0, 8, 2, 1};
    assert(classifyRfResponse(success, sizeof(success)) == ReadResult::Balance);
    const uint8_t badLength[] = {0, 0, 0, 0, 8, 18, 1};
    assert(classifyRfResponse(badLength, sizeof(badLength)) == ReadResult::TransportError);
}
