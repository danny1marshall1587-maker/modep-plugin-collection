#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "lv2/lv2.h"
#include "AelapseEngine.hpp"

#define AELAPSE_URI "http://moddevices.com/plugins/danny/aelapse"

enum PortIndex {
    PORT_IN_L             = 0,
    PORT_IN_R             = 1,
    PORT_OUT_L            = 2,
    PORT_OUT_R            = 3,

    // Delay
    PORT_DELAY_ACTIVE     = 4,
    PORT_DELAY_DRYWET     = 5,
    PORT_DELAY_TIME       = 6,
    PORT_DELAY_FEEDBACK   = 7,
    PORT_DELAY_CUT_LOW    = 8,
    PORT_DELAY_CUT_HI     = 9,
    PORT_DELAY_SATURATION = 10,
    PORT_DELAY_DRIFT      = 11,
    PORT_DELAY_MODE       = 12,

    // Springs
    PORT_SPRINGS_ACTIVE   = 13,
    PORT_SPRINGS_DRYWET   = 14,
    PORT_SPRINGS_WIDTH    = 15,
    PORT_SPRINGS_LENGTH   = 16,
    PORT_SPRINGS_DECAY    = 17,
    PORT_SPRINGS_DAMP     = 18,
    PORT_SPRINGS_SHAPE    = 19,
    PORT_SPRINGS_TONE     = 20,
    PORT_SPRINGS_SCATTER  = 21,
    PORT_SPRINGS_CHAOS    = 22,

    // Master
    PORT_MASTER_MIX       = 23,
    PORT_MASTER_GAIN      = 24
};

struct AelapseLV2 {
    const float* in_l;
    const float* in_r;
    float* out_l;
    float* out_r;

    // Delay Ports
    const float* delay_active;
    const float* delay_drywet;
    const float* delay_time;
    const float* delay_feedback;
    const float* delay_cut_low;
    const float* delay_cut_hi;
    const float* delay_saturation;
    const float* delay_drift;
    const float* delay_mode;

    // Springs Ports
    const float* springs_active;
    const float* springs_drywet;
    const float* springs_width;
    const float* springs_length;
    const float* springs_decay;
    const float* springs_damp;
    const float* springs_shape;
    const float* springs_tone;
    const float* springs_scatter;
    const float* springs_chaos;

    // Master
    const float* master_mix;
    const float* master_gain;

