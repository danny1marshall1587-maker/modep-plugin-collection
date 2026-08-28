#include <cstdlib>
#include <cstring>
#include <cmath>
#include "lv2/lv2.h"
#include "BluesbreakerEngine.hpp"

#define BLUESBREAKER_URI "urn:brummer:bluesbreaker"

enum PortIndex {
    PORT_AUDIO_IN  = 0,
    PORT_AUDIO_OUT = 1,
    PORT_BYPASS    = 2,
    PORT_GAIN      = 3,
    PORT_TONE      = 4,
    PORT_VOLUME    = 5
};

struct BluesbreakerLV2 {
    const float* in;
    float*       out;
    const float* bypass;
    const float* gain;
    const float* tone;
    const float* volume;

    AudioDSP::BluesbreakerEngine engine;
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

    BluesbreakerLV2* self = (BluesbreakerLV2*)std::calloc(1, sizeof(BluesbreakerLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.init(sample_rate);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    BluesbreakerLV2* self = (BluesbreakerLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN:
            self->in = (const float*)data_location;
            break;
        case PORT_AUDIO_OUT:
            self->out = (float*)data_location;
            break;
        case PORT_BYPASS:
            self->bypass = (const float*)data_location;
            break;
        case PORT_GAIN:
            self->gain = (const float*)data_location;
            break;
        case PORT_TONE:
            self->tone = (const float*)data_location;
            break;
        case PORT_VOLUME:
            self->volume = (const float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    BluesbreakerLV2* self = (BluesbreakerLV2*)instance;
    if (!self) return;
    self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    BluesbreakerLV2* self = (BluesbreakerLV2*)instance;
    if (!self || !self->out || sample_count == 0) return;

    const float* in = self->in ? self->in : self->out;
    float* out = self->out;

    bool isBypassed = (self->bypass && *self->bypass < 0.5f);

    if (isBypassed) {
        if (out != in) std::memcpy(out, in, sample_count * sizeof(float));
        return;
    }

    if (self->gain) self->engine.setGain(*self->gain);
    if (self->tone) self->engine.setTone(*self->tone);
    if (self->volume) self->engine.setVolume(*self->volume);

    self->engine.process(in, out, sample_count);
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    BluesbreakerLV2* self = (BluesbreakerLV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    BLUESBREAKER_URI,
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
