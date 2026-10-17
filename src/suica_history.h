#pragma once
#include <stddef.h>
#include <stdint.h>
// Service 090F record: exit line/station at 8/9; regional bits at 15[5:4].
// Only actual rail gate exits count as visits, never shops, buses or charges.
inline bool exitStationKey(const uint8_t* record, size_t size, uint32_t& key) {
    if (!record || size!=16 || (record[0]!=0x16 && record[0]!=0x17) ||
        (record[1]!=0x01 && record[1]!=0x15) || !(record[4] || record[5]) ||
        !record[8] || !record[9]) return false;
    key=(uint32_t((record[15]>>4)&3)<<16) | (uint32_t(record[8])<<8) | record[9];
    return true;
}
