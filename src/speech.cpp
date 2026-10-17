#include <M5Unified.h>
#include <SanoTTS.h>
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <cmath>
#include <vector>
#include <atomic>
#include <esp_cpu.h>
extern "C" {
#include <core/saan_prof.h>
#if SAAN_PROFILE
uint32_t saan_prof_now(void) { return esp_cpu_get_cycle_count(); }
#endif
}
#include "speech.h"
#include "stations.h"
#include "station_speech.h"
#include "balance_speech.h"

namespace {
QueueHandle_t requests;
std::atomic<bool> stationEnabled{true};
struct SpeechRequest { int balance; const Station* station; };
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
    tts.setLengthScale(1.7f);
    Audio audio;
    audio.samples = static_cast<int16_t*>(heap_caps_malloc(capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const bool available = audio.samples && tts.begin();
    Serial.printf("TTS init: %s, internal free=%u, PSRAM free=%u\n",
        available ? "ready (P4 scalar, kana)" : tts.lastError(),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    SpeechRequest request;
    std::vector<int16_t> introduction;
    std::vector<int16_t> ending;
    // Generate fixed phrases on the device once, then reuse them.
    auto cache = [&](const char* phrase, std::vector<int16_t>& saved) {
        if (!saved.empty()) return true;
        audio.used = 0;
        audio.overflow = false;
        if (!tts.synthesize(phrase, collect, &audio) || audio.overflow || !audio.used) return false;
        saved.assign(audio.samples, audio.samples + audio.used);
        return true;
    };
    if (available) {
        const uint32_t start = millis();
        const bool ready = cache("のこり", introduction) && cache("です", ending);
        Serial.printf("TTS fixed phrases: %s, prepare=%lu ms\n",
            ready ? "ready" : "failed (retry on request)", (unsigned long)(millis() - start));
    }
    while (true) {
        xQueueReceive(requests, &request, portMAX_DELAY);
        const int balance = request.balance;
        if (!available) { Serial.println("TTS unavailable; beep/display still active"); continue; }
        const auto text = balanceSpeech(balance);
        if (text.empty()) continue;
        for (int part = 0; part < (request.station ? 2 : 1); ++part) {
            if (part && !stationEnabled.load()) break;
            audio.used = 0;
            audio.overflow = false;
            const uint32_t start = millis();
#if SAAN_PROFILE
            saan_prof_reset();
#endif
            bool ok = true;
            if (part == 1) {
                const auto phrase = stationSpeech(request.station->kana);
                Serial.printf("Station speech: %s駅に行ったんですね！\n", request.station->name);
                ok = tts.synthesize(phrase.c_str(), collect, &audio);
            } else {
                ok = cache("のこり", introduction) && cache("です", ending);
                if (ok) {
                    constexpr size_t pauseSamples = sampleRate * 100 / 1000;
                    memcpy(audio.samples, introduction.data(), introduction.size() * sizeof(int16_t));
                    memset(audio.samples + introduction.size(), 0, pauseSamples * sizeof(int16_t));
                    audio.used = introduction.size() + pauseSamples;
                    audio.overflow = false;
                    auto amount = text.substr(text.find('#') + 1);
                    // balanceSpeech always ends in 「です」; synthesize through 「えん」.
                    amount.resize(amount.size() - std::string("です").size());
                    ok = tts.synthesize(amount.c_str(), collect, &audio);
                    if (ok && !audio.overflow) {
                        if (pauseSamples + ending.size() > capacity - audio.used) {
                            audio.overflow = true;
                        } else {
                            memset(audio.samples + audio.used, 0, pauseSamples * sizeof(int16_t));
                            audio.used += pauseSamples;
                            memcpy(audio.samples + audio.used, ending.data(), ending.size() * sizeof(int16_t));
                            audio.used += ending.size();
                        }
                    }
                }
            }
            Serial.printf("TTS %s: %s / synth=%lu ms audio=%u ms peak=%ld\n", part ? "station" : "balance",
                part ? request.station->name : text.c_str(),
                (unsigned long)(millis() - start), unsigned(audio.used * 1000 / sampleRate), (long)tts.absmax());
            Serial.printf("TTS checksum=%016llx samples=%lu\n", (unsigned long long)tts.checksum(), (unsigned long)tts.samples());
#if SAAN_PROFILE
            for (int id = 0; id < SAAN_PROF_N; ++id) {
                if (saan_prof_cnt[id]) Serial.printf("TTS profile %s=%llu cycles (%lu calls)\n", saan_prof_name(id),
                    (unsigned long long)saan_prof_acc[id], (unsigned long)saan_prof_cnt[id]);
            }
#endif
            if (!ok || audio.overflow || !audio.used) {
                Serial.printf("TTS synthesis failed: %s, overflow=%d\n", tts.lastError(), audio.overflow);
                continue;
            }
            // A newer scan supersedes a result that is still being synthesized.
            if (uxQueueMessagesWaiting(requests) || (part && !stationEnabled.load())) break;
            while (M5.Speaker.isPlaying()) vTaskDelay(pdMS_TO_TICKS(10));
            if (uxQueueMessagesWaiting(requests) || (part && !stationEnabled.load())) break;
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
            M5.Speaker.setVolume(255);
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
}

bool beginSpeech() {
    requests = xQueueCreate(1, sizeof(SpeechRequest));
    if (!requests) return false;
    if (xTaskCreatePinnedToCore(worker, "balance-tts", 16384, nullptr, 1, nullptr, 1) != pdPASS) {
        vQueueDelete(requests);
        requests = nullptr;
        return false;
    }
    return true;
}
void speakBalance(int balance, const Station* station) {
    const SpeechRequest request{balance, stationEnabled.load() ? station : nullptr};
    if (requests) xQueueOverwrite(requests, &request);
}

bool stationSpeechEnabled() { return stationEnabled.load(); }
void setStationSpeechEnabled(bool enabled) { stationEnabled.store(enabled); }
