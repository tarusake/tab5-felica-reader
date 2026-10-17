#pragma once
#include <string>
inline std::string stationSpeech(const char* kana) {
    return kana && *kana ? std::string(kana) + "えきにいったんですね" : std::string();
}
