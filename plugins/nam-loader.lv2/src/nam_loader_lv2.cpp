#include <cstdlib>
#include <cstring>
#include <cmath>
#include "lv2/lv2.h"
#include "NamNeuralEngine.hpp"

#define NAM_LOADER_URI "http://moddevices.com/plugins/danny/nam-loader"

enum PortIndex {
    PORT_AUDIO_IN   = 0,
    PORT_AUDIO_OUT  = 1,
    PORT_BYPASS     = 2,
    PORT_PROFILE    = 3,
    PORT_DRIVE      = 4,
    PORT_BASS       = 5,
    PORT_MID        = 6,
    PORT_TREBLE     = 7,
    PORT_VOLUME     = 8,
    PORT_GATE       = 9,
    PORT_CAB_ENABLE = 10
};

struct NamLoaderLV2 {
    const float* in;
    float*       out;
    const float* bypass;
    const float* profile;
    const float* drive;
    const float* bass;
    const float* mid;
    const float* treble;
    const float* volume;
    const float* gate;
    const float* cab_enable;

    AudioDSP::NamNeuralEngine engine;
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

    NamLoaderLV2* self = (NamLoaderLV2*)std::calloc(1, sizeof(NamLoaderLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.init(sample_rate);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
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
        case PORT_PROFILE:
            self->profile = (const float*)data_location;
            break;
        case PORT_DRIVE:
            self->drive = (const float*)data_location;
            break;
        case PORT_BASS:
            self->bass = (const float*)data_location;
            break;
        case PORT_MID:
            self->mid = (const float*)data_location;
            break;
        case PORT_TREBLE:
            self->treble = (const float*)data_location;
            break;
        case PORT_VOLUME:
            self->volume = (const float*)data_location;
            break;
        case PORT_GATE:
            self->gate = (const float*)data_location;
            break;
        case PORT_CAB_ENABLE:
            self->cab_enable = (const float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (!self) return;
    self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (!self || !self->out || sample_count == 0) return;

    const float* in = self->in ? self->in : self->out;
    float* out = self->out;

    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed) {
        if (out != in) std::memcpy(out, in, sample_count * sizeof(float));
        return;
    }

    if (self->profile) self->engine.setProfile(static_cast<int>(*self->profile + 0.5f));
    if (self->drive) self->engine.setDrive(*self->drive);
    if (self->bass) self->engine.setBass(*self->bass);
    if (self->mid) self->engine.setMid(*self->mid);
    if (self->treble) self->engine.setTreble(*self->treble);
    if (self->volume) self->engine.setVolume(*self->volume);
    if (self->gate) self->engine.setGateThreshold(*self->gate);
    if (self->cab_enable) self->engine.setCabEnabled(*self->cab_enable >= 0.5f);

    self->engine.process(in, out, sample_count);
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    NAM_LOADER_URI,
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
