#pragma once
#include <stdint.h>
struct Station { uint32_t key; const char* name; const char* kana; };
const Station* findStation(uint32_t key);
