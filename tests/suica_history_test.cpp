#include "../src/suica_history.h"
#include "../src/stations.h"
#include "../src/station_speech.h"
#include <cassert>
#include <cstring>
int main() {
    uint8_t record[16]={0x16,0x01,0,2,0x35,0x51,1,2,1,1,0,0,0,0,0,0};
    uint32_t key=0xffffffff;
    assert(exitStationKey(record,16,key) && key==0x000101);
    const Station* station=findStation(key);
    assert(station && std::strcmp(station->name,"東京")==0);
    assert(stationSpeech(station->kana)=="とうきょうえきにいったんですね");
    record[15]=0x20; assert(exitStationKey(record,16,key) && key==0x020101);
    record[1]=0x15; assert(exitStationKey(record,16,key)); // exit autocharge
    record[0]=0x17; assert(exitStationKey(record,16,key)); // simple gate
    for (int device : {0x05,0x12,0x1b,0x1f,0xc7,0xc8}) {
        record[0]=device;assert(!exitStationKey(record,16,key));
    }
    record[0]=0x16;
    for (int operation : {0x02,0x03,0x07,0x0d,0x0f,0x14,0x46}) {
        record[1]=operation;assert(!exitStationKey(record,16,key));
    }
    record[1]=1;
    assert(!exitStationKey(nullptr,16,key));assert(!exitStationKey(record,15,key));
    record[4]=record[5]=0;assert(!exitStationKey(record,16,key));
    assert(!findStation(0xffffff));
    assert(stationSpeech(nullptr).empty());
}
