#include "../src/stations.h"
#include "../src/station_speech.h"
#include "../data/stations.inc"
extern "C" {
#include "g2p.h"
}
#include <cassert>
#include <cstdio>
int main() {
    int32_t ids[2048],count,maximum=0;
    uint32_t previous=0;
    for (const auto& station : stations) {
        assert(station.key>previous);previous=station.key;
        assert(findStation(station.key));
        const auto text=stationSpeech(station.kana);
        if (saan_g2p(text.data(),text.size(),ids,2048,&count,nullptr)!=SAAN_G2P_OK) { std::fprintf(stderr,"Rejected %s: %s\n",station.name,text.c_str()); return 1; }
        assert(count<=350);
        if(count>maximum)maximum=count;
    }
    std::printf("%zu station phrases accepted (max %d ids)\n",sizeof(stations)/sizeof(stations[0]),maximum);
}
