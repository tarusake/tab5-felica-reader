#include "../include/sanotts_p4_conv.h"
#include <vector>
#include <cassert>
#include <cstring>
#include <random>

int main() {
    std::mt19937 random(20261017);
    for (int ksz : {1, 3, 7, 9}) {
    for (int cin : {1, 3, 16, 32, 48, 64, 96, 192, 304}) {
        for (int baseTime : {8, 14, 16}) {
            const int pad = ksz / 2, time = baseTime + 2*pad;
            for (bool withBias : {false, true}) {
                const int cout = 19, stride = (cin + 15) & ~15;
                const int t0 = pad + (baseTime == 8 ? 0 : 3);
                std::vector<float> x(cin*time), scales(cout), bias(cout);
                std::vector<int8_t> weights(cout*ksz*stride);
                for (auto& v : x) v = (int(random()%20001)-10000) / 2048.0f;
                for (auto& v : weights) v = int(random()%255)-127;
                for (auto& v : scales) v = (random()%100+1) / 8192.0f;
                for (auto& v : bias) v = (int(random()%100)-50) / 64.0f;
                std::vector<float> reference(cout*8, 0), actual(cout*8);
                for (int o=0; o<cout; ++o) {
                    for (int i=0; i<cin; ++i) {
                        for (int k=0; k<ksz; ++k) {
                            const float w = weights[(o*ksz+k)*stride+i];
                            if (w == 0) continue;
                            for (int t=0; t<8; ++t) reference[o*8+t] += w*x[i*time+t0+t+k-pad];
                        }
                    }
                    for (int t=0; t<8; ++t) reference[o*8+t] = reference[o*8+t]*scales[o]+(withBias?bias[o]:0);
                }
                assert(sanotts_p4_conv8(actual.data(), x.data(), weights.data(), scales.data(),
                    withBias?bias.data():nullptr, cin, cout, ksz, time, t0, t0+8));
                assert(!std::memcmp(reference.data(), actual.data(), actual.size()*sizeof(float)));
                // Unhandled shapes must leave the output untouched for fallback.
                auto before = actual;
                assert(!sanotts_p4_conv8(actual.data(), x.data(), weights.data(), scales.data(), nullptr,
                    cin, cout, 2, time, t0, t0+8));
                assert(!sanotts_p4_conv8(actual.data(), x.data(), weights.data(), scales.data(), nullptr,
                    cin, cout, 1, time, t0, t0+7));
                assert(actual == before);
                if (ksz > 1) {
                    assert(!sanotts_p4_conv8(actual.data(), x.data(), weights.data(), scales.data(), nullptr,
                        cin, cout, ksz, time, 0, 8));
                    assert(actual == before);
                }
            }
        }
    }
}
}
