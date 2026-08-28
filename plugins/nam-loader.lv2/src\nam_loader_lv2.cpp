#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include "lv2/lv2.h"
#include "NamNeuralEngine.hpp"

#define NAM_LOADER_URI "http://moddevices.com/plugins/danny/nam-loader"
#define LV2_STATE__interface "http://lv2plug.in/ns/ext/state#interface"

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

// LV2 State Definitions for saving/restoring patch state
typedef void* LV2_State_Handle;
typedef enum {
    LV2_STATE_SUCCESS = 0,
    LV2_STATE_ERR_UNKNOWN = 1,
    LV2_STATE_ERR_BAD_TYPE = 2,
    LV2_STATE_ERR_BAD_FLAGS = 3,
    LV2_STATE_ERR_NO_SPACE = 4
} LV2_State_Status;

typedef LV2_State_Status (*LV2_State_Store_Function)(
    LV2_State_Handle handle,
    uint32_t         key,
    const void*      value,
    size_t           size,
    uint32_t         type,
    uint32_t         flags);

typedef const void* (*LV2_State_Retrieve_Function)(
    LV2_State_Handle handle,
    uint32_t         key,
    size_t*          size,
    uint32_t*        type,
    uint32_t*        flags);

typedef struct {
    LV2_State_Status (*save)(
        LV2_Handle                instance,
        LV2_State_Store_Function store,
        LV2_State_Handle         handle,
        uint32_t                 flags,
        const LV2_Feature* const* features);

    LV2_State_Status (*restore)(
        LV2_Handle                  instance,
        LV2_State_Retrieve_Function retrieve,
        LV2_State_Handle           handle,
        uint32_t                   flags,
        const LV2_Feature* const*   features);
} LV2_State_Interface;

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
    int savedProfileId;
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
    self->savedProfileId = 8; // Default to JCM800
    self->engine.init(sample_rate);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN:   self->in = (const float*)data_location; break;
        case PORT_AUDIO_OUT:  self->out = (float*)data_location; break;
        case PORT_BYPASS:     self->bypass = (const float*)data_location; break;
        case PORT_PROFILE:    self->profile = (const float*)data_location; break;
        case PORT_DRIVE:      self->drive = (const float*)data_location; break;
        case PORT_BASS:       self->bass = (const float*)data_location; break;
        case PORT_MID:        self->mid = (const float*)data_location; break;
        case PORT_TREBLE:     self->treble = (const float*)data_location; break;
        case PORT_VOLUME:     self->volume = (const float*)data_location; break;
        case PORT_GATE:       self->gate = (const float*)data_location; break;
        case PORT_CAB_ENABLE: self->cab_enable = (const float*)data_location; break;
        default: break;
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

    if (self->profile) {
        int p = static_cast<int>(*self->profile + 0.5f);
        self->savedProfileId = p;
        self->engine.setProfile(p);
    }
    if (self->drive) self->engine.setDrive(*self->drive);
    if (self->bass) self->engine.setBass(*self->bass);
    if (self->mid) self->engine.setMid(*self->mid);
    if (self->treble) self->engine.setTreble(*self->treble);
    if (self->volume) self->engine.setVolume(*self->volume);
    if (self->gate) self->engine.setGateThreshold(*self->gate);
    if (self->cab_enable) self->engine.setCabEnabled(*self->cab_enable >= 0.5f);

    self->engine.process(in, out, sample_count);
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance)
{
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (self) std::free(self);
}

// --- LV2 State Interface for Saving & Restoring Loaded NAM Model with Patches ---
static LV2_State_Status state_save(LV2_Handle                instance,
                                  LV2_State_Store_Function store,
                                  LV2_State_Handle         handle,
                                  uint32_t                 flags,
                                  const LV2_Feature* const* features)
{
    (void)flags; (void)features;
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (!self || !store) return LV2_STATE_ERR_UNKNOWN;

    int32_t profileId = self->engine.getProfile();
    // Key 1: active profile ID
    store(handle, 1, &profileId, sizeof(int32_t), 0, 0);

    // Key 2: active profile Name string
    const char* name = self->engine.getCurrentProfile().name;
    if (name) {
        store(handle, 2, name, std::strlen(name) + 1, 0, 0);
    }

    return LV2_STATE_SUCCESS;
}

static LV2_State_Status state_restore(LV2_Handle                  instance,
                                     LV2_State_Retrieve_Function retrieve,
                                     LV2_State_Handle           handle,
                                     uint32_t                   flags,
                                     const LV2_Feature* const*   features)
{
    (void)flags; (void)features;
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (!self || !retrieve) return LV2_STATE_ERR_UNKNOWN;

    size_t size = 0;
    uint32_t type = 0, f = 0;
    const void* val = retrieve(handle, 1, &size, &type, &f);
    if (val && size >= sizeof(int32_t)) {
        int32_t profileId = *(const int32_t*)val;
        self->savedProfileId = profileId;
        self->engine.setProfile(profileId);
    }

    return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_interface = {
    state_save,
    state_restore
};

static const void* extension_data(const char* uri)
{
    if (uri && std::strcmp(uri, LV2_STATE__interface) == 0) {
        return &state_interface;
    }
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
