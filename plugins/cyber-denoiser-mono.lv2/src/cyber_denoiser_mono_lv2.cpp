#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "lv2/lv2.h"
#include "CyberDenoiserEngine.hpp"

#define CYBER_DENOISER_MONO_URI "http://moddevices.com/plugins/danny/cyber-denoiser-mono"

enum PortIndex {
    PORT_AUDIO_IN        = 0,
    PORT_AUDIO_OUT       = 1,
    PORT_BYPASS          = 2,
    PORT_T1              = 3,
    PORT_T2              = 4,
    PORT_T3              = 5,
    PORT_T4              = 6,
    PORT_T5              = 7,
    PORT_T6              = 8,
    PORT_T7              = 9,
    PORT_T8              = 10,
    PORT_T9              = 11,
    PORT_T10             = 12,
    PORT_LEARN           = 13,
    PORT_REDUCTION       = 14,
    PORT_SENSITIVITY     = 15,
    PORT_LISTEN_NOISE    = 16,
    PORT_LOW_CUT         = 17,
    PORT_LEARNING_STATUS = 18,
    PORT_L1              = 19,
    PORT_L2              = 20,
    PORT_L3              = 21,
    PORT_L4              = 22,
    PORT_L5              = 23,
    PORT_L6              = 24,
    PORT_L7              = 25,
    PORT_L8              = 26,
    PORT_L9              = 27,
    PORT_L10             = 28
};

struct CyberDenoiserMonoLV2 {
    const float* in;
    float*       out;
    const float* bypass;
    const float* t[10];
    const float* learn;
    const float* reduction;
    const float* sensitivity;
    const float* listen_noise;
    const float* low_cut;
    float*       learning_status;
    float*       l[10];

    AudioDSP::CyberDenoiserEngine engine;
    double sampleRate;
    bool prevLearnTrigger;
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double sample_rate, const char* bundle_path, const LV2_Feature* const* features) {
    (void)descriptor; (void)bundle_path; (void)features;
    CyberDenoiserMonoLV2* self = (CyberDenoiserMonoLV2*)std::calloc(1, sizeof(CyberDenoiserMonoLV2));
    if (!self) return nullptr;
    self->sampleRate = sample_rate;
    self->engine.prepare(sample_rate);
    self->prevLearnTrigger = false;
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location) {
    CyberDenoiserMonoLV2* self = (CyberDenoiserMonoLV2*)instance;
    if (!self) return;
    if (port >= PORT_T1 && port <= PORT_T10) { self->t[port - PORT_T1] = (const float*)data_location; return; }
    if (port >= PORT_L1 && port <= PORT_L10) { self->l[port - PORT_L1] = (float*)data_location; return; }
    switch (port) {
        case PORT_AUDIO_IN:        self->in = (const float*)data_location; break;
        case PORT_AUDIO_OUT:       self->out = (float*)data_location; break;
        case PORT_BYPASS:          self->bypass = (const float*)data_location; break;
        case PORT_LEARN:           self->learn = (const float*)data_location; break;
        case PORT_REDUCTION:       self->reduction = (const float*)data_location; break;
        case PORT_SENSITIVITY:     self->sensitivity = (const float*)data_location; break;
        case PORT_LISTEN_NOISE:    self->listen_noise = (const float*)data_location; break;
        case PORT_LOW_CUT:         self->low_cut = (const float*)data_location; break;
        case PORT_LEARNING_STATUS: self->learning_status = (float*)data_location; break;
        default: break;
    }
}

static void activate(LV2_Handle instance) {
    CyberDenoiserMonoLV2* self = (CyberDenoiserMonoLV2*)instance;
    if (self) { self->engine.reset(); self->prevLearnTrigger = false; }
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    CyberDenoiserMonoLV2* self = (CyberDenoiserMonoLV2*)instance;
    if (!self || !self->out) return;
    const float* in = self->in ? self->in : self->out;
    float* out = self->out;

    if (self->learn) {
        bool learnVal = (*self->learn > 0.5f);
        if (learnVal && !self->prevLearnTrigger) self->engine.startLearn();
        self->prevLearnTrigger = learnVal;
    }

    for (int i = 0; i < 10; ++i) {
        if (self->t[i]) self->engine.setThresholdDb(i, *self->t[i]);
    }
    if (self->reduction) self->engine.setReductionAmount(*self->reduction);
    if (self->sensitivity) self->engine.setThresholdOffsetDb(*self->sensitivity);
    if (self->listen_noise) self->engine.setListenNoise(*self->listen_noise > 0.5f);
    if (self->low_cut) self->engine.setLowCut(*self->low_cut > 0.5f);

    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed) {
        if (out != in) std::memcpy(out, in, sample_count * sizeof(float));
    } else {
        self->engine.processBlock(in, nullptr, out, nullptr, sample_count);
    }

    if (self->learning_status) *self->learning_status = self->engine.isLearning() ? 1.0f : 0.0f;
    for (int i = 0; i < 10; ++i) {
        if (self->l[i]) *self->l[i] = std::min(1.0f, self->engine.getBandLevel(i) * 4.0f);
    }
}

static void deactivate(LV2_Handle instance) { (void)instance; }
static void cleanup(LV2_Handle instance) {
    CyberDenoiserMonoLV2* self = (CyberDenoiserMonoLV2*)instance;
    if (self) std::free(self);
}
static const void* extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor descriptor = {
    CYBER_DENOISER_MONO_URI,
    instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
