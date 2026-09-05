#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <atomic>

#include "dr_wav.h"
#include "dr_mp3.h"

namespace AudioDSP {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline float dbToLin(float db) {
    if (db <= -60.0f) return 0.0f;
    return std::pow(10.0f, db * 0.05f);
}

// State Variable Filter (SVF) for tone and sub thump
class StateVariableFilter {
public:
    void reset() {
        s1 = 0.0f;
        s2 = 0.0f;
    }

    void setParameters(float cutoffHz, float q, float sampleRate) {
        cutoffHz = std::max(20.0f, std::min(cutoffHz, sampleRate * 0.45f));
        q = std::max(0.2f, std::min(q, 10.0f));
        float g = std::tan(static_cast<float>(M_PI) * cutoffHz / sampleRate);
        float k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        this->k = k;
    }

    float processLowpass(float in) {
        float v3 = in - s2;
        float v1 = a1 * s1 + a2 * v3;
        float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return v2;
    }

    float processBandpass(float in) {
        float v3 = in - s2;
        float v1 = a1 * s1 + a2 * v3;
        float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return v1;
    }

private:
    float s1{0.0f};
    float s2{0.0f};
    float k{1.0f};
    float a1{0.0f};
    float a2{0.0f};
    float a3{0.0f};
};

// Playback voice for polyphonic / click-free retriggering
struct Voice {
    bool active{false};
    double playhead{0.0};
    double speed{1.0};
    float velocity{1.0f};
    float envAmp{1.0f};
    float ampDecayRate{0.999f};
    float envPunch{1.0f};
    float punchDecayRate{0.99f};
    uint32_t age{0};
};

class CyberStompBoxEngine {
public:
    static constexpr size_t NUM_VOICES = 4;

    void init(double sRate) {
        sampleRate = static_cast<float>(sRate > 8000.0 ? sRate : 48000.0);
        reset();
    }

    void reset() {
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            voices[i].active = false;
            voices[i].playhead = 0.0;
            voices[i].age = 0;
        }
        subFilter.reset();
        toneFilter.reset();
        debounceCounter = 0;
        ledDecay = 0.0f;
    }

    bool loadSampleFile(const std::string& filepath) {
        if (filepath.empty()) return false;

        // Extract filename / extension
        std::string ext;
        size_t dotPos = filepath.find_last_of('.');
        if (dotPos != std::string::npos) {
            ext = filepath.substr(dotPos);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }

        float* pData = nullptr;
        unsigned int channels = 0;
        unsigned int srate = 0;
        uint64_t totalFrames = 0;

        if (ext == ".mp3") {
            drmp3_config cfg;
            drmp3_uint64 fCount = 0;
            pData = drmp3_open_file_and_read_pcm_frames_f32(filepath.c_str(), &cfg, &fCount, NULL);
            if (pData) {
                channels = cfg.channels;
                srate = cfg.sampleRate;
                totalFrames = fCount;
            }
        } else {
            // Default to WAV / AIFF via drwav
            unsigned int c = 0, sr = 0;
            drwav_uint64 fCount = 0;
            pData = drwav_open_file_and_read_pcm_frames_f32(filepath.c_str(), &c, &sr, &fCount, NULL);
            if (pData) {
                channels = c;
                srate = sr;
                totalFrames = fCount;
            }
        }

        if (!pData || totalFrames == 0) {
            return false;
        }

        std::vector<float> newLeft(totalFrames);
        std::vector<float> newRight(totalFrames);

        float peak = 0.0001f;
        if (channels == 1) {
            for (size_t i = 0; i < totalFrames; ++i) {
                float val = pData[i];
                newLeft[i] = val;
                newRight[i] = val;
                float a = std::abs(val);
                if (a > peak) peak = a;
            }
        } else {
            for (size_t i = 0; i < totalFrames; ++i) {
                float vl = pData[i * channels];
                float vr = pData[i * channels + 1];
                newLeft[i] = vl;
                newRight[i] = vr;
                float a = std::max(std::abs(vl), std::abs(vr));
                if (a > peak) peak = a;
            }
        }

        if (ext == ".mp3") {
            drmp3_free(pData, NULL);
        } else {
            drwav_free(pData, NULL);
        }

        // Normalize peak to ~0.9 (-0.9 dB)
        float normGain = 0.9f / peak;
        if (normGain > 4.0f) normGain = 4.0f; // cap max boost
        for (size_t i = 0; i < totalFrames; ++i) {
            newLeft[i] *= normGain;
            newRight[i] *= normGain;
        }

        sampleLeft = std::move(newLeft);
        sampleRight = std::move(newRight);
        sampleTotalFrames = totalFrames;
        sampleFileRate = (srate > 0) ? static_cast<float>(srate) : sampleRate;
        currentFilePath = filepath;

        // Extract base name
        size_t slashPos = filepath.find_last_of("/\\");
        if (slashPos != std::string::npos) {
            currentBaseName = filepath.substr(slashPos + 1);
        } else {
            currentBaseName = filepath;
        }

        sampleDurationSec = static_cast<float>(sampleTotalFrames) / sampleFileRate;
        return true;
    }

