#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include "lv2/lv2.h"

#define DIMENSION_IV_URI "urn:hvcc:Dimension_IV"

constexpr float PI_FLOAT = 3.14159265358979323846f;
constexpr float TWO_PI_FLOAT = 6.28318530717958647692f;

enum PortIndex {
    PORT_AUDIO_IN_1  = 0,
    PORT_AUDIO_IN_2  = 1,
    PORT_AUDIO_OUT_1 = 2,
    PORT_AUDIO_OUT_2 = 3,
    PORT_EVENTS_IN   = 4,
    PORT_ANTIPHASE   = 5,
    PORT_DIMENSION   = 6
};

class DimensionIVDSP {
public:
    DimensionIVDSP() = default;
    ~DimensionIVDSP() = default;

    void init(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        mBufferLength = static_cast<int>(mSampleRate * 0.060);
        if (mBufferLength < 1024) mBufferLength = 1024;

        mDelayBufferL.assign(mBufferLength, 0.0f);
        mDelayBufferR.assign(mBufferLength, 0.0f);

        reset();
    }

    void reset() {
        std::fill(mDelayBufferL.begin(), mDelayBufferL.end(), 0.0f);
        std::fill(mDelayBufferR.begin(), mDelayBufferR.end(), 0.0f);
        mWriteIndex = 0;
        mLfoPhaseL = 0.0f;
        mLfoPhaseR = PI_FLOAT; // 180-degree anti-phase

        mDeEmphasisL = 0.0f;
        mDeEmphasisR = 0.0f;
        mPreEmphasisL = 0.0f;
        mPreEmphasisR = 0.0f;
    }

    void setMode(int mode) {
        switch (mode) {
            case 1: // Mode I (Subtle)
                mRate = 0.25f;
                mDepth = 0.38f;
                mBaseDelayMs = 4.8f;
                mModDepthMs = 1.2f;
                break;
            case 2: // Mode II (Lush)
                mRate = 0.50f;
                mDepth = 0.62f;
                mBaseDelayMs = 5.6f;
                mModDepthMs = 2.0f;
                break;
            case 3: // Mode III (Deep)
                mRate = 0.85f;
                mDepth = 0.82f;
                mBaseDelayMs = 6.6f;
                mModDepthMs = 2.8f;
                break;
            case 4: // Mode IV (Intense)
            default:
                mRate = 1.25f;
                mDepth = 1.00f;
                mBaseDelayMs = 7.8f;
                mModDepthMs = 3.6f;
                break;
        }
    }

    void setAntiphase(int antiphase) {
        if (antiphase == 0) mCrossAmount = 0.0f;       // Off
        else if (antiphase == 2) mCrossAmount = 0.75f; // Max
        else mCrossAmount = 0.45f;                     // Normal
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t numSamples) {
        const float sRate = static_cast<float>(mSampleRate);
        float lfoInc = (TWO_PI_FLOAT * mRate) / sRate;

        float deEmpCoeff = 1.0f - std::exp(-TWO_PI_FLOAT * 7500.0f / sRate);
        float preEmpCoeff = 1.0f - std::exp(-TWO_PI_FLOAT * 2500.0f / sRate);

        float baseDelaySamples = (mBaseDelayMs * 0.001f) * sRate;
        float modDepthSamples = (mModDepthMs * 0.001f) * sRate * mDepth;

        bool isStereoIn = (inR != nullptr && inR != inL);

        for (uint32_t s = 0; s < numSamples; ++s) {
            float dryL = inL[s];
            float dryR = isStereoIn ? inR[s] : inL[s];

            // NE570 Pre-emphasis & soft BBD saturation
            mPreEmphasisL += (dryL - mPreEmphasisL) * preEmpCoeff;
            float bbdInL = std::tanh((dryL + (dryL - mPreEmphasisL) * 0.6f) * 1.15f);

            mPreEmphasisR += (dryR - mPreEmphasisR) * preEmpCoeff;
            float bbdInR = std::tanh((dryR + (dryR - mPreEmphasisR) * 0.6f) * 1.15f);

            mDelayBufferL[mWriteIndex] = bbdInL;
            mDelayBufferR[mWriteIndex] = bbdInR;

            float lfoL = std::sin(mLfoPhaseL);
            float lfoR = std::sin(mLfoPhaseR);

            mLfoPhaseL += lfoInc;
            if (mLfoPhaseL >= TWO_PI_FLOAT) mLfoPhaseL -= TWO_PI_FLOAT;

            mLfoPhaseR += lfoInc;
            if (mLfoPhaseR >= TWO_PI_FLOAT) mLfoPhaseR -= TWO_PI_FLOAT;

            float delayL = baseDelaySamples + (lfoL * modDepthSamples);
            float delayR = baseDelaySamples + (lfoR * modDepthSamples);

            float wetL = readDelayLinear(mDelayBufferL, delayL);
            float wetR = readDelayLinear(mDelayBufferR, delayR);

            // NE570 De-emphasis
            mDeEmphasisL += (wetL - mDeEmphasisL) * deEmpCoeff;
            wetL = mDeEmphasisL;

            mDeEmphasisR += (wetR - mDeEmphasisR) * deEmpCoeff;
            wetR = mDeEmphasisR;

            // Spatial Crossfeed Matrix
            float spatialWetL = (wetL - wetR * mCrossAmount) * (1.0f / (1.0f + mCrossAmount));
            float spatialWetR = (wetR - wetL * mCrossAmount) * (1.0f / (1.0f + mCrossAmount));

            outL[s] = dryL * 0.5f + spatialWetL;
            if (outR) {
                outR[s] = dryR * 0.5f + spatialWetR;
            }

            mWriteIndex = (mWriteIndex + 1) % mBufferLength;
        }
    }

private:
    inline float readDelayLinear(const std::vector<float>& buffer, float delaySamples) const {
        float rIndex = static_cast<float>(mWriteIndex) - delaySamples;
        while (rIndex < 0.0f) rIndex += static_cast<float>(mBufferLength);

        int i0 = static_cast<int>(rIndex);
        int i1 = (i0 + 1) % mBufferLength;
        float frac = rIndex - static_cast<float>(i0);

        return buffer[i0] + frac * (buffer[i1] - buffer[i0]);
    }