    aelapse::AelapseEngine engine;
    double sampleRate;
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double sample_rate,
                             const char* bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;
    AelapseLV2* self = (AelapseLV2*)std::calloc(1, sizeof(AelapseLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate > 0.0 ? sample_rate : 48000.0;
    self->engine.init(static_cast<float>(self->sampleRate));

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    AelapseLV2* self = (AelapseLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_IN_L:             self->in_l             = (const float*)data_location; break;
        case PORT_IN_R:             self->in_r             = (const float*)data_location; break;
        case PORT_OUT_L:            self->out_l            = (float*)data_location; break;
        case PORT_OUT_R:            self->out_r            = (float*)data_location; break;

        case PORT_DELAY_ACTIVE:     self->delay_active     = (const float*)data_location; break;
        case PORT_DELAY_DRYWET:     self->delay_drywet     = (const float*)data_location; break;
        case PORT_DELAY_TIME:       self->delay_time       = (const float*)data_location; break;
        case PORT_DELAY_FEEDBACK:   self->delay_feedback   = (const float*)data_location; break;
        case PORT_DELAY_CUT_LOW:    self->delay_cut_low    = (const float*)data_location; break;
        case PORT_DELAY_CUT_HI:     self->delay_cut_hi     = (const float*)data_location; break;
        case PORT_DELAY_SATURATION: self->delay_saturation = (const float*)data_location; break;
        case PORT_DELAY_DRIFT:      self->delay_drift      = (const float*)data_location; break;
        case PORT_DELAY_MODE:       self->delay_mode       = (const float*)data_location; break;

        case PORT_SPRINGS_ACTIVE:   self->springs_active   = (const float*)data_location; break;
        case PORT_SPRINGS_DRYWET:   self->springs_drywet   = (const float*)data_location; break;
        case PORT_SPRINGS_WIDTH:    self->springs_width    = (const float*)data_location; break;
        case PORT_SPRINGS_LENGTH:   self->springs_length   = (const float*)data_location; break;
        case PORT_SPRINGS_DECAY:    self->springs_decay    = (const float*)data_location; break;
        case PORT_SPRINGS_DAMP:     self->springs_damp     = (const float*)data_location; break;
        case PORT_SPRINGS_SHAPE:    self->springs_shape    = (const float*)data_location; break;
        case PORT_SPRINGS_TONE:     self->springs_tone     = (const float*)data_location; break;
        case PORT_SPRINGS_SCATTER:  self->springs_scatter  = (const float*)data_location; break;
        case PORT_SPRINGS_CHAOS:    self->springs_chaos    = (const float*)data_location; break;

        case PORT_MASTER_MIX:       self->master_mix       = (const float*)data_location; break;
        case PORT_MASTER_GAIN:      self->master_gain      = (const float*)data_location; break;
        default: break;
    }
}

static void activate(LV2_Handle instance)
{
    AelapseLV2* self = (AelapseLV2*)instance;
    if (!self) return;
    self->engine.init(static_cast<float>(self->sampleRate));
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    AelapseLV2* self = (AelapseLV2*)instance;
    if (!self || sample_count == 0) return;

    const float* inL = self->in_l;
    const float* inR = self->in_r ? self->in_r : self->in_l;
    float* outL = self->out_l;
    float* outR = self->out_r ? self->out_r : self->out_l;

    if (!inL || !outL) return;

    // Delay parameters
    bool delayActive = self->delay_active ? (*self->delay_active > 0.5f) : true;
    float delayDryWet = self->delay_drywet ? *self->delay_drywet : 50.0f;
    float delayTime = self->delay_time ? *self->delay_time : 0.35f;
    float delayFeedback = self->delay_feedback ? *self->delay_feedback : 0.45f;
    float delayCutLow = self->delay_cut_low ? *self->delay_cut_low : 80.0f;
    float delayCutHi = self->delay_cut_hi ? *self->delay_cut_hi : 4500.0f;
    float delaySaturation = self->delay_saturation ? *self->delay_saturation * 0.01f : 0.35f;
    float delayDrift = self->delay_drift ? *self->delay_drift * 0.01f : 0.25f;
    int delayMode = self->delay_mode ? static_cast<int>(*self->delay_mode + 0.5f) : 0;

    // Springs parameters
    bool springsActive = self->springs_active ? (*self->springs_active > 0.5f) : true;
    float springsDryWet = self->springs_drywet ? *self->springs_drywet : 40.0f;
    float springsWidth = self->springs_width ? *self->springs_width : 100.0f;
    float springsLength = self->springs_length ? *self->springs_length : 0.12f;
    float springsDecay = self->springs_decay ? *self->springs_decay * 0.01f : 0.65f;
    float springsDamp = self->springs_damp ? *self->springs_damp * 0.01f : 0.40f;
    float springsShape = self->springs_shape ? *self->springs_shape * 0.01f : 0.50f;
    float springsTone = self->springs_tone ? *self->springs_tone : 850.0f;
    float springsScatter = self->springs_scatter ? *self->springs_scatter * 0.01f : 0.50f;
    float springsChaos = self->springs_chaos ? *self->springs_chaos * 0.01f : 0.20f;

    // Master parameters
    float masterMix = self->master_mix ? *self->master_mix : 100.0f;
    float masterGain = self->master_gain ? *self->master_gain : 0.0f;

    for (uint32_t s = 0; s < sample_count; ++s) {
        float xL = inL[s];
        float xR = inR ? inR[s] : xL;
        float yL = 0.0f, yR = 0.0f;

        self->engine.process(xL, xR, yL, yR,
                             delayActive, delayDryWet, delayTime,
                             delayFeedback, delayCutLow, delayCutHi,
                             delaySaturation, delayDrift, delayMode,
                             springsActive, springsDryWet, springsWidth,
                             springsLength, springsDecay, springsDamp,
                             springsShape, springsTone, springsScatter, springsChaos,
                             masterMix, masterGain);

        outL[s] = yL;
        if (outR && outR != outL) outR[s] = yR;
    }
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance)
{
    AelapseLV2* self = (AelapseLV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor descriptor = {
    AELAPSE_URI,
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