    void trigger(float vel = 1.0f) {
        if (debounceCounter > 0) return;
        debounceCounter = static_cast<int>(sampleRate * 0.005f); // ~5ms debounce

        if (sampleTotalFrames == 0) return;

        float effectiveVel = fixedVelocityMode ? 1.0f : std::clamp(vel, 0.15f, 1.0f);

        // Find oldest or inactive voice
        size_t bestIdx = 0;
        uint32_t oldestAge = 0;
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            if (!voices[i].active) {
                bestIdx = i;
                break;
            }
            if (voices[i].age > oldestAge) {
                oldestAge = voices[i].age;
                bestIdx = i;
            }
        }

        Voice& v = voices[bestIdx];
        v.active = true;
        v.playhead = 0.0;
        v.age = 0;
        v.velocity = effectiveVel;
        v.envAmp = 1.0f;
        v.envPunch = 1.0f;

        // Pitch speed: (2^(semitones/12)) * (fileRate / hostRate)
        double pitchRatio = std::pow(2.0, static_cast<double>(pitchSemitones) / 12.0);
        v.speed = pitchRatio * (static_cast<double>(sampleFileRate) / static_cast<double>(sampleRate));

        // Envelopes
        float decayTime = std::max(0.04f, std::min(decaySec, 4.0f));
        v.ampDecayRate = std::exp(-1.0f / (sampleRate * decayTime * 0.8f));
        v.punchDecayRate = std::exp(-1.0f / (sampleRate * 0.015f)); // 15ms punch decay

