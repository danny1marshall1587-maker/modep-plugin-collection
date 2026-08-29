#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "lv2/lv2.h"

#define REV_CC_CV_URI "http://moddevices.com/plugins/danny/rev-cc-cv"

enum PortIndex {
    PORT_MIDI_IN       = 0,
    PORT_CV_OUT_MAIN   = 1,
    PORT_CV_OUT_REV    = 2,
    PORT_CV_OUT_BIPOLAR= 3,
    PORT_CV_OUT_NORM   = 4,
    PORT_DIRECTION     = 5, // 0: Normal, 1: Reverse
    PORT_LEVEL         = 6, // 0..100% (or MIDI-learnable manual knob)
    PORT_CC_NUM        = 7, // 0..127 (default 7: Expression)
    PORT_MIDI_CH       = 8, // 0: Omni, 1..16
    PORT_MIN_VOLTS     = 9, // -10.0 .. +10.0V (default 0.0)
    PORT_MAX_VOLTS     = 10,// -10.0 .. +10.0V (default 10.0)
    PORT_CURVE         = 11,// 0: Linear, 1: Log, 2: Exp, 3: S-Curve
    PORT_SMOOTHING     = 12,// 0 .. 200 ms (default 10ms)
    PORT_OFFSET        = 13 // -5.0 .. +5.0V (default 0.0)
};

// LV2 Atom Sequence Structs for MIDI
struct LV2_Atom {
    uint32_t size;
    uint32_t type;
};

struct LV2_Atom_Event {
    union {
        int64_t frames;
        double  beats;
    } time;
    LV2_Atom body;
};

struct LV2_Atom_Sequence_Body {
    uint32_t unit;
    uint32_t pad;
};

struct LV2_Atom_Sequence {
    LV2_Atom atom;
    LV2_Atom_Sequence_Body body;
};

#define LV2_ATOM_SEQUENCE_FOREACH(seq, ev) \
    for (const LV2_Atom_Event* (ev) = (const LV2_Atom_Event*)((const uint8_t*)(seq) + sizeof(LV2_Atom_Sequence)); \
         (const uint8_t*)(ev) < ((const uint8_t*)(seq) + sizeof(LV2_Atom) + (seq)->atom.size); \
         (ev) = (const LV2_Atom_Event*)((const uint8_t*)(ev) + sizeof(LV2_Atom_Event) + (((ev)->body.size + 7) & ~7)))

struct RevCcCv {
    const LV2_Atom_Sequence* midi_in;
    float* cv_out_main;
    float* cv_out_rev;
    float* cv_out_bipolar;
    float* cv_out_norm;

    const float* direction;
    const float* level;
    const float* cc_num;
    const float* midi_ch;
    const float* min_volts;
    const float* max_volts;
    const float* curve;
    const float* smoothing;
    const float* offset;

    double sampleRate;
    float currentNormVal;
    float targetNormVal;
    float lastLevelKnobVal;
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double sample_rate,
                             const char* bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;
    RevCcCv* self = (RevCcCv*)std::calloc(1, sizeof(RevCcCv));
    if (!self) return nullptr;

    self->sampleRate = sample_rate > 0.0 ? sample_rate : 48000.0;
    self->currentNormVal = 0.0f;
    self->targetNormVal = 0.0f;
    self->lastLevelKnobVal = -1.0f;

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    RevCcCv* self = (RevCcCv*)instance;
    if (!self) return;

    switch (port) {
        case PORT_MIDI_IN:        self->midi_in        = (const LV2_Atom_Sequence*)data_location; break;
        case PORT_CV_OUT_MAIN:    self->cv_out_main    = (float*)data_location; break;
        case PORT_CV_OUT_REV:     self->cv_out_rev     = (float*)data_location; break;
        case PORT_CV_OUT_BIPOLAR: self->cv_out_bipolar = (float*)data_location; break;
        case PORT_CV_OUT_NORM:    self->cv_out_norm    = (float*)data_location; break;
        case PORT_DIRECTION:      self->direction      = (const float*)data_location; break;
        case PORT_LEVEL:          self->level          = (const float*)data_location; break;
        case PORT_CC_NUM:         self->cc_num         = (const float*)data_location; break;
        case PORT_MIDI_CH:        self->midi_ch        = (const float*)data_location; break;
        case PORT_MIN_VOLTS:      self->min_volts      = (const float*)data_location; break;
        case PORT_MAX_VOLTS:      self->max_volts      = (const float*)data_location; break;
        case PORT_CURVE:          self->curve          = (const float*)data_location; break;
        case PORT_SMOOTHING:      self->smoothing      = (const float*)data_location; break;
        case PORT_OFFSET:         self->offset         = (const float*)data_location; break;
        default: break;
    }
}

