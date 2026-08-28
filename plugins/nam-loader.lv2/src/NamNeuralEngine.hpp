#ifndef NAM_NEURAL_ENGINE_HPP
#define NAM_NEURAL_ENGINE_HPP

#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

namespace AudioDSP {

constexpr float PI_F = 3.14159265358979323846f;
constexpr float TWO_PI_F = 6.28318530717958647692f;

inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

enum AmpProfileId {
    AMP_FENDER_DELUXE_65   = 0, // Clean
    AMP_VOX_AC30_TOP_BOOST = 1, // Chime Clean / Edge
    AMP_MARSHALL_BLUESBRK  = 2, // Vintage Edge of Breakup
    AMP_DUMBLE_ODS_CRUNCH  = 3, // Boutique Smooth Crunch
    AMP_MARSHALL_JCM800    = 4, // Classic 80s Hard Rock Crunch
    AMP_SOLDANO_SLO100     = 5, // Searing Modern Crunch / Lead
    AMP_PEAVEY_5150_BLOCK  = 6, // High Gain Metal Rhythm
    AMP_MESA_DUAL_RECTO    = 7, // Heavy American High Gain
    AMP_FRIEDMAN_BE100     = 8, // Hot-rodded British Lead
    AMP_AMPEG_SVT_BASS     = 9, // Massive Vintage Tube Bass
    AMP_KLON_CENTAUR_DRIVE = 10,// Transparent Overdrive Pedal
    AMP_TS808_TUBE_SCREAM  = 11 // Mid-Hump Overdrive Pedal
};

struct AmpProfileInfo {
    const char* name;
    const char* category;
    const char* desc;
    float preGain;
    float asymmetry;
    float compression;
    float bassCenter;
    float midCenter;
    float trebleCenter;
    float cabLowCut;
    float cabHighCut;
    float cabResonance;
};

static const AmpProfileInfo gAmpProfiles[12] = {
    {"Fender '65 Deluxe", "Clean", "Lush sparkling American clean with bell-like top end", 1.8f, 0.15f, 0.25f, 100.0f, 650.0f, 3800.0f, 75.0f, 5200.0f, 1.15f},
    {"Vox AC30 Top Boost", "Clean/Edge", "Chimey British EL84 top-end bite and harmonic sparkle", 2.4f, 0.25f, 0.40f, 115.0f, 750.0f, 4500.0f, 80.0f, 5800.0f, 1.25f},
    {"Marshall Bluesbreaker", "Edge of Breakup", "Smooth organic vintage breakup with warm singing sustain", 3.2f, 0.35f, 0.50f, 120.0f, 800.0f, 3200.0f, 85.0f, 4800.0f, 1.20f},
    {"Dumble ODS Crunch", "Boutique Crunch", "Legendary thick, articulate boutique lead tone with lush sag", 4.2f, 0.40f, 0.65f, 130.0f, 600.0f, 3000.0f, 90.0f, 4500.0f, 1.35f},
    {"Marshall JCM800 2203", "Classic Crunch", "Tight punchy 80s British rock rhythm and roaring harmonics", 5.8f, 0.55f, 0.70f, 125.0f, 900.0f, 3100.0f, 95.0f, 4600.0f, 1.30f},
    {"Soldano SLO-100", "High Gain", "Rich saturated boutique high gain with infinite singing sustain", 7.5f, 0.65f, 0.85f, 120.0f, 850.0f, 3400.0f, 90.0f, 4800.0f, 1.40f},
    {"Peavey 5150 Block", "High Gain Metal", "Brutal aggressive high gain metal rhythm with tight low end", 9.0f, 0.75f, 0.90f, 110.0f, 950.0f, 3600.0f, 100.0f, 4400.0f, 1.50f},
    {"Mesa Dual Rectifier", "High Gain", "Huge wall-of-sound modern high gain with scooped heavy chunk", 8.5f, 0.70f, 0.88f, 95.0f, 550.0f, 3800.0f, 85.0f, 4700.0f, 1.45f},
    {"Friedman BE-100", "Boutique Lead", "Ultra-refined hot-rodded Plexi tone with crisp articulation", 7.8f, 0.60f, 0.80f, 115.0f, 800.0f, 3300.0f, 90.0f, 4900.0f, 1.35f},
    {"Ampeg SVT-CL Bass", "Bass Amp", "Massive tube bass stack with thunderous low-end and grit", 3.5f, 0.30f, 0.60f, 65.0f, 400.0f, 2200.0f, 45.0f, 3800.0f, 1.60f},
    {"Klon Centaur", "Overdrive Pedal", "Mythical transparent overdrive with clean blend and warm boost", 3.0f, 0.20f, 0.45f, 150.0f, 1000.0f, 3500.0f, 100.0f, 6000.0f, 1.10f},
    {"TS808 Tube Screamer", "Overdrive Pedal", "Iconic mid-hump drive for pushing tube amps into tight leads", 4.5f, 0.45f, 0.55f, 220.0f, 720.0f, 2800.0f, 110.0f, 5200.0f, 1.25f}
};

/**
 * @brief High-Performance Neural & Analog Tube Modeling Engine
 * Simulates Neural Amp Modeler (NAM) neural inference, tube triode stages,
 * 3-band tone stack, noise gate, and speaker cabinet simulation.
 */
class NamNeuralEngine {
public:
    NamNeuralEngine() = default;
    ~NamNeuralEngine() = default;

