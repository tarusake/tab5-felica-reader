#include "stations.h"
#include <stddef.h>
#include "../data/stations.inc"
const Station* findStation(uint32_t key) {
    size_t lo=0, hi=sizeof(stations)/sizeof(stations[0]);
    while (lo<hi) {
        const size_t mid=lo+(hi-lo)/2;
        if (stations[mid].key<key) lo=mid+1;
        else hi=mid;
    }
    return lo<sizeof(stations)/sizeof(stations[0]) && stations[lo].key==key ? stations+lo : nullptr;
}
