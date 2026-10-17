#pragma once
#include <string>

inline std::string balanceSpeech(int yen) {
    if (yen < 0 || yen > 65535) return {};
    const char* digits[] = {"", "いち", "に", "さん", "よん", "ご", "ろく", "なな", "はち", "きゅう"};
    std::string number;
    if (!yen) number = "ぜろ";
    if (yen >= 10000) { number += digits[yen / 10000]; number += "まん"; yen %= 10000; }
    int n = yen / 1000;
    if (n) {
        if (n == 3) number += "さんぜん";
        else if (n == 8) number += "はっせん";
        else { if (n != 1) number += digits[n]; number += "せん"; }
    }
    yen %= 1000;
    n = yen / 100;
    if (n) {
        if (n == 3) number += "さんびゃく";
        else if (n == 6) number += "ろっぴゃく";
        else if (n == 8) number += "はっぴゃく";
        else { if (n != 1) number += digits[n]; number += "ひゃく"; }
    }
    yen %= 100;
    n = yen / 10;
    if (n) { if (n != 1) number += digits[n]; number += "じゅう"; }
    number += digits[yen % 10];
    return "のこり#" + number + "えんです";
}
