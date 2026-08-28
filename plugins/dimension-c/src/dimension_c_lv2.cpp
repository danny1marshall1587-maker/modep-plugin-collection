#include <cstdlib>
#include <cstring>
#include <cmath>
#include "lv2/lv2.h"
#include "DimensionCEngine.hpp"

#define DIMENSION_C_URI "http://moddevices.com/plugins/danny/dimension-c"

enum PortIndex {
    PORT_AUDIO_IN_L  = 0,
    PORT_AUDIO_IN_R  = 1,
    PORT_AUDIO_OUT_L = 2,
    PORT_AUDIO_OUT_R = 3,
    PORT_BYPASS      = 4,
    PORT_MODE        = 5,
    PORT_RATE        = 6,
    PORT_DEPTH       = 7,
    PORT_WIDTH       = 8,
    PORT_MIX         = 9,
    PORT_COLOR       = 10
};

struct DimensionCLV2 {
    const float* inL;
    const float* inR;
    float*       outL;
    float*       outR;

    const float* bypass;
    const float* mode;
    const float* rate;
    const float* depth;
    const float* width;
    const float* mix;
    const float* color;

    AudioDSP::DimensionCEngine engine;
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

    DimensionCLV2* self = (DimensionCLV2*)std::calloc(1, sizeof(DimensionCLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.prepare(sample_rate);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    DimensionCLV2* self = (DimensionCLV2*)instance;
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
        case PORT_MODE:
            self->mode = (const float*)data_location;
            break;
        case PORT_RATE:
            self->rate = (const float*)data_location;
            break;
        case PORT_DEPTH:
            self->depth = (const float*)data_location;
            break;
        case PORT_WIDTH:
            self->width = (const float*)data_location;
            break;
        case PORT_MIX:
            self->mix = (const float*)data_location;
            break;
        case PORT_COLOR:
            self->color = (const float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    DimensionCLV2* self = (DimensionCLV2*)instance;
    if (!self) return;
    self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    DimensionCLV2* self = (DimensionCLV2*)instance;
    if (!self || !self->outL || sample_count == 0) return;

    const float* inL = self->inL ? self->inL : self->outL;
    const float* inR = self->inR ? self->inR : inL;
    float* outL = self->outL;
    float* outR = self->outR ? self->outR : outL;

    // Apply parameters
    if (self->mode) {
        int m = static_cast<int>(*self->mode + 0.5f);
        self->engine.setMode(m);
    }
    if (self->rate) self->engine.setManualRate(*self->rate);
    if (self->depth) self->engine.setManualDepth(*self->depth);
    if (self->width) self->engine.setStereoWidth(*self->width);
    if (self->mix) self->engine.setMix(*self->mix / 100.0f);
    if (self->color) self->engine.setBbdColor(*self->color >= 0.5f);

    // Bypass check (0 = bypass, 1 = active)
    bool isBypassed = (self->bypass && *self->bypass < 0.5f);

    if (isBypassed) {
        if (outL != inL) std::memcpy(outL, inL, sample_count * sizeof(float));
        if (outR != inR) std::memcpy(outR, inR, sample_count * sizeof(float));
    } else {
        self->engine.processBlock(inL, inR, outL, outR, sample_count);
    }
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    DimensionCLV2* self = (DimensionCLV2*)instance;
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
    DIMENSION_C_URI,
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
