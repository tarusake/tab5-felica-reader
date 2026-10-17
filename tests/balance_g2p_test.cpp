#include "../src/balance_speech.h"
extern "C" {
#include "g2p.h"
}
#include <cassert>
#include <cstdio>
int main() {
    int32_t ids[2048], count, maximum = 0;
    for (int yen = 0; yen <= 65535; ++yen) {
        const auto text = balanceSpeech(yen);
        assert(saan_g2p(text.data(), text.size(), ids, 2048, &count, nullptr) == SAAN_G2P_OK);
        assert(count <= 350);
        if (count > maximum) maximum = count;
    }
    std::printf("All 65536 amounts accepted by sanoTTS G2P (max %d ids)\n", maximum);
}