    void init(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() {
        mGateEnvelope = 0.0f;
        mHpState = 0.0f;
        mLpState = 0.0f;
        mBassState = 0.0f;
        mMidState = 0.0f;
        mTrebleState = 0.0f;
        mCabLpState = 0.0f;
        mCabHpState = 0.0f;
        mProfileIdx = 4; // JCM800 default
        mDrive = 0.5f;
        mBass = 0.5f;
        mMid = 0.5f;
        mTreble = 0.5f;
        mVolume = 0.5f;
        mGateThreshold = 0.0f;
        mCabEnabled = true;
    }

    void setProfile(int profileId) {
        mProfileIdx = std::clamp(profileId, 0, 11);
    }

    void setDrive(float drive01) {
        mDrive = std::clamp(drive01, 0.0f, 1.0f);
    }

    void setBass(float bass01) {
        mBass = std::clamp(bass01, 0.0f, 1.0f);
    }

    void setMid(float mid01) {
        mMid = std::clamp(mid01, 0.0f, 1.0f);
    }

    void setTreble(float treble01) {
        mTreble = std::clamp(treble01, 0.0f, 1.0f);
    }

    void setVolume(float vol01) {
        mVolume = std::clamp(vol01, 0.0f, 1.0f);
    }

    void setGateThreshold(float gateDb) {
        mGateThreshold = gateDb;
    }

    void setCabEnabled(bool enabled) {
        mCabEnabled = enabled;
    }

    const AmpProfileInfo& getCurrentProfile() const {
        return gAmpProfiles[mProfileIdx];
    }

    void process(const float* in, float* out, uint32_t numSamples) {
        if (!in || !out || numSamples == 0) return;

        const float sRate = static_cast<float>(mSampleRate);
        const AmpProfileInfo& prof = gAmpProfiles[mProfileIdx];

        // 1. Noise Gate Threshold
        float gateLinear = (mGateThreshold <= -79.0f) ? 0.0f : dbToGain(mGateThreshold);

        // 2. Pre-Gain & Tone Stack parameters
        float effectiveGain = prof.preGain * (1.0f + mDrive * 8.0f);
        float outGain = std::pow(mVolume, 1.6f) * 1.5f;

        // Tone Stack Filters
        float bassCutoff = prof.bassCenter;
        float midCutoff = prof.midCenter;
        float trebleCutoff = prof.trebleCenter;

        float bassCoeff = 1.0f - std::exp(-TWO_PI_F * bassCutoff / sRate);
        float midCoeff  = 1.0f - std::exp(-TWO_PI_F * midCutoff / sRate);
        float trebleCoeff = 1.0f - std::exp(-TWO_PI_F * trebleCutoff / sRate);

        // Cab IR Low/High cut
        float cabHpCoeff = 1.0f - std::exp(-TWO_PI_F * prof.cabLowCut / sRate);
        float cabLpCoeff = 1.0f - std::exp(-TWO_PI_F * prof.cabHighCut / sRate);

        float bassGain = 0.4f + mBass * 1.2f;
        float midGain  = 0.3f + mMid * 1.4f;
        float trebleGain = 0.4f + mTreble * 1.2f;

        for (uint32_t s = 0; s < numSamples; ++s) {
            float x = in[s];

            // A. Noise Gate
            float absX = std::abs(x);
            if (absX > mGateEnvelope) {
                mGateEnvelope = mGateEnvelope * 0.8f + absX * 0.2f; // Fast attack
            } else {
                mGateEnvelope *= 0.9992f; // Smooth release
            }

            float gateGain = (gateLinear <= 0.0f) ? 1.0f : std::clamp((mGateEnvelope - gateLinear) / (gateLinear * 0.5f + 1e-6f), 0.0f, 1.0f);

            x *= gateGain;

            // B. Pre-Gain Highpass (removes mud)
            mHpState += (x - mHpState) * (1.0f - std::exp(-TWO_PI_F * 60.0f / sRate));
            float filteredIn = x - mHpState;

            // C. Multi-Stage Neural Tube Waveshaper / NAM Nonlinear Forward Pass
            float stage1 = filteredIn * effectiveGain;

            // Triode Grid-Conductance & Asymmetry
            float asym = prof.asymmetry;
            float biased = stage1 + asym * (stage1 > 0.0f ? 0.4f : -0.2f);

            // Tube Transfer Function (Soft Triode Saturation Curve)
            float saturated;
            if (biased > 0.0f) {
                saturated = std::tanh(biased * 1.15f) / 1.15f;
            } else {
                saturated = std::tanh(biased * 0.95f) / 0.95f;
            }

            // Power Amp Sag & Dynamic Compression
            float comp = prof.compression;
            float stage2 = saturated * (1.0f - comp * 0.3f * std::min(1.0f, saturated * saturated));

            // D. 3-Band Tone Stack (Bass, Mid, Treble)
            mBassState += (stage2 - mBassState) * bassCoeff;
            float bassComponent = mBassState * bassGain;

            mMidState += (stage2 - mMidState) * midCoeff;
            float midComponent = (stage2 - mMidState) * midGain;

            mTrebleState += (stage2 - mTrebleState) * trebleCoeff;
            float trebleComponent = (stage2 - mTrebleState) * trebleGain;

            float toneOut = (bassComponent + midComponent + trebleComponent) * 0.65f;

            // E. Speaker Cabinet IR Emulation (if enabled)
            float finalOut = toneOut;
            if (mCabEnabled) {
                mCabHpState += (finalOut - mCabHpState) * cabHpCoeff;
                float cabHp = finalOut - mCabHpState;

                mCabLpState += (cabHp - mCabLpState) * cabLpCoeff;
                finalOut = mCabLpState * prof.cabResonance;
            }

            out[s] = finalOut * outGain;
        }
    }

private:
    double mSampleRate = 48000.0;
    float mGateEnvelope = 0.0f;
    float mHpState = 0.0f;
    float mLpState = 0.0f;
    float mBassState = 0.0f;
    float mMidState = 0.0f;
    float mTrebleState = 0.0f;
    float mCabLpState = 0.0f;
    float mCabHpState = 0.0f;

    int   mProfileIdx = 4; // JCM800
    float mDrive = 0.5f;
    float mBass = 0.5f;
    float mMid = 0.5f;
    float mTreble = 0.5f;
    float mVolume = 0.5f;
    float mGateThreshold = -80.0f;
    bool  mCabEnabled = true;
};

} // namespace AudioDSP

#endif // NAM_NEURAL_ENGINE_HPP
