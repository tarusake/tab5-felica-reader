#pragma once
namespace stationToggle {
constexpr int x = 850, y = 644, width = 382, height = 64;
inline bool contains(int touchX, int touchY) {
    return touchX >= x && touchX < x + width && touchY >= y && touchY < y + height;
}
}
