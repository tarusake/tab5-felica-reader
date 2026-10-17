#include "../src/balance_speech.h"
#include <cassert>
int main() {
    assert(balanceSpeech(0) == "のこり#ぜろえんです");
    assert(balanceSpeech(10) == "のこり#じゅうえんです");
    assert(balanceSpeech(100) == "のこり#ひゃくえんです");
    assert(balanceSpeech(300) == "のこり#さんびゃくえんです");
    assert(balanceSpeech(600) == "のこり#ろっぴゃくえんです");
    assert(balanceSpeech(800) == "のこり#はっぴゃくえんです");
    assert(balanceSpeech(1000) == "のこり#せんえんです");
    assert(balanceSpeech(3000) == "のこり#さんぜんえんです");
    assert(balanceSpeech(8000) == "のこり#はっせんえんです");
    assert(balanceSpeech(12010) == "のこり#いちまんにせんじゅうえんです");
    assert(balanceSpeech(20000) == "のこり#にまんえんです");
    assert(balanceSpeech(65535) == "のこり#ろくまんごせんごひゃくさんじゅうごえんです");
    assert(balanceSpeech(-1).empty());
    assert(balanceSpeech(65536).empty());
}
