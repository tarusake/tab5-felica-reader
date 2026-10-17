#pragma once
#include <stddef.h>
#include <stdint.h>

// The streaming model often emits eight time positions from an interior convolution window.
// Keep those independent accumulators in registers instead of loading/storing
// the output eight times for every input channel. Each output retains the exact
// input-channel accumulation order of the upstream W8A32 kernel.
static inline int sanotts_p4_conv1x1_8(float* y, const float* x,
        const int8_t* weights, const float* scale, const float* bias,
        int cin, int cout, int ksz, int time, int t0, int t1) {
    if (ksz != 1 || t1 - t0 != 8 || t0 < 0 || t1 > time) return 0;
    const int stride = (cin + 15) & ~15;
    for (int o = 0; o < cout; ++o) {
        float a0=0, a1=0, a2=0, a3=0, a4=0, a5=0, a6=0, a7=0;
        const int8_t* wo = weights + (size_t)o * stride;
        for (int i = 0; i < cin; ++i) {
            const int q = wo[i];
            if (!q) continue;
            const float w = (float)q;
            const float* xi = x + (size_t)i * time + t0;
            a0 += w*xi[0]; a1 += w*xi[1]; a2 += w*xi[2]; a3 += w*xi[3];
            a4 += w*xi[4]; a5 += w*xi[5]; a6 += w*xi[6]; a7 += w*xi[7];
        }
        const float s = scale[o], b = bias ? bias[o] : 0;
        float* out = y + (size_t)o * 8;
        out[0]=a0*s+b; out[1]=a1*s+b; out[2]=a2*s+b; out[3]=a3*s+b;
        out[4]=a4*s+b; out[5]=a5*s+b; out[6]=a6*s+b; out[7]=a7*s+b;
    }
    return 1;
}

static inline int sanotts_p4_conv8(float* y, const float* x,
        const int8_t* weights, const float* scale, const float* bias,
        int cin, int cout, int ksz, int time, int t0, int t1) {
    if (ksz == 1) return sanotts_p4_conv1x1_8(y, x, weights, scale, bias, cin, cout, ksz, time, t0, t1);
    const int pad = ksz / 2;
    if (ksz < 1 || !(ksz & 1) || t1 - t0 != 8 || t0 < pad || t1 + pad > time) return 0;
    const int stride = (cin + 15) & ~15;
    for (int o = 0; o < cout; ++o) {
        float a0=0, a1=0, a2=0, a3=0, a4=0, a5=0, a6=0, a7=0;
        const int8_t* wo = weights + (size_t)o * ksz * stride;
        for (int i = 0; i < cin; ++i) {
            for (int k = 0; k < ksz; ++k) {
                const int q = wo[(size_t)k * stride + i];
                if (!q) continue;
                const float w = (float)q;
                const float* xi = x + (size_t)i * time + t0 + k - pad;
                a0 += w*xi[0]; a1 += w*xi[1]; a2 += w*xi[2]; a3 += w*xi[3];
                a4 += w*xi[4]; a5 += w*xi[5]; a6 += w*xi[6]; a7 += w*xi[7];
            }
        }
        const float s = scale[o], b = bias ? bias[o] : 0;
        float* out = y + (size_t)o * 8;
        out[0]=a0*s+b; out[1]=a1*s+b; out[2]=a2*s+b; out[3]=a3*s+b;
        out[4]=a4*s+b; out[5]=a5*s+b; out[6]=a6*s+b; out[7]=a7*s+b;
    }
    return 1;
}
