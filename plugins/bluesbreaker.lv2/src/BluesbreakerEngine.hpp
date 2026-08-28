#ifndef BLUESBREAKER_ENGINE_HPP
#define BLUESBREAKER_ENGINE_HPP

#include <cmath>
#include <algorithm>
#include <vector>

namespace AudioDSP {

constexpr float TWO_PI = 6.28318530717958647692f;

/**
 * @brief High-precision circuit model of the vintage Marshall Bluesbreaker (BB-1) overdrive pedal.
 * Features op-amp soft-clipping feedback diodes, 220Hz bass-tightening highpass, 
 * passive active tone circuit, and smooth dynamic touch sensitivity.
 */
class BluesbreakerEngine {
public:
    BluesbreakerEngine() = default;
    ~BluesbreakerEngine() = default;

    void init(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() {
        mHpState = 0.0f;
        mToneLpState1 = 0.0f;
        mToneLpState2 = 0.0f;
        mPostFilterState = 0.0f;
        mGain = 0.5f;
        mTone = 0.5f;
        mVolume = 0.5f;
    }

    void setGain(float gain01) {
        mGain = std::clamp(gain01, 0.0f, 1.0f);
    }

    void setTone(float tone01) {
        mTone = std::clamp(tone01, 0.0f, 1.0f);
    }

    void setVolume(float vol01) {
        mVolume = std::clamp(vol01, 0.0f, 1.0f);
    }

    void process(const float* in, float* out, uint32_t numSamples) {
        if (!in || !out || numSamples == 0) return;

        const float sRate = static_cast<float>(mSampleRate);

        // Pre-gain highpass (220 Hz 1st order filter) to tighten bass response
        float hpCoeff = 1.0f - std::exp(-TWO_PI * 220.0f / sRate);

        // Drive gain multiplier: 1.0x (clean boost) up to 45.0x (full vintage crunch)
        float driveGain = 1.0f + std::pow(mGain, 2.2f) * 44.0f;

        // Post-clipping Tone filter (cutoff sweeps 1200 Hz to 8500 Hz)
        float toneCutoff = 1200.0f + std::pow(mTone, 1.5f) * 7300.0f;
        float toneCoeff = 1.0f - std::exp(-TWO_PI * toneCutoff / sRate);

        // Anti-aliasing / smoothing lowpass (12 kHz)
        float postCoeff = 1.0f - std::exp(-TWO_PI * 12000.0f / sRate);

        // Output Volume scaling (with clean boost range up to +6 dB)
        float outGain = std::pow(mVolume, 1.8f) * 2.2f;

        for (uint32_t s = 0; s < numSamples; ++s) {
            float x = in[s];

            // 1. Pre-Gain Highpass (tightens low-end flub)
            mHpState += (x - mHpState) * hpCoeff;
            float filteredX = x - mHpState;

            // 2. Op-Amp Gain Stage & Feedback Diode Soft-Clipping (1N4148 model)
            float boosted = filteredX * driveGain;
            
            // Symmetrical / slightly asymmetrical silicon diode soft-clipping curve
            float clipped;
            if (boosted > 0.0f) {
                clipped = std::tanh(boosted * 1.25f);
            } else {
                clipped = std::tanh(boosted * 1.20f); // slight even harmonic warmth
            }

            // Clean blend component (preserves attack transient dynamics)
            float wet = clipped * 0.85f + filteredX * 0.15f;

            // 3. Dual-Pole Tone Filter Section
            mToneLpState1 += (wet - mToneLpState1) * toneCoeff;
            mToneLpState2 += (mToneLpState1 - mToneLpState2) * toneCoeff;

            float toneOut = mToneLpState2;

            // 4. Output Lowpass Smoothing & Level Control
            mPostFilterState += (toneOut - mPostFilterState) * postCoeff;
            out[s] = mPostFilterState * outGain;
        }
    }

private:
    double mSampleRate = 48000.0;
    float mHpState = 0.0f;
    float mToneLpState1 = 0.0f;
    float mToneLpState2 = 0.0f;
    float mPostFilterState = 0.0f;

    float mGain = 0.5f;
    float mTone = 0.5f;
    float mVolume = 0.5f;
};

} // namespace AudioDSP

#endif // BLUESBREAKER_ENGINE_HPP