    double mSampleRate = 48000.0;
    int mBufferLength = 4096;
    int mWriteIndex = 0;

    std::vector<float> mDelayBufferL;
    std::vector<float> mDelayBufferR;

    float mLfoPhaseL = 0.0f;
    float mLfoPhaseR = PI_FLOAT;

    float mDeEmphasisL = 0.0f;
    float mDeEmphasisR = 0.0f;
    float mPreEmphasisL = 0.0f;
    float mPreEmphasisR = 0.0f;

    float mRate = 0.25f;
    float mDepth = 0.38f;
    float mBaseDelayMs = 4.8f;
    float mModDepthMs = 1.2f;
    float mCrossAmount = 0.45f;
};

struct DimensionIVLV2 {
    const float* in1;
    const float* in2;
    float*       out1;
    float*       out2;
    const void*  events_in;
    const float* antiphase;
    const float* dimension;

    DimensionIVDSP dsp;
    double sampleRate;
};

static LV2_Handle instantiate(const LV2_Descriptor*     descriptor,
                             double                    sample_rate,
                             const char*               bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor;
    (void)bundle_path;
    (void)features;

    DimensionIVLV2* self = (DimensionIVLV2*)std::calloc(1, sizeof(DimensionIVLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->dsp.init(sample_rate);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    DimensionIVLV2* self = (DimensionIVLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN_1:
            self->in1 = (const float*)data_location;
            break;
        case PORT_AUDIO_IN_2:
            self->in2 = (const float*)data_location;
            break;
        case PORT_AUDIO_OUT_1:
            self->out1 = (float*)data_location;
            break;
        case PORT_AUDIO_OUT_2:
            self->out2 = (float*)data_location;
            break;
        case PORT_EVENTS_IN:
            self->events_in = data_location;
            break;
        case PORT_ANTIPHASE:
            self->antiphase = (const float*)data_location;
            break;
        case PORT_DIMENSION:
            self->dimension = (const float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    DimensionIVLV2* self = (DimensionIVLV2*)instance;
    if (!self) return;
    self->dsp.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    DimensionIVLV2* self = (DimensionIVLV2*)instance;
    if (!self || !self->out1 || sample_count == 0) return;

    const float* in1 = self->in1 ? self->in1 : self->out1;
    const float* in2 = self->in2 ? self->in2 : in1;
    float* out1 = self->out1;
    float* out2 = self->out2 ? self->out2 : out1;

    int dimVal = self->dimension ? static_cast<int>(*self->dimension + 0.5f) : 1;
    int antiVal = self->antiphase ? static_cast<int>(*self->antiphase + 0.5f) : 1;

    self->dsp.setMode(dimVal);
    self->dsp.setAntiphase(antiVal);

    self->dsp.process(in1, in2, out1, out2, sample_count);
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    DimensionIVLV2* self = (DimensionIVLV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    DIMENSION_IV_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &descriptor : nullptr;
}