static void activate(LV2_Handle instance)
{
    RevCcCv* self = (RevCcCv*)instance;
    if (!self) return;
    self->currentNormVal = 0.0f;
    self->targetNormVal = 0.0f;
    self->lastLevelKnobVal = -1.0f;
}

static inline float apply_curve(float norm_val, int curve_type) {
    norm_val = std::max(0.0f, std::min(1.0f, norm_val));
    switch (curve_type) {
        case 1: // Logarithmic / Audio Taper
            return std::pow(norm_val, 2.0f);
        case 2: // Exponential / Anti-Log
            return std::sqrt(norm_val);
        case 3: // S-Curve (Smooth Hermite)
            return norm_val * norm_val * (3.0f - 2.0f * norm_val);
        case 0: // Linear
        default:
            return norm_val;
    }
}

static void run(LV2_Handle instance, uint32_t n_samples)
{
    RevCcCv* self = (RevCcCv*)instance;
    if (!self) return;

    int target_cc = self->cc_num ? (int)(*self->cc_num) : 7;
    int target_ch = self->midi_ch ? (int)(*self->midi_ch) : 0; // 0 = Omni
    bool is_reverse = self->direction ? (*self->direction >= 0.5f) : false;

    float min_v = self->min_volts ? *self->min_volts : 0.0f;
    float max_v = self->max_volts ? *self->max_volts : 10.0f;
    int curve_type = self->curve ? (int)(*self->curve) : 0;
    float smooth_ms = self->smoothing ? std::max(0.0f, *self->smoothing) : 10.0f;
    float volt_offset = self->offset ? *self->offset : 0.0f;

    // Check manual level knob change
    if (self->level) {
        float lvl = *self->level * 0.01f; // 0..100 -> 0..1
        if (std::abs(lvl - self->lastLevelKnobVal) > 0.001f) {
            self->targetNormVal = std::max(0.0f, std::min(1.0f, lvl));
            self->lastLevelKnobVal = lvl;
        }
    }

    // Process incoming MIDI CC messages
    if (self->midi_in) {
        LV2_ATOM_SEQUENCE_FOREACH(self->midi_in, ev) {
            const uint8_t* msg = (const uint8_t*)(ev + 1);
            uint8_t status = msg[0] & 0xF0;
            uint8_t channel = (msg[0] & 0x0F) + 1;

            if (status == 0xB0) { // Control Change
                uint8_t cc = msg[1];
                uint8_t val = msg[2];

                if ((target_ch == 0 || target_ch == channel) && cc == target_cc) {
                    self->targetNormVal = val / 127.0f;
                    self->lastLevelKnobVal = self->targetNormVal;
                }
            }
        }
    }

    // Calculate smoothing coefficient per sample
    float alpha = 1.0f;
    if (smooth_ms > 0.1f) {
        alpha = 1.0f - std::exp(-1.0f / (float)(self->sampleRate * (smooth_ms * 0.001f)));
        alpha = std::max(0.0001f, std::min(1.0f, alpha));
    }

    for (uint32_t s = 0; s < n_samples; ++s) {
        self->currentNormVal += alpha * (self->targetNormVal - self->currentNormVal);

        float shaped_fwd = apply_curve(self->currentNormVal, curve_type);
        float shaped_rev = 1.0f - shaped_fwd;

        // Apply direction mode to main output
        float active_norm = is_reverse ? shaped_rev : shaped_fwd;

        // 1. CV Out Main (Switched Direction)
        if (self->cv_out_main) {
            self->cv_out_main[s] = (min_v + active_norm * (max_v - min_v)) + volt_offset;
        }

        // 2. CV Out Reverse (Always Inverted)
        if (self->cv_out_rev) {
            self->cv_out_rev[s] = (min_v + shaped_rev * (max_v - min_v)) + volt_offset;
        }

        // 3. CV Out Bipolar (-5.0V to +5.0V)
        if (self->cv_out_bipolar) {
            float bip = (active_norm * 10.0f) - 5.0f;
            self->cv_out_bipolar[s] = bip + volt_offset;
        }

        // 4. CV Out Normalized (0.0 to 1.0)
        if (self->cv_out_norm) {
            self->cv_out_norm[s] = std::max(0.0f, std::min(1.0f, active_norm));
        }
    }
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    std::free(instance);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    REV_CC_CV_URI,
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
    return index == 0 ? &descriptor : nullptr;
}
