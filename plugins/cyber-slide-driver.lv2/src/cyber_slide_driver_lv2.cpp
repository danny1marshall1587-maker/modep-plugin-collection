/*
 * Cyber Slide Driver - LV2 Plugin
 * Joey Landreth Signature Dual-Cascaded Compression & Vocal Slide Overdrive
 *
 * Signal Chain Architecture:
 * 1. Input Tightening:
 *    - 4th-order Linkwitz-Riley / cascaded Butterworth high-pass at 80 Hz to prevent "farty" low-end.
 *    - Pre-emphasis 800 Hz vocal midrange peaking bump (horn-like formant boost).
 * 2. Dual Cascaded FET 1176 Compression:
 *    - Comp A (Transient Limiter): 4:1 ratio, 1 ms attack, 50 ms release, catches initial glass/brass clack.
 *    - Comp B (Sustain Bloom Engine): 8:1 / 12:1 switchable ratio, 10 ms attack, 420 ms release with make-up gain.
 * 3. Smooth Asymmetrical Overdrive Stage:
 *    - Hybrid Germanium / Silicon diode soft clipping in op-amp feedback loop (Klon / Dumble style).
 *    - Dynamic clean headroom blend: lets pure pick attack through while sustaining body saturates smoothly.
 * 4. Post-Drive Smoothing & De-Harsh Polish:
 *    - Active variable low-pass tone control (2.2 kHz to 8.5 kHz).
 *    - 3.2 kHz anti-harsh notch filter (strips slide ice-pick resonance).
 * 5. Integrated 3-in-1 Master Noise Engine:
 *    - 10-band spectral phase cancellation + dynamic de-fizz + smart clean-input gate.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-slide-driver"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_COMP_A         = 3,   // Transient Limiter Peak Squash (0 - 100%)
    PORT_COMP_B         = 4,   // Sustain Bloom Engine (0 - 100%)
    PORT_DRIVE          = 5,   // Smooth Overdrive (0 - 100%)
    PORT_CLEAN          = 6,   // Clean Headroom Blend (0 - 100%)
    PORT_TONE           = 7,   // Post High-Cut Tone (2.2k - 8.5kHz) (0 - 100%)
    PORT_DE_ICE         = 8,   // 3.2 kHz Anti-Harsh Slide Notch (0 - 100%)
    PORT_MID_BOOST      = 9,   // 800 Hz Vocal Formant Boost (0 - 100%)
    PORT_BASS_TIGHT     = 10,  // 80 Hz Sub-Bass Tightening (0 - 100%)
    PORT_OUTPUT         = 11,  // Master Volume (-18 dB to +18 dB) (0 - 100%)
    PORT_RATIO_B        = 12,  // Comp B Ratio (0 = 8:1, 1 = 12:1)
    PORT_ZERO_NOISE     = 13,  // Master 3-in-1 Noise Engine (0 / 1)
    PORT_CLEAN_MODE     = 14,  // Pure Clean Dual-FET Compression Mode (0 / 1)
    PORT_COUNT          = 15
};

// ─── One-Pole Filter ────────────────────────────────────────────────────────
struct OnePole {
    float z1 = 0.0f;
    inline float lp(float x, float fc, float sr) {
        float w  = 2.0f * (float)M_PI * fc / sr;
        float a0 = w / (1.0f + w);
        z1 += a0 * (x - z1);
        return z1;
    }
    inline float hp(float x, float fc, float sr) {
        return x - lp(x, fc, sr);
    }
    void reset() { z1 = 0.0f; }
};

// ─── Direct Form II Transposed Biquad Filter ────────────────────────────────
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;

    void reset() { s1 = s2 = 0.0f; }

    inline float process(float in) {
        float out = b0 * in + s1;
        s1 = b1 * in - a1 * out + s2;
        s2 = b2 * in - a2 * out;
        return out;
    }

    void setHighPass(float fc, float Q, float sr) {
        float w0 = 2.0f * (float)M_PI * fc / sr;
        float alpha = sinf(w0) / (2.0f * Q);
        float cosw = cosf(w0);
        float a0 = 1.0f + alpha;
        b0 = ((1.0f + cosw) * 0.5f) / a0;
        b1 = (-(1.0f + cosw)) / a0;
        b2 = ((1.0f + cosw) * 0.5f) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setPeaking(float fc, float gainDB, float Q, float sr) {
        float A = powf(10.0f, gainDB / 40.0f);
        float w0 = 2.0f * (float)M_PI * fc / sr;
        float alpha = sinf(w0) / (2.0f * Q);
        float cosw = cosf(w0);
        float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosw) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    void setLowPass(float fc, float Q, float sr) {
        float w0 = 2.0f * (float)M_PI * fc / sr;
        float alpha = sinf(w0) / (2.0f * Q);
        float cosw = cosf(w0);
        float a0 = 1.0f + alpha;
        b0 = ((1.0f - cosw) * 0.5f) / a0;
        b1 = (1.0f - cosw) / a0;
        b2 = ((1.0f - cosw) * 0.5f) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setNotch(float fc, float depthDB, float Q, float sr) {
        setPeaking(fc, -fabsf(depthDB), Q, sr);
    }
};

// ─── FET 1176 Style Compressor Engine ────────────────────────────────────────
struct FETCompressor {
    float env = 0.0f;
    float gain = 1.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;

    void init(float atkMs, float relMs, float sr) {
        attackCoeff = 1.0f - expf(-1.0f / (sr * (atkMs * 0.001f)));
        releaseCoeff = 1.0f - expf(-1.0f / (sr * (relMs * 0.001f)));
        env = 0.0f;
        gain = 1.0f;
    }

    void reset() {
        env = 0.0f;
        gain = 1.0f;
    }

    inline float process(float in, float driveAmount, float ratio, float makeupGain) {
        float driven = in * (1.0f + driveAmount * 4.5f);
        float absX = fabsf(driven);

        if (absX > env) {
            env += attackCoeff * (absX - env);
        } else {
            env += releaseCoeff * (absX - env);
        }

        const float threshold = 0.08f; // ~ -22 dBFS
        float targetGain = 1.0f;
        if (env > threshold) {
            float excess = env / threshold;
            targetGain = powf(excess, (1.0f / ratio) - 1.0f);
        }

        gain += (targetGain - gain) * 0.05f;
        return driven * gain * makeupGain;
    }
};

// ─── Main Plugin Class ──────────────────────────────────────────────────────
class CyberSlideDriver {
public:
    CyberSlideDriver(double rate) : sampleRate(rate > 1000.0 ? rate : 48000.0) {
        const float sr = (float)sampleRate;

        // 1. Input Filters: 4th-order HP at 82 Hz
        hipass1.setHighPass(82.0f, 0.707f, sr);
        hipass2.setHighPass(82.0f, 0.707f, sr);
        midVocalBump.setPeaking(800.0f, 0.0f, 1.3f, sr);

        // 2. Dual Cascaded 1176 FET Compressors
        compA.init(1.0f, 50.0f, sr);
        compB.init(10.0f, 420.0f, sr);

        // 3. Post-Drive Shaping
        toneLp.setLowPass(5000.0f, 0.707f, sr);
        deIceNotch.setNotch(3200.0f, 0.0f, 2.8f, sr);
        dcBlocker.reset();

        // 4. 10-Band Spectral Phase-Cancellation De-Noise Engine
        specAttCoeff = 1.0f - expf(-1.0f / (0.0001f * sr));
        specLevelAtt = 1.0f - expf(-1.0f / (0.0020f * sr));
        specLevelRel = 1.0f - expf(-1.0f / (0.0600f * sr));

        const float multi[9] = { 60.0f, 150.0f, 400.0f, 800.0f, 1500.0f, 3000.0f, 5000.0f, 8000.0f, 12000.0f };
        for (int i = 0; i < 9; ++i) {
            specLpStates[i] = 0.0f;
            float w = 2.0f * (float)M_PI * multi[i] / sr;
            specBCoeffs[i] = std::min(0.99f, 1.0f - expf(-w));
        }

        const float defaultThreshDb[10] = {
            -72.0f, -70.0f, -68.0f, -65.0f, -63.0f,
            -60.0f, -57.0f, -55.0f, -54.0f, -53.0f
        };
        for (int i = 0; i < 10; ++i) {
            specLevelStates[i] = 0.0f;
            specCurrentGains[i] = 1.0f;
            specThresholdsDb[i] = defaultThreshDb[i];
            specTLinear[i] = powf(10.0f, specThresholdsDb[i] * 0.05f);
        }

        // Dynamic De-Fizz
        defizzEnv = 0.0f;
        defizzLpState = 0.0f;
        defizzExpanderGain = 1.0f;
        defizzAtk = 1.0f - expf(-1.0f / (0.002f * sr));
        defizzRel = 1.0f - expf(-1.0f / (0.070f * sr));

        // Smart Gate
        gateScHp.reset();
        gateScLp.reset();
        gateEnv = 0.0f;
        gateGain = 0.0f;
        gateIsOpen = false;
        gateAtk = 1.0f - expf(-1.0f / (sr * 0.0015f));
        gateRel = 1.0f - expf(-1.0f / (sr * 0.220f));
        gateAtkSmooth = 1.0f - expf(-1.0f / (sr * 0.001f));
        gateRelSmooth = 1.0f - expf(-1.0f / (sr * 0.180f));
    }

    void reset() {
        hipass1.reset();
        hipass2.reset();
        midVocalBump.reset();
        compA.reset();
        compB.reset();
        toneLp.reset();
        deIceNotch.reset();
        dcBlocker.reset();
        gateScHp.reset();
        gateScLp.reset();
        gateEnv = 0.0f;
        gateGain = 0.0f;
        gateIsOpen = false;
        defizzEnv = 0.0f;
        defizzLpState = 0.0f;
        defizzExpanderGain = 1.0f;
        for (int i = 0; i < 9; ++i) specLpStates[i] = 0.0f;
        for (int i = 0; i < 10; ++i) {
            specLevelStates[i] = 0.0f;
            specCurrentGains[i] = 1.0f;
        }
    }

    // Hybrid Germanium / Silicon Soft-Clipping Saturation
    inline float softClipHybrid(float x, float drive) {
        float gainFactor = 1.0f + drive * 5.5f;
        float xIn = x * gainFactor;

        // Asymmetrical soft-clipping: Germanium soft round positive lobe, Silicon firm negative lobe
        float out;
        if (xIn >= 0.0f) {
            out = tanhf(xIn * 1.15f) * 0.85f;
        } else {
            float neg = -xIn;
            out = -(tanhf(neg * 1.45f) * 0.78f);
        }
        return out;
    }

    void process(
        const float* in, float* out, uint32_t n_samples,
        float bypass, float pCompA, float pCompB, float pDrive,
        float pClean, float pTone, float pDeIce, float pMidBoost,
        float pBassTight, float pOutput, float pRatioB, float pZeroNoise,
        float pCleanMode
    ) {
        if (bypass < 0.5f) {
            if (out != in) std::memcpy(out, in, n_samples * sizeof(float));
            return;
        }

        const float sr = (float)sampleRate;

        // Parametric Filters Calibration
        float midDb = (pMidBoost / 100.0f) * 6.5f;
        midVocalBump.setPeaking(800.0f, midDb, 1.4f, sr);

        float toneNorm = std::max(0.0f, std::min(100.0f, pTone)) / 100.0f;
        float toneCutoff = 2200.0f + toneNorm * 6300.0f;
        toneLp.setLowPass(toneCutoff, 0.707f, sr);

        float deIceNorm = std::max(0.0f, std::min(100.0f, pDeIce)) / 100.0f;
        float notchDb = deIceNorm * 8.5f;
        deIceNotch.setNotch(3200.0f, notchDb, 3.2f, sr);

        float compANorm = std::max(0.0f, std::min(100.0f, pCompA)) / 100.0f;
        float compBNorm = std::max(0.0f, std::min(100.0f, pCompB)) / 100.0f;
        float driveNorm = std::max(0.0f, std::min(100.0f, pDrive)) / 100.0f;
        float cleanNorm = std::max(0.0f, std::min(100.0f, pClean)) / 100.0f;
        float bassTightNorm = std::max(0.0f, std::min(100.0f, pBassTight)) / 100.0f;

        float ratioB = (pRatioB > 0.5f) ? 12.0f : 8.0f;
        float makeupB = 1.0f + compBNorm * 1.8f;

        float outDb = ((pOutput / 100.0f) - 0.5f) * 36.0f;
        float outGain = powf(10.0f, outDb * 0.05f);

        bool useGate = (pZeroNoise > 0.5f);
        bool cleanMode = (pCleanMode > 0.5f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = in[i];
            float s = rawIn;

            // 1. Input Tightening & Voicing
            float sHp = hipass2.process(hipass1.process(s));
            s = (1.0f - bassTightNorm) * s + bassTightNorm * sHp;
            s = midVocalBump.process(s);

            // 2. Dual Cascaded FET Compression
            s = compA.process(s, compANorm, 4.0f, 1.15f);
            s = compB.process(s, compBNorm, ratioB, makeupB);

            // Clean tap before clipping
            float preClipClean = s;

            // 3. Asymmetrical Soft-Clipping Overdrive OR Pure Clean Comp
            if (cleanMode) {
                // Pure clean compression: No diode clipping saturation, crystal-clear high headroom
                s = preClipClean;
            } else {
                float driven = softClipHybrid(s, driveNorm);
                s = (1.0f - cleanNorm * 0.45f) * driven + (cleanNorm * 0.45f) * preClipClean;
            }

            // 4. Post-Drive Smoothing & De-Ice Polish
            s = toneLp.process(s);
            s = deIceNotch.process(s);
            s = dcBlocker.hp(s, 20.0f, sr);

            // 5. 3-in-1 Master Noise Engine
            if (useGate) {
                // A. 10-Band Spectral Phase-Cancellation De-Noise Engine
                float b[10];
                specLpStates[0] += (s - specLpStates[0]) * specBCoeffs[0];
                b[0] = specLpStates[0];
                for (int band = 1; band < 9; ++band) {
                    specLpStates[band] += (s - specLpStates[band]) * specBCoeffs[band];
                    b[band] = specLpStates[band] - specLpStates[band - 1];
                }
                b[9] = s - specLpStates[8];

                float spectralSum = 0.0f;
                for (int band = 0; band < 10; ++band) {
                    float absB = fabsf(b[band]);
                    float coeff = (absB > specLevelStates[band]) ? specLevelAtt : specLevelRel;
                    specLevelStates[band] += (absB - specLevelStates[band]) * coeff;
                    float r = specLevelStates[band];

                    float targetG = 1.0f - (specTLinear[band] / (r + 1e-9f));
                    targetG = std::max(0.0f, std::min(1.0f, targetG));

                    float currentCoeff;
                    if (targetG > specCurrentGains[band]) {
                        currentCoeff = specAttCoeff;
                    } else {
                        float tailStab = 1.0f + (1.0f - std::min(1.0f, specCurrentGains[band])) * 8.0f;
                        currentCoeff = 1.0f - expf(-1.0f / (0.010f * tailStab * sr));
                    }
                    specCurrentGains[band] += (targetG - specCurrentGains[band]) * currentCoeff;
                    spectralSum += b[band] * specCurrentGains[band];
                }
                s = spectralSum;

                // B. Dynamic De-Fizz (Sliding High-Cut + Smooth Downward Expander)
                float absP = fabsf(s);
                if (absP > defizzEnv) defizzEnv += defizzAtk * (absP - defizzEnv);
                else defizzEnv += defizzRel * (absP - defizzEnv);

                float normLevel = (defizzEnv - 0.0005f) / (0.0300f - 0.0005f);
                normLevel = std::max(0.0f, std::min(1.0f, normLevel));

                float dynCutoff = 2800.0f + normLevel * 11200.0f;
                float defizzW = 2.0f * (float)M_PI * dynCutoff / sr;
                float defizzAlpha = defizzW / (1.0f + defizzW);
                defizzLpState += defizzAlpha * (s - defizzLpState);
                s = defizzLpState;

                float expTarget = (normLevel > 0.15f) ? 1.0f : (normLevel / 0.15f);
                defizzExpanderGain += 0.005f * (expTarget - defizzExpanderGain);
                s *= defizzExpanderGain;

                // C. Smart Zero-Floor Clean-Input Sidechain Gate
                float sc = gateScLp.lp(gateScHp.hp(rawIn, 100.0f, sr), 3200.0f, sr);
                float absSc = fabsf(sc);
                if (absSc > gateEnv) gateEnv += gateAtk * (absSc - gateEnv);
                else gateEnv += gateRel * (absSc - gateEnv);

                const float threshOpen = 0.00100f;
                const float threshClose = 0.00045f;

                if (!gateIsOpen) {
                    if (gateEnv >= threshOpen) gateIsOpen = true;
                } else {
                    if (gateEnv < threshClose) gateIsOpen = false;
                }

                float targetGain = gateIsOpen ? 1.0f : 0.0f;
                float smoothRate = (targetGain > gateGain) ? gateAtkSmooth : gateRelSmooth;
                gateGain += smoothRate * (targetGain - gateGain);
                s *= gateGain;
            }

            s *= outGain;
            out[i] = s;
        }
    }

private:
    double sampleRate;
    Biquad hipass1, hipass2, midVocalBump;
    FETCompressor compA, compB;
    Biquad toneLp, deIceNotch;
    OnePole dcBlocker;

    // 10-Band Spectral De-Noise
    float specLpStates[9];
    float specLevelStates[10];
    float specCurrentGains[10];
    float specThresholdsDb[10];
    float specTLinear[10];
    float specBCoeffs[9];
    float specLevelAtt, specLevelRel, specAttCoeff;

    // Dynamic De-Fizz
    float defizzEnv;
    float defizzLpState;
    float defizzExpanderGain;
    float defizzAtk, defizzRel;

    // Smart Zero-Floor Gate
    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

// ─── LV2 Plugin Wrapper ──────────────────────────────────────────────────────
struct CyberSlideDriverLV2 {
    CyberSlideDriver* dsp = nullptr;
    const float* audioIn  = nullptr;
    float*       audioOut = nullptr;

    const float* pBypass    = nullptr;
    const float* pCompA     = nullptr;
    const float* pCompB     = nullptr;
    const float* pDrive     = nullptr;
    const float* pClean     = nullptr;
    const float* pTone      = nullptr;
    const float* pDeIce     = nullptr;
    const float* pMidBoost  = nullptr;
    const float* pBassTight = nullptr;
    const float* pOutput    = nullptr;
    const float* pRatioB    = nullptr;
    const float* pZeroNoise = nullptr;
    const float* pCleanMode = nullptr;

    ~CyberSlideDriverLV2() {
        if (dsp) delete dsp;
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    auto* self = new CyberSlideDriverLV2();
    self->dsp = new CyberSlideDriver(rate);
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    auto* self = (CyberSlideDriverLV2*)instance;
    if (!self) return;
    switch (port) {
        case PORT_AUDIO_IN:   self->audioIn   = (const float*)data; break;
        case PORT_AUDIO_OUT:  self->audioOut  = (float*)data;       break;
        case PORT_BYPASS:     self->pBypass   = (const float*)data; break;
        case PORT_COMP_A:     self->pCompA    = (const float*)data; break;
        case PORT_COMP_B:     self->pCompB    = (const float*)data; break;
        case PORT_DRIVE:      self->pDrive    = (const float*)data; break;
        case PORT_CLEAN:      self->pClean    = (const float*)data; break;
        case PORT_TONE:       self->pTone     = (const float*)data; break;
        case PORT_DE_ICE:     self->pDeIce    = (const float*)data; break;
        case PORT_MID_BOOST:  self->pMidBoost = (const float*)data; break;
        case PORT_BASS_TIGHT: self->pBassTight= (const float*)data; break;
        case PORT_OUTPUT:     self->pOutput   = (const float*)data; break;
        case PORT_RATIO_B:    self->pRatioB   = (const float*)data; break;
        case PORT_ZERO_NOISE: self->pZeroNoise= (const float*)data; break;
        case PORT_CLEAN_MODE: self->pCleanMode= (const float*)data; break;
        default: break;
    }
}

static void activate(LV2_Handle instance) {
    auto* self = (CyberSlideDriverLV2*)instance;
    if (self && self->dsp) self->dsp->reset();
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    auto* self = (CyberSlideDriverLV2*)instance;
    if (!self || !self->dsp || !self->audioIn || !self->audioOut) return;

    self->dsp->process(
        self->audioIn, self->audioOut, n_samples,
        self->pBypass    ? *self->pBypass    : 1.0f,
        self->pCompA     ? *self->pCompA     : 45.0f,
        self->pCompB     ? *self->pCompB     : 65.0f,
        self->pDrive     ? *self->pDrive     : 38.0f,
        self->pClean     ? *self->pClean     : 25.0f,
        self->pTone      ? *self->pTone      : 55.0f,
        self->pDeIce     ? *self->pDeIce     : 40.0f,
        self->pMidBoost  ? *self->pMidBoost  : 60.0f,
        self->pBassTight ? *self->pBassTight : 75.0f,
        self->pOutput    ? *self->pOutput    : 50.0f,
        self->pRatioB    ? *self->pRatioB    : 0.0f,
        self->pZeroNoise ? *self->pZeroNoise : 1.0f,
        self->pCleanMode ? *self->pCleanMode : 0.0f
    );
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    auto* self = (CyberSlideDriverLV2*)instance;
    if (self) delete self;
}

static const void* extension_data(const char* uri) {
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
