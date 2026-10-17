"""Apply the project's guarded ESP32-P4 kernel to pinned sanoTTS-jp 1.2.0."""
from pathlib import Path
Import("env")
source = (Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
          / "SanoTTS-jp" / "src" / "core" / "saanotts_int8.c")
text = source.read_text()
include = '#include "saanotts_int8.h"'
if '#include <sanotts_p4_conv.h>' not in text:
    if include not in text:
        raise RuntimeError("sanoTTS int8 includes changed; review P4 patch")
    text = text.replace(include, include + '\n#include <sanotts_p4_conv.h>', 1)
signature = '''void saan_conv1d_i8_r(float *y, const float *x, const int8_t *W, const float *scale,
                      const float *b, int cin, int cout, int ksz, int T, int t0, int t1) {'''
hook = '''
#if defined(CONFIG_IDF_TARGET_ESP32P4) && SAAN_P4_FAST
    if (sanotts_p4_conv8(y, x, W, scale, b, cin, cout, ksz, T, t0, t1)) return;
#endif
'''
if 'sanotts_p4_conv8(y,' not in text:
    if signature not in text:
        raise RuntimeError("sanoTTS convolution signature changed; review P4 patch")
    text = text.replace(signature, signature + hook, 1)
if text != source.read_text():
    source.write_text(text)
