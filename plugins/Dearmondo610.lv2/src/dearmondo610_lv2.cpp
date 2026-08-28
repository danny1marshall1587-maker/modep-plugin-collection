#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "lv2/lv2.h"

#define DEARMONDO610_URI "urn:hvcc:DeArmondo610"

constexpr float TWO_PI = 6.28318530717958647692f;

enum PortIndex {
    PORT_AUDIO_IN_1  = 0,
    PORT_AUDIO_OUT_1 = 1,
    PORT_EVENTS_IN   = 2,
    PORT_EMPHASIS    = 3,
    PORT_PEDAL       = 4,
    PORT_TONESWEEP   = 5,
    PORT_VOLUMEMIN   = 6,
    PORT_CV_IN       = 7
};

class DeArmond610DSP {
public:
    DeArmond610DSP() = default;
    ~DeArmond610DSP() = default;

    void init(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() {
        mToneLowPass = 0.0f;
        mToneHighPass = 0.0f;
        mEmphasisState1 = 0.0f;
        mEmphasisState2 = 0.0f;
    }

    void process(const float* in, float* out, uint32_t numSamples,
                 float pedalPos, bool toneSweep, bool emphasis, float volumeMin, const float* cvIn)
    {
        if (!in || !out || numSamples == 0) return;

        const float sRate = static_cast<float>(mSampleRate);

        for (uint32_t s = 0; s < numSamples; ++s) {
            float effPedal = pedalPos;
            if (cvIn) {
                // CV is -5V to +5V -> normalize to -0.5 to +0.5
                effPedal += cvIn[s] * 0.1f;
            }
            effPedal = std::clamp(effPedal, 0.0f, 1.0f);

            // Logarithmic Audio Volume Taper
            // Map [0, 1] through log curve with volumeMin floor
            float logVolume = volumeMin + (1.0f - volumeMin) * (effPedal * effPedal);

            // Input Sample
            float x = in[s];

            // 1. Passive Tone Filter Sweep (350 Hz heel to 4500 Hz toe)
            if (toneSweep) {
                float cutoff = 350.0f + (4500.0f - 350.0f) * std::pow(effPedal, 1.5f);
                float coeff = 1.0f - std::exp(-TWO_PI * cutoff / sRate);
                mToneLowPass += (x - mToneLowPass) * coeff;
                
                // Tone mix blends lowpassed tone with raw signal
                x = mToneLowPass * 0.85f + x * (0.15f + 0.85f * effPedal);
            }

            // 2. Vintage DeArmond 1961 Mid/Treble Emphasis (Peaking bell around 2.2 kHz)
            if (emphasis) {
                float empCutoff = 2200.0f;
                float empCoeff = 1.0f - std::exp(-TWO_PI * empCutoff / sRate);
                mEmphasisState1 += (x - mEmphasisState1) * empCoeff;
                mEmphasisState2 += (mEmphasisState1 - mEmphasisState2) * empCoeff;
                float band = mEmphasisState1 - mEmphasisState2;
                x += band * 1.25f; // Add harmonic bite
            }

            // 3. Output Gain with subtle vintage output transformer saturation
            float wet = x * logVolume;
            out[s] = std::tanh(wet * 1.1f);
        }
    }

private:
    double mSampleRate = 48000.0;
    float mToneLowPass = 0.0f;
    float mToneHighPass = 0.0f;
    float mEmphasisState1 = 0.0f;
    float mEmphasisState2 = 0.0f;
};

struct DeArmondo610LV2 {
    const float* in1;
    float*       out1;
    const void*  events_in;
    const float* emphasis;
    const float* pedal;
    const float* tonesweep;
    const float* volumemin;
    const float* cv_in;

    DeArmond610DSP dsp;
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

    DeArmondo610LV2* self = (DeArmondo610LV2*)std::calloc(1, sizeof(DeArmondo610LV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->dsp.init(sample_rate);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    DeArmondo610LV2* self = (DeArmondo610LV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN_1:
            self->in1 = (const float*)data_location;
            break;
        case PORT_AUDIO_OUT_1:
            self->out1 = (float*)data_location;
            break;
        case PORT_EVENTS_IN:
            self->events_in = data_location;
            break;
        case PORT_EMPHASIS:
            self->emphasis = (const float*)data_location;
            break;
        case PORT_PEDAL:
            self->pedal = (const float*)data_location;
            break;
        case PORT_TONESWEEP:
            self->tonesweep = (const float*)data_location;
            break;
        case PORT_VOLUMEMIN:
            self->volumemin = (const float*)data_location;
            break;
        case PORT_CV_IN:
            self->cv_in = (const float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    DeArmondo610LV2* self = (DeArmondo610LV2*)instance;
    if (!self) return;
    self->dsp.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    DeArmondo610LV2* self = (DeArmondo610LV2*)instance;
    if (!self || !self->out1 || sample_count == 0) return;

    const float* in1 = self->in1 ? self->in1 : self->out1;
    float* out1 = self->out1;

    float pedalPos = self->pedal ? *self->pedal : 1.0f;
    bool toneSweep = self->tonesweep ? (*self->tonesweep >= 0.5f) : true;
    bool emphasis = self->emphasis ? (*self->emphasis >= 0.5f) : true;
    float volMin = self->volumemin ? *self->volumemin : 0.0f;
    const float* cvIn = self->cv_in;

    self->dsp.process(in1, out1, sample_count, pedalPos, toneSweep, emphasis, volMin, cvIn);
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    DeArmondo610LV2* self = (DeArmondo610LV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    DEARMONDO610_URI,
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
