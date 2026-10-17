#pragma once
bool beginSpeech();
struct Station;
void speakBalance(int balance, const Station* station = nullptr);

bool stationSpeechEnabled();
void setStationSpeechEnabled(bool enabled);
