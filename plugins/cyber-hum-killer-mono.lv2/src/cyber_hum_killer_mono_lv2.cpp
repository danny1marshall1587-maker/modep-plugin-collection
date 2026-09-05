#include <cstdlib>
#include <cstring>
#include <cmath>
#include "lv2/lv2.h"
#include "CyberHumEngine.hpp"

#define CYBER_HUM_MONO_URI "http://moddevices.com/plugins/danny/cyber-hum-killer-mono"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_GRID_FREQ      = 3,
    PORT_REDUCTION      = 4,
    PORT_SMOOTH         = 5,
    PORT_LISTEN_NOISE   = 6,
    PORT_LOW_CUT        = 7,
    PORT_LEARN          = 8,
    PORT_T1             = 9,
    PORT_T2             = 10,
    PORT_T3             = 11,
    PORT_T4             = 12,
    PORT_T5             = 13,
    PORT_T6             = 14,
    PORT_T7             = 15,
    PORT_T8             = 16,
    PORT_LEARN_STATUS   = 17,
    PORT_L1             = 18,
    PORT_L2             = 19,
    PORT_L3             = 20,
    PORT_L4             = 21,
    PORT_L5             = 22,
    PORT_L6             = 23,
    PORT_L7             = 24,
    PORT_L8             = 25
};

struct CyberHumMonoLV2 {
    const float* in;
    float*       out;
    const float* bypass;
    const float* grid_freq;
    const float* reduction;
    const float* smooth;
    const float* listen_noise;
    const float* low_cut;
    const float* learn;
    const float* thresholds[8];
    float*       learn_status;
    float*       levels[8];

    AudioDSP::CyberHumEngine engine;
    double sampleRate;
    float  prevLearnVal;
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double sample_rate, const char* bundle_path, const LV2_Feature* const* features) {
    (void)descriptor; (void)bundle_path; (void)features;
    CyberHumMonoLV2* self = (CyberHumMonoLV2*)std::calloc(1, sizeof(CyberHumMonoLV2));
    if (!self) return nullptr;
    self->sampleRate = sample_rate;
    self->engine.init(sample_rate);
    self->prevLearnVal = 0.0f;
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location) {
    CyberHumMonoLV2* self = (CyberHumMonoLV2*)instance;
    if (!self) return;
    if (port >= PORT_T1 && port <= PORT_T8) { self->thresholds[port - PORT_T1] = (const float*)data_location; return; }
    if (port >= PORT_L1 && port <= PORT_L8) { self->levels[port - PORT_L1] = (float*)data_location; return; }
    switch (port) {
        case PORT_AUDIO_IN:     self->in = (const float*)data_location; break;
        case PORT_AUDIO_OUT:    self->out = (float*)data_location; break;
        case PORT_BYPASS:       self->bypass = (const float*)data_location; break;
        case PORT_GRID_FREQ:    self->grid_freq = (const float*)data_location; break;
        case PORT_REDUCTION:    self->reduction = (const float*)data_location; break;
        case PORT_SMOOTH:       self->smooth = (const float*)data_location; break;
        case PORT_LISTEN_NOISE: self->listen_noise = (const float*)data_location; break;
        case PORT_LOW_CUT:      self->low_cut = (const float*)data_location; break;
        case PORT_LEARN:        self->learn = (const float*)data_location; break;
        case PORT_LEARN_STATUS: self->learn_status = (float*)data_location; break;
        default: break;
    }
}

static void activate(LV2_Handle instance) {
    CyberHumMonoLV2* self = (CyberHumMonoLV2*)instance;
    if (self) { self->engine.reset(); self->prevLearnVal = 0.0f; }
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    CyberHumMonoLV2* self = (CyberHumMonoLV2*)instance;
    if (!self || !self->out) return;
    const float* in = self->in ? self->in : self->out;
    float* out = self->out;

    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed) {
        if (out != in) std::memcpy(out, in, sample_count * sizeof(float));
        return;
    }

    if (self->grid_freq) self->engine.setGridMode(static_cast<int>(*self->grid_freq + 0.5f));
    if (self->reduction) self->engine.setReduction(*self->reduction * 0.01f);
    if (self->smooth) self->engine.setSmooth(*self->smooth * 0.01f);
    if (self->listen_noise) self->engine.setListenHumOnly(*self->listen_noise >= 0.5f);
    if (self->low_cut) self->engine.setLowCut(*self->low_cut >= 0.5f);

    for (int b = 0; b < 8; ++b) {
        if (self->thresholds[b]) self->engine.setThreshold(b, *self->thresholds[b]);
    }

    if (self->learn) {
        float curLearn = *self->learn;
        if (curLearn >= 0.5f && self->prevLearnVal < 0.5f) self->engine.triggerLearn(true);
        self->prevLearnVal = curLearn;
    }

    self->engine.process(in, nullptr, out, nullptr, sample_count);

    if (self->learn_status) *self->learn_status = self->engine.isLearning() ? 1.0f : 0.0f;
    for (int i = 0; i < 8; ++i) {
        if (self->levels[i]) *self->levels[i] = self->engine.getBandLevel(i);
    }
}

static void deactivate(LV2_Handle instance) { (void)instance; }
static void cleanup(LV2_Handle instance) {
    CyberHumMonoLV2* self = (CyberHumMonoLV2*)instance;
    if (self) std::free(self);
}
static const void* extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor descriptor = {
    CYBER_HUM_MONO_URI,
    instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