        ledDecay = 1.0f;
    }

    void setPitchSemitones(float semitones) { pitchSemitones = std::clamp(semitones, -12.0f, 12.0f); }
    void setDecayTime(float sec) { decaySec = std::clamp(sec, 0.05f, 4.0f); }
    void setPunch(float p) { punchAmount = std::clamp(p, 0.0f, 1.0f); }
    void setSubThumpDb(float db) { subThumpGain = dbToLin(std::clamp(db, 0.0f, 15.0f)); }
    void setTone(float t) { toneAmount = std::clamp(t, 0.0f, 1.0f); }
    void setFixedVelocity(bool fixed) { fixedVelocityMode = fixed; }
    void setGuitarGainDb(float db) { guitarGain = dbToLin(db); }
    void setStompGainDb(float db) { stompGain = dbToLin(db); }
    void setSplitOutput(bool split) { splitOutputMode = split; }

    float getActivityLed() const { return ledDecay; }
    const std::string& getSampleName() const { return currentBaseName; }
    const std::string& getFilePath() const { return currentFilePath; }
    float getSampleDuration() const { return sampleDurationSec; }
    uint64_t getSampleFrames() const { return sampleTotalFrames; }

    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t numSamples) {
        // Update filters
        float toneCutoff = 500.0f + toneAmount * 9500.0f;
        toneFilter.setParameters(toneCutoff, 0.707f, sampleRate);
        subFilter.setParameters(52.0f, 1.4f, sampleRate);

        for (uint32_t i = 0; i < numSamples; ++i) {
            if (debounceCounter > 0) debounceCounter--;
            if (ledDecay > 0.0f) {
                ledDecay -= (1.0f / (sampleRate * 0.12f));
                if (ledDecay < 0.0f) ledDecay = 0.0f;
            }

            float stompSampleL = 0.0f;
            float stompSampleR = 0.0f;

            if (sampleTotalFrames > 0) {
                for (size_t vIdx = 0; vIdx < NUM_VOICES; ++vIdx) {
                    Voice& v = voices[vIdx];
                    if (!v.active) continue;
                    v.age++;

                    uint32_t idx0 = static_cast<uint32_t>(v.playhead);
                    if (idx0 >= sampleTotalFrames) {
                        v.active = false;
                        continue;
                    }

                    double frac = v.playhead - static_cast<double>(idx0);
                    uint32_t idx1 = (idx0 + 1 < sampleTotalFrames) ? idx0 + 1 : idx0;

                    // Linear interpolation
                    float sL = static_cast<float>((1.0 - frac) * sampleLeft[idx0] + frac * sampleLeft[idx1]);
                    float sR = static_cast<float>((1.0 - frac) * sampleRight[idx0] + frac * sampleRight[idx1]);

                    // Apply punch transient boost
                    if (punchAmount > 0.01f) {
                        float punchGain = 1.0f + v.envPunch * punchAmount * 1.6f;
                        sL *= punchGain;
                        sR *= punchGain;
                    }

                    // Apply amplitude envelope & velocity
                    float voiceGain = v.envAmp * v.velocity;
                    stompSampleL += sL * voiceGain;
                    stompSampleR += sR * voiceGain;

                    // Advance playhead & envelopes
                    v.playhead += v.speed;
                    v.envAmp *= v.ampDecayRate;
                    v.envPunch *= v.punchDecayRate;

                    if (v.playhead >= sampleTotalFrames || v.envAmp < 0.0001f) {
                        v.active = false;
                    }
                }
            }

            // Apply 50Hz sub thump boost
            if (subThumpGain > 1.01f) {
                float subL = subFilter.processBandpass(stompSampleL) * (subThumpGain - 1.0f);
                stompSampleL += subL;
                stompSampleR += subL;
            }

            // Apply tone low-pass filter
            stompSampleL = toneFilter.processLowpass(stompSampleL);
            stompSampleR = toneFilter.processLowpass(stompSampleR);

            // Soft saturation & master stomp volume
            stompSampleL = std::tanh(stompSampleL * 1.25f) * 0.85f * stompGain;
            stompSampleR = std::tanh(stompSampleR * 1.25f) * 0.85f * stompGain;

            // Guitar pass-through handling
            float inSampleL = inL ? inL[i] * guitarGain : 0.0f;
            float inSampleR = inR ? inR[i] * guitarGain : inSampleL;

            if (splitOutputMode) {
                // Split: Left = Clean Guitar, Right = Stomp to PA/Subwoofer
                outL[i] = inSampleL;
                outR[i] = stompSampleR;
            } else {
                // Mix: Guitar and Stomp mixed to both Left and Right
                outL[i] = inSampleL + stompSampleL;
                outR[i] = inSampleR + stompSampleR;
            }
        }
    }

private:
    float sampleRate{48000.0f};

    // Sample data buffers in RAM
    std::vector<float> sampleLeft;
    std::vector<float> sampleRight;
    uint64_t sampleTotalFrames{0};
    float sampleFileRate{48000.0f};
    float sampleDurationSec{0.0f};
    std::string currentFilePath;
    std::string currentBaseName{"NONE"};

    // Polyphonic voices
    Voice voices[NUM_VOICES];

    // Filters
    StateVariableFilter subFilter;
    StateVariableFilter toneFilter;

    // Parameters
    float pitchSemitones{0.0f};
    float decaySec{0.6f};
    float punchAmount{0.6f};
    float subThumpGain{1.41f}; // ~+3dB
    float toneAmount{0.6f};
    bool fixedVelocityMode{true};
    float guitarGain{1.0f};
    float stompGain{1.0f};
    bool splitOutputMode{false};

    int debounceCounter{0};
    float ledDecay{0.0f};
};

} // namespace AudioDSP
