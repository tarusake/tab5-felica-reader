#include <M5Unified.h>
#include <SanoTTS.h>
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <cmath>
#include <vector>
#include "speech.h"
#include "balance_speech.h"

namespace {
QueueHandle_t requests;
constexpr size_t sampleRate = 22050;
constexpr size_t capacity = sampleRate * 20;
struct Audio {
    int16_t* samples = nullptr;
    size_t used = 0;
    bool overflow = false;
};
void collect(const int16_t* pcm, size_t count, void* user) {
    auto& audio = *static_cast<Audio*>(user);
    if (count > capacity - audio.used) audio.overflow = true;
    if (!audio.overflow) {
        memcpy(audio.samples + audio.used, pcm, count * sizeof(int16_t));
        audio.used += count;
    }
    // The scalar P4 path must let the idle task and USB loop run regularly.
    vTaskDelay(1);
}
void worker(void*) {
    SanoTTS tts;
    // A slightly longer utterance is easier to understand over TAB5's speaker.
    tts.setLengthScale(1.5f);
    Audio audio;
    audio.samples = static_cast<int16_t*>(heap_caps_malloc(capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const bool available = audio.samples && tts.begin();
    Serial.printf("TTS init: %s, internal free=%u, PSRAM free=%u\n",
        available ? "ready (P4 scalar, kana)" : tts.lastError(),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    int balance;
    std::vector<int16_t> introduction;
    while (true) {
        xQueueReceive(requests, &balance, portMAX_DELAY);
        if (!available) { Serial.println("TTS unavailable; beep/display still active"); continue; }
        const auto text = balanceSpeech(balance);
        if (text.empty()) continue;
        audio.used = 0;
        audio.overflow = false;
        const uint32_t start = millis();
        // Synthesize the fixed introduction once, then insert a definite beat
        // of silence before the separately synthesized amount.
        bool ok = true;
        if (introduction.empty()) {
            ok = tts.synthesize("のこり", collect, &audio);
            if (ok && !audio.overflow && audio.used) {
                introduction.assign(audio.samples, audio.samples + audio.used);
            } else {
                ok = false;
            }
        }
        if (ok) {
            constexpr size_t pauseSamples = sampleRate * 300 / 1000;
            memcpy(audio.samples, introduction.data(), introduction.size() * sizeof(int16_t));
            memset(audio.samples + introduction.size(), 0, pauseSamples * sizeof(int16_t));
            audio.used = introduction.size() + pauseSamples;
            audio.overflow = false;
            const auto amount = text.substr(text.find('#') + 1);
            ok = tts.synthesize(amount.c_str(), collect, &audio);
        }
        Serial.printf("TTS: %s / synth=%lu ms audio=%u ms peak=%ld\n", text.c_str(),
            (unsigned long)(millis() - start), unsigned(audio.used * 1000 / sampleRate), (long)tts.absmax());
        if (!ok || audio.overflow || !audio.used) {
            Serial.printf("TTS synthesis failed: %s, overflow=%d\n", tts.lastError(), audio.overflow);
            continue;
        }
        // A newer scan supersedes a result that is still being synthesized.
        if (uxQueueMessagesWaiting(requests)) continue;
        while (M5.Speaker.isPlaying()) vTaskDelay(pdMS_TO_TICKS(10));
        if (uxQueueMessagesWaiting(requests)) continue;
        // Synthesized speech is much quieter than a full-scale beep. Normalize
        // its peak and use a separate master volume only during narration.
        int peak = 0;
        double energy = 0;
        for (size_t i = 0; i < audio.used; ++i) {
            const int sample = audio.samples[i];
            peak = std::max(peak, std::abs(sample));
            energy += double(sample) * sample;
        }
        Serial.printf("TTS PCM: peak=%d rms=%.0f output=%lu Hz\n", peak,
                      std::sqrt(energy / audio.used), (unsigned long)M5.Speaker.config().sample_rate);
        if (!peak) { Serial.println("TTS silent PCM rejected"); continue; }
        const float gain = 28000.0f / peak;
        for (size_t i = 0; i < audio.used; ++i) audio.samples[i] = int16_t(audio.samples[i] * gain);
        const uint8_t previousVolume = M5.Speaker.getVolume();
        M5.Speaker.setVolume(176);
        if (!M5.Speaker.playRaw(audio.samples, audio.used, sampleRate, false, 1, 0, false)) {
            M5.Speaker.setVolume(previousVolume);
            Serial.println("TTS playback failed");
            continue;
        }
        // playRaw retains this pointer; never overwrite it during playback.
        while (M5.Speaker.isPlaying(0)) vTaskDelay(pdMS_TO_TICKS(10));
        M5.Speaker.setVolume(previousVolume);
        Serial.println("TTS playback complete");
    }
}
}

bool beginSpeech() {
    requests = xQueueCreate(1, sizeof(int));
    if (!requests) return false;
    if (xTaskCreatePinnedToCore(worker, "balance-tts", 16384, nullptr, 1, nullptr, 1) != pdPASS) {
        vQueueDelete(requests);
        requests = nullptr;
        return false;
    }
    return true;
}
void speakBalance(int balance) {
    if (requests) xQueueOverwrite(requests, &balance);
}
