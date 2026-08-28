#include <cstdlib>
#include <cstring>
#include <cmath>
#include "lv2/lv2.h"
#include "AetherEngine.hpp"

#define AETHER_URI "http://github.com/Dougal-s/Aether"

struct AetherLV2 {
    const float* in_left;
    const float* in_right;
    float*       out_left;
    float*       out_right;

    const float* ports[53];

    AudioDSP::AetherEngine engine;
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

    AetherLV2* self = (AetherLV2*)std::calloc(1, sizeof(AetherLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.init(sample_rate);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    AetherLV2* self = (AetherLV2*)instance;
    if (!self) return;

    if (port < 53) {
        self->ports[port] = (const float*)data_location;
    }

    switch (port) {
        case 2:
            self->in_left = (const float*)data_location;
            break;
        case 3:
            self->in_right = (const float*)data_location;
            break;
        case 4:
            self->out_left = (float*)data_location;
            break;
        case 5:
            self->out_right = (float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    AetherLV2* self = (AetherLV2*)instance;
    if (!self) return;
    self->engine.reset();
}

static inline float getP(const float** ports, int idx, float defVal) {
    return (ports[idx] != nullptr) ? *ports[idx] : defVal;
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    AetherLV2* self = (AetherLV2*)instance;
    if (!self || sample_count == 0) return;

    float* outL = self->out_left;
    float* outR = self->out_right;
    if (!outL || !outR) return;

    const float* inL = self->in_left;
    const float* inR = self->in_right;

    float mixPct          = getP(self->ports, 6, 100.0f);
    float dryLevelPct     = getP(self->ports, 7, 80.0f);
    float predelayLevelPct= getP(self->ports, 8, 20.0f);
    float earlyLevelPct   = getP(self->ports, 9, 10.0f);
    float lateLevelPct    = getP(self->ports, 10, 20.0f);
    float widthPct        = getP(self->ports, 12, 100.0f);
    float predelayMs      = getP(self->ports, 13, 20.0f);

    bool  earlyLowCutOn   = getP(self->ports, 14, 0.0f) >= 0.5f;
    float earlyLowCutHz   = getP(self->ports, 15, 15.0f);
    bool  earlyHighCutOn  = getP(self->ports, 16, 0.0f) >= 0.5f;
    float earlyHighCutHz  = getP(self->ports, 17, 20000.0f);
    int   earlyStages     = static_cast<int>(getP(self->ports, 22, 7.0f) + 0.5f);
    float earlyDiffMs     = getP(self->ports, 23, 20.0f);
    float earlyModDepthMs = getP(self->ports, 24, 0.0f);
    float earlyModRateHz  = getP(self->ports, 25, 1.0f);
    float earlyFeedback   = getP(self->ports, 26, 0.7f);

    int   lateDelayCount  = static_cast<int>(getP(self->ports, 28, 3.0f) + 0.5f);
    float lateDelayMs     = getP(self->ports, 29, 100.0f);
    float lateModDepthMs  = getP(self->ports, 30, 0.2f);
    float lateModRateHz   = getP(self->ports, 31, 0.2f);
    float lateFeedback    = getP(self->ports, 32, 0.7f);

    int   lateStages      = static_cast<int>(getP(self->ports, 33, 7.0f) + 0.5f);
    float lateDiffMs      = getP(self->ports, 34, 50.0f);
    float lateDiffModDepthMs = getP(self->ports, 35, 0.2f);
    float lateDiffModRateHz  = getP(self->ports, 36, 0.5f);
    float lateDiffFeedback   = getP(self->ports, 37, 0.7f);

    bool  lateLowShelfOn  = getP(self->ports, 38, 0.0f) >= 0.5f;
    float lateLowShelfHz  = getP(self->ports, 39, 100.0f);
    float lateLowShelfDb  = getP(self->ports, 40, -2.0f);

    bool  lateHighShelfOn = getP(self->ports, 41, 0.0f) >= 0.5f;
    float lateHighShelfHz = getP(self->ports, 42, 1500.0f);
    float lateHighShelfDb = getP(self->ports, 43, -3.0f);

    bool  lateHighCutOn   = getP(self->ports, 44, 0.0f) >= 0.5f;
    float lateHighCutHz   = getP(self->ports, 45, 20000.0f);

    self->engine.process(inL, inR, outL, outR, sample_count,
                         mixPct, dryLevelPct, predelayLevelPct, earlyLevelPct, lateLevelPct,
                         widthPct, predelayMs,
                         earlyLowCutOn, earlyLowCutHz, earlyHighCutOn, earlyHighCutHz,
                         earlyStages, earlyDiffMs, earlyModDepthMs, earlyModRateHz, earlyFeedback,
                         lateDelayCount, lateDelayMs, lateModDepthMs, lateModRateHz, lateFeedback,
                         lateStages, lateDiffMs, lateDiffModDepthMs, lateDiffModRateHz, lateDiffFeedback,
                         lateLowShelfOn, lateLowShelfHz, lateLowShelfDb,
                         lateHighShelfOn, lateHighShelfHz, lateHighShelfDb,
                         lateHighCutOn, lateHighCutHz);
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    AetherLV2* self = (AetherLV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    AETHER_URI,
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
