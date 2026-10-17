#pragma once
#include <cstddef>
#include <cstdint>

enum class ReadResult { NoCard, Balance, Unsupported, TransportError };

inline ReadResult classifyRfResponse(const uint8_t* data, size_t size) {
    if (size < 4) return ReadResult::TransportError;
    const uint32_t status = uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
        (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
    // RF failures contain only the four status bytes, without framing/data.
    if (status == 0x80) return ReadResult::NoCard;
    if (status) return ReadResult::Unsupported;
    if (size < 6 || data[5] != size - 5) return ReadResult::TransportError;
    return ReadResult::Balance;
}
