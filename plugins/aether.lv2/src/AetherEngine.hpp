#ifndef AETHER_ENGINE_HPP
#define AETHER_ENGINE_HPP

#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdint>

namespace AudioDSP {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;

inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// Simple 1st-order lowpass / highpass / shelving filter
struct BiquadFilter {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;

    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    void setLowpass(float cutoff, float sampleRate) {
        cutoff = std::clamp(cutoff, 20.0f, sampleRate * 0.49f);
        float w0 = TWO_PI * cutoff / sampleRate;
        float cosw0 = std::cos(w0);
        float alpha = std::sin(w0) * 0.7071f;
        float a0 = 1.0f + alpha;

        b0 = ((1.0f - cosw0) / 2.0f) / a0;
        b1 = (1.0f - cosw0) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighpass(float cutoff, float sampleRate) {
        cutoff = std::clamp(cutoff, 20.0f, sampleRate * 0.49f);
        float w0 = TWO_PI * cutoff / sampleRate;
        float cosw0 = std::cos(w0);
        float alpha = std::sin(w0) * 0.7071f;
        float a0 = 1.0f + alpha;

        b0 = ((1.0f + cosw0) / 2.0f) / a0;
        b1 = (-(1.0f + cosw0)) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighShelf(float cutoff, float gainDb, float sampleRate) {
        cutoff = std::clamp(cutoff, 20.0f, sampleRate * 0.49f);
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = TWO_PI * cutoff / sampleRate;
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = (sinw0 / 2.0f) * std::sqrt((A + 1.0f / A) * (1.0f / 0.7071f - 1.0f) + 2.0f);
        float sqrtA2 = 2.0f * std::sqrt(A) * alpha;

        float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + sqrtA2;
        b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 + sqrtA2)) / a0;
        b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 - sqrtA2)) / a0;
        a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - sqrtA2) / a0;
    }

    void setLowShelf(float cutoff, float gainDb, float sampleRate) {
        cutoff = std::clamp(cutoff, 20.0f, sampleRate * 0.49f);
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = TWO_PI * cutoff / sampleRate;
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = (sinw0 / 2.0f) * std::sqrt((A + 1.0f / A) * (1.0f / 0.7071f - 1.0f) + 2.0f);
        float sqrtA2 = 2.0f * std::sqrt(A) * alpha;

        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + sqrtA2;
        b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + sqrtA2)) / a0;
        b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - sqrtA2)) / a0;
        a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - sqrtA2) / a0;
    }

    inline float process(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// Allpass Diffuser Stage with Modulation
struct AllpassStage {
    std::vector<float> buffer;
    int size = 0;
    int writeIndex = 0;
    float feedback = 0.7f;
    float lfoPhase = 0.0f;
    float lfoRate = 0.5f;
    float modDepth = 0.0f;

    void init(int maxDelay) {
        size = std::max(128, maxDelay);
        buffer.assign(size, 0.0f);
        writeIndex = 0;
        lfoPhase = 0.0f;
    }

    void reset() {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    inline float process(float in, float delaySamples, float sampleRate) {
        if (size <= 0) return in;

        // Modulated read index
        float mod = 0.0f;
        if (modDepth > 0.001f) {
            mod = std::sin(lfoPhase) * modDepth * (sampleRate * 0.001f);
            lfoPhase += TWO_PI * lfoRate / sampleRate;
            if (lfoPhase >= TWO_PI) lfoPhase -= TWO_PI;
        }

        float totalDelay = std::clamp(delaySamples + mod, 1.0f, static_cast<float>(size - 2));
        float readPos = static_cast<float>(writeIndex) - totalDelay;
        if (readPos < 0.0f) readPos += size;

        int i0 = static_cast<int>(readPos);
        int i1 = (i0 + 1) % size;
        float frac = readPos - static_cast<float>(i0);

        float delayed = buffer[i0] * (1.0f - frac) + buffer[i1] * frac;

        float w = in + feedback * delayed;
        // Optional soft-saturation on high feedback
        if (std::abs(w) > 1.2f) w = std::tanh(w);

        float out = delayed - feedback * w;

        buffer[writeIndex] = w;
        writeIndex = (writeIndex + 1) % size;

        return out;
    }
};

// Modulated Delay Line for Late Reverberation
struct LateDelayLine {
    std::vector<float> buffer;
    int size = 0;
    int writeIndex = 0;
    float lfoPhase = 0.0f;
    BiquadFilter lowShelf;
    BiquadFilter highShelf;
    BiquadFilter highCut;

    void init(int maxDelay) {
        size = std::max(256, maxDelay);
        buffer.assign(size, 0.0f);
        writeIndex = 0;
        lfoPhase = 0.0f;
        lowShelf.reset();
        highShelf.reset();
        highCut.reset();
    }

    void reset() {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
        lowShelf.reset();
        highShelf.reset();
        highCut.reset();
    }

    inline float read(float delaySamples, float modDepthMs, float modRateHz, float sampleRate) {
        if (size <= 0) return 0.0f;

        float mod = 0.0f;
        if (modDepthMs > 0.001f) {
            mod = std::sin(lfoPhase) * modDepthMs * (sampleRate * 0.001f);
            lfoPhase += TWO_PI * modRateHz / sampleRate;
            if (lfoPhase >= TWO_PI) lfoPhase -= TWO_PI;
        }

        float totalDelay = std::clamp(delaySamples + mod, 1.0f, static_cast<float>(size - 2));
        float readPos = static_cast<float>(writeIndex) - totalDelay;
        if (readPos < 0.0f) readPos += size;

        int i0 = static_cast<int>(readPos);
        int i1 = (i0 + 1) % size;
        float frac = readPos - static_cast<float>(i0);

        return buffer[i0] * (1.0f - frac) + buffer[i1] * frac;
    }

    inline void write(float sample) {
        if (size <= 0) return;
        buffer[writeIndex] = sample;
        writeIndex = (writeIndex + 1) % size;
    }
};

/**
 * @brief High-Quality Algorithmic Stereo Reverb based on Cloudseed (Aether by Dougal Stewart)
 */
class AetherEngine {
public:
    AetherEngine() = default;
    ~AetherEngine() = default;

    void init(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        // Predelay (up to 500ms stereo)
        int predelaySize = static_cast<int>(mSampleRate * 0.6);
        mPredelayBufL.assign(predelaySize, 0.0f);
        mPredelayBufR.assign(predelaySize, 0.0f);
        mPredelayWrite = 0;

        // Early Diffusion Allpass Filters (8 stages per channel)
        mEarlyDiffL.resize(8);
        mEarlyDiffR.resize(8);
        for (int i = 0; i < 8; ++i) {
            int maxD = static_cast<int>(mSampleRate * 0.15);
            mEarlyDiffL[i].init(maxD);
            mEarlyDiffR[i].init(maxD);
            mEarlyDiffL[i].lfoPhase = (i * 0.785f);
            mEarlyDiffR[i].lfoPhase = (i * 0.785f + 1.57f);
        }

        // Late Delay Lines (12 lines)
        mLateLines.resize(12);
        for (int i = 0; i < 12; ++i) {
            int maxD = static_cast<int>(mSampleRate * 1.5);
            mLateLines[i].init(maxD);
            mLateLines[i].lfoPhase = (i * 0.523f);
        }

        // Late Diffusion Allpass Filters (8 stages per channel)
        mLateDiffL.resize(8);
        mLateDiffR.resize(8);
        for (int i = 0; i < 8; ++i) {
            int maxD = static_cast<int>(mSampleRate * 0.15);
            mLateDiffL[i].init(maxD);
            mLateDiffR[i].init(maxD);
            mLateDiffL[i].lfoPhase = (i * 0.785f);
            mLateDiffR[i].lfoPhase = (i * 0.785f + 1.57f);
        }

        reset();
    }

    void reset() {
        std::fill(mPredelayBufL.begin(), mPredelayBufL.end(), 0.0f);
        std::fill(mPredelayBufR.begin(), mPredelayBufR.end(), 0.0f);
        mPredelayWrite = 0;

        for (auto& ap : mEarlyDiffL) ap.reset();
        for (auto& ap : mEarlyDiffR) ap.reset();
        for (auto& dl : mLateLines) dl.reset();
        for (auto& ap : mLateDiffL) ap.reset();
        for (auto& ap : mLateDiffR) ap.reset();
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t numSamples,
                 float mixPct, float dryLevelPct, float predelayLevelPct, float earlyLevelPct, float lateLevelPct,
                 float widthPct, float predelayMs,
                 bool earlyLowCutOn, float earlyLowCutHz, bool earlyHighCutOn, float earlyHighCutHz,
                 int earlyStages, float earlyDiffMs, float earlyModDepthMs, float earlyModRateHz, float earlyFeedback,
                 int lateDelayCount, float lateDelayMs, float lateModDepthMs, float lateModRateHz, float lateFeedback,
                 int lateStages, float lateDiffMs, float lateDiffModDepthMs, float lateDiffModRateHz, float lateDiffFeedback,
                 bool lateLowShelfOn, float lateLowShelfHz, float lateLowShelfDb,
                 bool lateHighShelfOn, float lateHighShelfHz, float lateHighShelfDb,
                 bool lateHighCutOn, float lateHighCutHz)
    {
        if (!outL || !outR || numSamples == 0) return;

        float sRate = static_cast<float>(mSampleRate);

        float wetMix = std::clamp(mixPct * 0.01f, 0.0f, 1.0f);
        float dryGain = (dryLevelPct * 0.01f) * (1.0f - wetMix * 0.5f);
        float preGain = (predelayLevelPct * 0.01f) * wetMix;
        float earlyGain = (earlyLevelPct * 0.01f) * wetMix;
        float lateGain = (lateLevelPct * 0.01f) * wetMix;
        float width = std::clamp(widthPct * 0.01f, 0.0f, 1.0f);

        float predelaySamples = std::clamp(predelayMs * (sRate * 0.001f), 0.0f, static_cast<float>(mPredelayBufL.size() - 2));
        int preBufSize = static_cast<int>(mPredelayBufL.size());

        earlyStages = std::clamp(earlyStages, 0, 8);
        lateStages = std::clamp(lateStages, 0, 8);
        lateDelayCount = std::clamp(lateDelayCount, 1, 12);

        // Pre-computed prime delays for Cloudseed diffusion stages
        static const float earlyDelayScales[8] = { 1.0f, 1.31f, 1.63f, 1.97f, 2.39f, 2.81f, 3.17f, 3.67f };
        static const float lateDelayScales[12] = { 1.0f, 1.13f, 1.29f, 1.47f, 1.67f, 1.83f, 2.03f, 2.27f, 2.49f, 2.71f, 2.93f, 3.19f };

        for (uint32_t s = 0; s < numSamples; ++s) {
            float xL = inL ? inL[s] : 0.0f;
            float xR = inR ? inR[s] : (inL ? inL[s] : 0.0f);

            // 1. Stereo Width Input Processing
            float mid = 0.5f * (xL + xR);
            float side = 0.5f * (xL - xR) * width;
            float inProcL = mid + side;
            float inProcR = mid - side;

            // 2. Predelay
            mPredelayBufL[mPredelayWrite] = inProcL;
            mPredelayBufR[mPredelayWrite] = inProcR;

            float rPos = static_cast<float>(mPredelayWrite) - predelaySamples;
            if (rPos < 0.0f) rPos += preBufSize;
            int pi0 = static_cast<int>(rPos);
            int pi1 = (pi0 + 1) % preBufSize;
            float pFrac = rPos - static_cast<float>(pi0);

            float preL = mPredelayBufL[pi0] * (1.0f - pFrac) + mPredelayBufL[pi1] * pFrac;
            float preR = mPredelayBufR[pi0] * (1.0f - pFrac) + mPredelayBufR[pi1] * pFrac;

            mPredelayWrite = (mPredelayWrite + 1) % preBufSize;

            // 3. Early Diffusion
            float earlyL = preL;
            float earlyR = preR;

            for (int i = 0; i < earlyStages; ++i) {
                float dSamples = std::clamp(earlyDiffMs * earlyDelayScales[i] * (sRate * 0.001f), 10.0f, static_cast<float>(mEarlyDiffL[i].size - 10));
                mEarlyDiffL[i].feedback = earlyFeedback;
                mEarlyDiffL[i].modDepth = earlyModDepthMs;
                mEarlyDiffL[i].lfoRate = earlyModRateHz;
                earlyL = mEarlyDiffL[i].process(earlyL, dSamples, sRate);

                mEarlyDiffR[i].feedback = earlyFeedback;
                mEarlyDiffR[i].modDepth = earlyModDepthMs;
                mEarlyDiffR[i].lfoRate = earlyModRateHz;
                earlyR = mEarlyDiffR[i].process(earlyR, dSamples * 1.09f, sRate);
            }

            // 4. Late Reverberation Tank (Coupled Modulated Delay Lines)
            float lateSumL = 0.0f;
            float lateSumR = 0.0f;

            for (int i = 0; i < lateDelayCount; ++i) {
                float dTime = lateDelayMs * lateDelayScales[i];
                float dSamples = std::clamp(dTime * (sRate * 0.001f), 10.0f, static_cast<float>(mLateLines[i].size - 50));
                float delayed = mLateLines[i].read(dSamples, lateModDepthMs, lateModRateHz, sRate);

                // Damping Filters in feedback loop
                if (lateHighCutOn) {
                    mLateLines[i].highCut.setLowpass(lateHighCutHz, sRate);
                    delayed = mLateLines[i].highCut.process(delayed);
                }
                if (lateHighShelfOn) {
                    mLateLines[i].highShelf.setHighShelf(lateHighShelfHz, lateHighShelfDb, sRate);
                    delayed = mLateLines[i].highShelf.process(delayed);
                }
                if (lateLowShelfOn) {
                    mLateLines[i].lowShelf.setLowShelf(lateLowShelfHz, lateLowShelfDb, sRate);
                    delayed = mLateLines[i].lowShelf.process(delayed);
                }

                // Cross-mix into stereo channels
                if (i % 2 == 0) lateSumL += delayed;
                else lateSumR += delayed;

                // Write feedback back into delay line
                float feedSample = (i % 2 == 0 ? earlyL : earlyR) + delayed * lateFeedback;
                if (std::abs(feedSample) > 1.2f) feedSample = std::tanh(feedSample);
                mLateLines[i].write(feedSample);
            }

            lateSumL /= std::sqrt(static_cast<float>(lateDelayCount));
            lateSumR /= std::sqrt(static_cast<float>(lateDelayCount));

            // 5. Late Diffusion
            float lateDiffOutL = lateSumL;
            float lateDiffOutR = lateSumR;

            for (int i = 0; i < lateStages; ++i) {
                float dSamples = std::clamp(lateDiffMs * earlyDelayScales[i] * (sRate * 0.001f), 10.0f, static_cast<float>(mLateDiffL[i].size - 10));
                mLateDiffL[i].feedback = lateDiffFeedback;
                mLateDiffL[i].modDepth = lateDiffModDepthMs;
                mLateDiffL[i].lfoRate = lateDiffModRateHz;
                lateDiffOutL = mLateDiffL[i].process(lateDiffOutL, dSamples, sRate);

                mLateDiffR[i].feedback = lateDiffFeedback;
                mLateDiffR[i].modDepth = lateDiffModDepthMs;
                mLateDiffR[i].lfoRate = lateDiffModRateHz;
                lateDiffOutR = mLateDiffR[i].process(lateDiffOutR, dSamples * 1.13f, sRate);
            }

            // 6. Master Mixer
            outL[s] = xL * dryGain + preL * preGain + earlyL * earlyGain + lateDiffOutL * lateGain;
            outR[s] = xR * dryGain + preR * preGain + earlyR * earlyGain + lateDiffOutR * lateGain;
        }
    }

private:
    double mSampleRate = 48000.0;
    std::vector<float> mPredelayBufL;
    std::vector<float> mPredelayBufR;
    int mPredelayWrite = 0;

    std::vector<AllpassStage> mEarlyDiffL;
    std::vector<AllpassStage> mEarlyDiffR;

    std::vector<LateDelayLine> mLateLines;

    std::vector<AllpassStage> mLateDiffL;
    std::vector<AllpassStage> mLateDiffR;
};

} // namespace AudioDSP

#endif // AETHER_ENGINE_HPP
