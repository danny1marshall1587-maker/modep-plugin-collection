#include <cstdlib>
#include <cstring>
#include <cmath>
#include "lv2/lv2.h"
#include "HarmonicTremoloEngine.hpp"

#define HARMONIC_TREMOLO_URI "http://moddevices.com/plugins/danny/harmonic-tremolo"

enum PortIndex {
    PORT_AUDIO_IN_L    = 0,
    PORT_AUDIO_IN_R    = 1,
    PORT_AUDIO_OUT_L   = 2,
    PORT_AUDIO_OUT_R   = 3,
    PORT_BYPASS        = 4,
    PORT_RATE          = 5,
    PORT_DEPTH         = 6,
    PORT_CROSSOVER     = 7,
    PORT_WARMTH        = 8,
    PORT_WAVEFORM      = 9,
    PORT_STEREO_PHASE  = 10,
    PORT_MIX           = 11,
    PORT_RESONANCE     = 12
};

struct HarmonicTremoloLV2 {
    // Audio port pointers
    const float* inL;
    const float* inR;
    float*       outL;
    float*       outR;

    // Control port pointers
    const float* bypass;
    const float* rate;
    const float* depth;
    const float* crossover;
    const float* warmth;
    const float* waveform;
    const float* stereo_phase;
    const float* mix;
    const float* resonance;

    // DSP Engine
    AudioDSP::HarmonicTremoloEngine engine;
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

    HarmonicTremoloLV2* self = (HarmonicTremoloLV2*)std::calloc(1, sizeof(HarmonicTremoloLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.prepare(sample_rate);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    HarmonicTremoloLV2* self = (HarmonicTremoloLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN_L:
            self->inL = (const float*)data_location;
            break;
        case PORT_AUDIO_IN_R:
            self->inR = (const float*)data_location;
            break;
        case PORT_AUDIO_OUT_L:
            self->outL = (float*)data_location;
            break;
        case PORT_AUDIO_OUT_R:
            self->outR = (float*)data_location;
            break;
        case PORT_BYPASS:
            self->bypass = (const float*)data_location;
            break;
        case PORT_RATE:
            self->rate = (const float*)data_location;
            break;
        case PORT_DEPTH:
            self->depth = (const float*)data_location;
            break;
        case PORT_CROSSOVER:
            self->crossover = (const float*)data_location;
            break;
        case PORT_WARMTH:
            self->warmth = (const float*)data_location;
            break;
        case PORT_WAVEFORM:
            self->waveform = (const float*)data_location;
            break;
        case PORT_STEREO_PHASE:
            self->stereo_phase = (const float*)data_location;
            break;
        case PORT_MIX:
            self->mix = (const float*)data_location;
            break;
        case PORT_RESONANCE:
            self->resonance = (const float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    HarmonicTremoloLV2* self = (HarmonicTremoloLV2*)instance;
    if (!self) return;
    self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    HarmonicTremoloLV2* self = (HarmonicTremoloLV2*)instance;
    if (!self || !self->outL) return;

    const float* inL = self->inL ? self->inL : self->outL;
    const float* inR = self->inR ? self->inR : inL;
    float* outL = self->outL;
    float* outR = self->outR ? self->outR : outL;

    // Check bypass (0 = bypass, 1 = active)
    bool isBypassed = (self->bypass && *self->bypass < 0.5f);

    if (isBypassed) {
        if (outL != inL) std::memcpy(outL, inL, sample_count * sizeof(float));
        if (outR != inR) std::memcpy(outR, inR, sample_count * sizeof(float));
        return;
    }

    // Update DSP parameters
    if (self->rate) self->engine.setRate(*self->rate);
    if (self->depth) self->engine.setDepth(*self->depth);
    if (self->crossover) self->engine.setCrossoverFrequency(*self->crossover);
    if (self->warmth) self->engine.setWarmth(*self->warmth);
    if (self->waveform) self->engine.setWaveform((int)(*self->waveform + 0.5f));
    if (self->stereo_phase) self->engine.setStereoPhaseOffset(*self->stereo_phase);
    if (self->mix) self->engine.setMix(*self->mix);
    if (self->resonance) self->engine.setResonanceQ(*self->resonance);

    // Process audio buffer
    for (uint32_t i = 0; i < sample_count; ++i) {
        self->engine.processSample(inL[i], inR[i], outL[i], outR[i]);
    }
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    HarmonicTremoloLV2* self = (HarmonicTremoloLV2*)instance;
    if (self) {
        std::free(self);
    }
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    HARMONIC_TREMOLO_URI,
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
