#pragma once

#include <cmath>
#include <vector>
#include <array>
#include <random>
#include <algorithm>
#include <cstdint>

namespace aelapse {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;

inline float clamp(float v, float mn, float mx) {
    return (v < mn) ? mn : (v > mx ? mx : v);
}

// Cubic Hermite interpolation for smooth tape delay reading
inline float hermiteInterpolate(float y0, float y1, float y2, float y3, float frac) {
    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

// Tape saturation with asymmetric bias and soft compression
inline float tapeSaturate(float x, float drive) {
    if (drive <= 0.001f) return x;
    float gain = 1.0f + drive * 4.0f;
    float in = x * gain + (0.08f * drive); // slight DC bias for even harmonic generation
    float sat = std::tanh(in) - (0.08f * drive * (1.0f / (1.0f + gain)));
    return sat / (1.0f + drive * 0.8f);
}

// 1st order Lowpass / Highpass for tape frequency shaping
class OnePoleFilter {
public:
    float z1 = 0.0f;

    void reset() { z1 = 0.0f; }

    float processLP(float x, float cutoff, float sampleRate) {
        float norm = clamp(cutoff / (sampleRate * 0.5f), 0.0001f, 0.999f);
        float coeff = 1.0f - std::exp(-TWO_PI * norm);
        z1 += coeff * (x - z1);
        return z1;
    }

    float processHP(float x, float cutoff, float sampleRate) {
        float lp = processLP(x, cutoff, sampleRate);
        return x - lp;
    }
};

// Allpass Filter for Spring Reverb Chirp & Dispersion
class AllPassFilter {
private:
    std::vector<float> buffer;
    size_t writePos = 0;
    size_t length = 1;
    float feedback = 0.6f;

public:
    void init(size_t len, float fb) {
        length = len > 0 ? len : 1;
        buffer.assign(length, 0.0f);
        writePos = 0;
        feedback = fb;
    }

    void reset() {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    float process(float input) {
        float delayed = buffer[writePos];
        float vn = input - feedback * delayed;
        float output = delayed + feedback * vn;
        buffer[writePos] = vn;
        writePos = (writePos + 1) % length;
        return output;
    }
};

// Modulated LFO for Wow & Flutter / Drift
class DriftLFO {
private:
    float phase = 0.0f;
    float phase2 = 0.0f;
    float noiseZ = 0.0f;
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist;

public:
    DriftLFO() : rng(1337), dist(-1.0f, 1.0f) {}

    void reset() {
        phase = 0.0f;
        phase2 = 0.0f;
        noiseZ = 0.0f;
    }

    float process(float sampleRate, float driftAmt) {
        if (driftAmt <= 0.001f) return 0.0f;
        // Capstan wow (~0.35 Hz) + scrape flutter (~4.2 Hz) + random brownian drift
        phase += TWO_PI * (0.35f / sampleRate);
        if (phase > TWO_PI) phase -= TWO_PI;

        phase2 += TWO_PI * (4.2f / sampleRate);
        if (phase2 > TWO_PI) phase2 -= TWO_PI;

        // Brownian noise filter
        float white = dist(rng);
        noiseZ += 0.002f * (white - noiseZ);

        float wow = std::sin(phase) * 0.0012f;
        float flutter = std::sin(phase2) * 0.0004f;
        float randomDrift = noiseZ * 0.0018f;

        return (wow + flutter + randomDrift) * driftAmt;
    }
};

// ==============================================================================
// TAPE DELAY ENGINE
// ==============================================================================
class TapeDelayEngine {
private:
    static constexpr size_t MAX_DELAY_SAMPLES = 48000 * 4; // up to ~4 seconds
    std::vector<float> bufferL;
    std::vector<float> bufferR;
    size_t writePos = 0;
    float sampleRate = 48000.0f;

    DriftLFO lfo;
    OnePoleFilter hpFilterL, hpFilterR;
    OnePoleFilter lpFilterL, lpFilterR;

    float currentDelayL = 0.4f;
    float currentDelayR = 0.4f;

public:
    TapeDelayEngine() {
        bufferL.assign(MAX_DELAY_SAMPLES, 0.0f);
        bufferR.assign(MAX_DELAY_SAMPLES, 0.0f);
    }

    void init(float sr) {
        sampleRate = sr > 0.0f ? sr : 48000.0f;
        size_t maxBuf = static_cast<size_t>(sampleRate * 4.0f);
        bufferL.assign(maxBuf, 0.0f);
        bufferR.assign(maxBuf, 0.0f);
        writePos = 0;
        lfo.reset();
        hpFilterL.reset(); hpFilterR.reset();
        lpFilterL.reset(); lpFilterR.reset();
    }

    void process(float inL, float inR,
                 float& outL, float& outR,
                 bool active, float dryWet, float delayTimeSec,
                 float feedback, float cutLow, float cutHi,
                 float saturation, float drift, int mode)
    {
        if (!active) {
            outL = inL;
            outR = inR;
            return;
        }

        // Apply Drift modulation
        float driftMod = lfo.process(sampleRate, drift);
        float targetDelay = clamp(delayTimeSec * (1.0f + driftMod), 0.01f, 3.5f);
        float targetSamples = targetDelay * sampleRate;

        // Smooth delay time transitions (tape speed inertia)
        currentDelayL += 0.001f * (targetSamples - currentDelayL);
        currentDelayR += 0.001f * (targetSamples - currentDelayR);

        // Calculate read positions
        float rPosL = static_cast<float>(writePos) - currentDelayL;
        while (rPosL < 0.0f) rPosL += bufferL.size();
        while (rPosL >= bufferL.size()) rPosL -= bufferL.size();

        float rPosR = rPosL;
        if (mode == 1) { // Ping-Pong / Back-and-Forth: offset right delay
            rPosR = static_cast<float>(writePos) - (currentDelayR * 0.75f);
            while (rPosR < 0.0f) rPosR += bufferR.size();
            while (rPosR >= bufferR.size()) rPosR -= bufferR.size();
        }

        // Interpolated read
        auto readHermite = [](const std::vector<float>& buf, float pos) {
            int i1 = static_cast<int>(pos);
            float frac = pos - static_cast<float>(i1);
            int sz = static_cast<int>(buf.size());
            int i0 = (i1 - 1 + sz) % sz;
            int i2 = (i1 + 1) % sz;
            int i3 = (i1 + 2) % sz;
            return hermiteInterpolate(buf[i0], buf[i1], buf[i2], buf[i3], frac);
        };

        float delayedL = readHermite(bufferL, rPosL);
        float delayedR = readHermite(bufferR, rPosR);

        // Tone filtering (Tape Low/High Cut)
        delayedL = hpFilterL.processHP(delayedL, cutLow, sampleRate);
        delayedL = lpFilterL.processLP(delayedL, cutHi, sampleRate);
        delayedR = hpFilterR.processHP(delayedR, cutLow, sampleRate);
        delayedR = lpFilterR.processLP(delayedR, cutHi, sampleRate);

        // Tape Saturation & Warmth
        delayedL = tapeSaturate(delayedL, saturation);
        delayedR = tapeSaturate(delayedR, saturation);

        // Feedback calculation
        float fb = clamp(feedback, 0.0f, 1.15f); // allow self-oscillation above 1.0
        float toWriteL = inL + delayedL * fb;
        float toWriteR = inR + delayedR * fb;

        if (mode == 1) { // Ping-Pong feedback cross-feed
            float crossL = inL + delayedR * fb;
            float crossR = inR + delayedL * fb;
            toWriteL = crossL;
            toWriteR = crossR;
        }

        // Soft limit feedback to prevent digital blowup during self-oscillation
        bufferL[writePos] = std::tanh(toWriteL);
        bufferR[writePos] = std::tanh(toWriteR);

        writePos = (writePos + 1) % bufferL.size();

        // Wet / Dry blend
        float wet = dryWet * 0.01f;
        outL = inL * (1.0f - wet) + delayedL * wet;
        outR = inR * (1.0f - wet) + delayedR * wet;
    }
};

// ==============================================================================
// PHYSICAL SPRING REVERB TANK ENGINE
// ==============================================================================
class SpringReverbEngine {
private:
    static constexpr size_t NUM_SPRINGS = 4;
    static constexpr size_t AP_PER_SPRING = 8;

    std::array<std::array<AllPassFilter, AP_PER_SPRING>, NUM_SPRINGS> allpasses;
    std::array<std::vector<float>, NUM_SPRINGS> delayTanks;
    std::array<size_t, NUM_SPRINGS> tankWritePos{};
    std::array<size_t, NUM_SPRINGS> tankLengths{};

    std::array<OnePoleFilter, NUM_SPRINGS> dampFilters;
    std::array<OnePoleFilter, NUM_SPRINGS> toneFilters;

    float sampleRate = 48000.0f;
    float lfoPhase = 0.0f;

public:
    void init(float sr) {
        sampleRate = sr > 0.0f ? sr : 48000.0f;

        // Base prime delay lengths for 4 independent physical spring coils
        const std::array<float, NUM_SPRINGS> baseDelaysMs = { 38.2f, 44.7f, 51.3f, 59.8f };

        for (size_t s = 0; s < NUM_SPRINGS; ++s) {
            size_t tankLen = static_cast<size_t>((baseDelaysMs[s] * 0.001f) * sampleRate);
            if (tankLen < 10) tankLen = 10;
            tankLengths[s] = tankLen;
            delayTanks[s].assign(tankLen, 0.0f);
            tankWritePos[s] = 0;

            // Cascade of all-pass dispersion stages for signature spring "boing"
            const std::array<float, AP_PER_SPRING> apDelaysMs = {
                1.42f, 2.87f, 4.15f, 6.78f, 9.32f, 12.14f, 15.65f, 18.91f
            };
            for (size_t a = 0; a < AP_PER_SPRING; ++a) {
                size_t apLen = static_cast<size_t>((apDelaysMs[a] * 0.001f) * sampleRate * (1.0f + s * 0.08f));
                if (apLen < 2) apLen = 2;
                allpasses[s][a].init(apLen, 0.62f);
            }

            dampFilters[s].reset();
            toneFilters[s].reset();
        }
        lfoPhase = 0.0f;
    }

    void process(float inL, float inR,
                 float& outL, float& outR,
                 bool active, float dryWet, float width,
                 float length, float decay, float damp,
                 float shape, float tone, float scatter, float chaos)
    {
        if (!active) {
            outL = inL;
            outR = inR;
            return;
        }

        // Mono sum into spring transducer
        float monoIn = 0.5f * (inL + inR);

        // Spring chaos / agitation modulation
        lfoPhase += TWO_PI * (0.8f / sampleRate);
        if (lfoPhase > TWO_PI) lfoPhase -= TWO_PI;
        float chaosMod = std::sin(lfoPhase) * (chaos * 0.003f);

        float springSumL = 0.0f;
        float springSumR = 0.0f;

        float fbDecay = clamp(decay * 0.96f, 0.0f, 0.98f);
        float dampCutoff = 16000.0f - (damp * 14000.0f); // 2kHz - 16kHz damping
        float toneFreq = clamp(tone, 80.0f, 5000.0f);
        float apFeedback = clamp(0.5f + shape * 0.35f, 0.2f, 0.88f);

        for (size_t s = 0; s < NUM_SPRINGS; ++s) {
            // Read from tank delay
            size_t rPos = tankWritePos[s];
            float tankDelayed = delayTanks[s][rPos];

            // Send through dispersion all-pass cascade
            float dispersed = monoIn + tankDelayed * fbDecay;
            for (size_t a = 0; a < AP_PER_SPRING; ++a) {
                dispersed = allpasses[s][a].process(dispersed);
            }

            // High-frequency damping
            dispersed = dampFilters[s].processLP(dispersed, dampCutoff, sampleRate);

            // Tank resonant tone shaping
            dispersed = toneFilters[s].processLP(dispersed, toneFreq, sampleRate);

            // Non-linear spring saturation
            dispersed = std::tanh(dispersed * 1.1f + chaosMod);

            // Write back to tank delay line
            delayTanks[s][tankWritePos[s]] = dispersed;
            tankWritePos[s] = (tankWritePos[s] + 1) % tankLengths[s];

            // Stereo panning of the 4 springs across the stereo field
            if (s % 2 == 0) {
                springSumL += dispersed * (1.0f + (s == 0 ? scatter * 0.3f : -scatter * 0.2f));
                springSumR += dispersed * (0.4f * scatter);
            } else {
                springSumR += dispersed * (1.0f + (s == 1 ? scatter * 0.3f : -scatter * 0.2f));
                springSumL += dispersed * (0.4f * scatter);
            }
        }

        // Apply Stereo Width
        float mid = 0.5f * (springSumL + springSumR);
        float side = 0.5f * (springSumL - springSumR) * (width * 0.01f);
        float wetL = mid + side;
        float wetR = mid - side;

        // Dry / Wet Blend
        float wetMix = dryWet * 0.01f;
        outL = inL * (1.0f - wetMix) + wetL * wetMix;
        outR = inR * (1.0f - wetMix) + wetR * wetMix;
    }
};

// ==============================================================================
// COMPLETE AELAPSE ENGINE
// ==============================================================================
class AelapseEngine {
private:
    TapeDelayEngine delay;
    SpringReverbEngine springs;

public:
    void init(float sampleRate) {
        delay.init(sampleRate);
        springs.init(sampleRate);
    }

    void process(float inL, float inR,
                 float& outL, float& outR,
                 // Delay Params
                 bool delayActive, float delayDryWet, float delaySeconds,
                 float delayFeedback, float delayCutLow, float delayCutHi,
                 float delaySaturation, float delayDrift, int delayMode,
                 // Springs Params
                 bool springsActive, float springsDryWet, float springsWidth,
                 float springsLength, float springsDecay, float springsDamp,
                 float springsShape, float springsTone, float springsScatter, float springsChaos,
                 // Master
                 float masterMix, float masterGain)
    {
        float dOutL = 0.0f, dOutR = 0.0f;
        // 1. Process Tape Delay
        delay.process(inL, inR, dOutL, dOutR,
                      delayActive, delayDryWet, delaySeconds,
                      delayFeedback, delayCutLow, delayCutHi,
                      delaySaturation, delayDrift, delayMode);

        float sOutL = 0.0f, sOutR = 0.0f;
        // 2. Feed Delay Output into Spring Reverb
        springs.process(dOutL, dOutR, sOutL, sOutR,
                        springsActive, springsDryWet, springsWidth,
                        springsLength, springsDecay, springsDamp,
                        springsShape, springsTone, springsScatter, springsChaos);

        // 3. Master Blend & Output Gain
        float gainLinear = std::pow(10.0f, masterGain * 0.05f);
        float mixNorm = masterMix * 0.01f;

        float mixedL = inL * (1.0f - mixNorm) + sOutL * mixNorm;
        float mixedR = inR * (1.0f - mixNorm) + sOutR * mixNorm;

        outL = mixedL * gainLinear;
        outR = mixedR * gainLinear;
    }
};

} // namespace aelapse
