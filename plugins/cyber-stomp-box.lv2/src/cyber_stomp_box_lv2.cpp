#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>
#include <algorithm>

#include "lv2/core/lv2.h"
#include "lv2/atom/atom.h"
#include "lv2/atom/util.h"
#include "lv2/midi/midi.h"
#include "lv2/patch/patch.h"
#include "lv2/state/state.h"
#include "lv2/urid/urid.h"

#include "CyberStompBoxEngine.hpp"

#define CYBER_STOMP_BOX_URI  "http://moddevices.com/plugins/danny/cyber-stomp-box"
#define CYBER_STOMP_PARAM_SAMPLE "http://moddevices.com/plugins/danny/cyber-stomp-box#sample"

enum PortIndex {
    PORT_AUDIO_IN_L    = 0,
    PORT_AUDIO_IN_R    = 1,
    PORT_AUDIO_OUT_L   = 2,
    PORT_AUDIO_OUT_R   = 3,
    PORT_CONTROL       = 4,
    PORT_NOTIFY        = 5,
    PORT_BYPASS        = 6,
    PORT_TRIGGER       = 7,
    PORT_SAMPLE_SLOT   = 8,
    PORT_PITCH         = 9,
    PORT_DECAY         = 10,
    PORT_PUNCH         = 11,
    PORT_SUB_THUMP     = 12,
    PORT_TONE          = 13,
    PORT_FIXED_VEL     = 14,
    PORT_GUITAR_VOL    = 15,
    PORT_STOMP_VOL     = 16,
    PORT_OUTPUT_MODE   = 17,
    PORT_LED_ACTIVITY  = 18
};

static const char* const FACTORY_SAMPLE_PATHS[6] = {
    "C:\\Users\\danny\\Documents\\MOD Desktop\\user-files\\Audio Samples\\01_Acoustic_Cajon.wav",
    "C:\\Users\\danny\\Documents\\MOD Desktop\\user-files\\Audio Samples\\02_Deep_Wood_Stomp.wav",
    "C:\\Users\\danny\\Documents\\MOD Desktop\\user-files\\Audio Samples\\03_Punchy_Studio_Kick.wav",
    "C:\\Users\\danny\\Documents\\MOD Desktop\\user-files\\Audio Samples\\04_Vintage_Boom_Kick.wav",
    "C:\\Users\\danny\\Documents\\MOD Desktop\\user-files\\Audio Samples\\05_Foot_Tambourine.wav",
    "C:\\Users\\danny\\Documents\\MOD Desktop\\user-files\\Audio Samples\\06_Foot_Snare.wav"
};

struct CyberStompBoxLV2 {
    // Ports
    const float* inL;
    const float* inR;
    float*       outL;
    float*       outR;

    const LV2_Atom_Sequence* control_in;
    LV2_Atom_Sequence*       notify_out;

    const float* bypass;
    const float* trigger_port;
    const float* sample_slot;
    const float* pitch;
    const float* decay;
    const float* punch;
    const float* sub_thump;
    const float* tone;
    const float* fixed_vel;
    const float* guitar_vol;
    const float* stomp_vol;
    const float* output_mode;
    float*       led_activity;

    // URIDs
    LV2_URID urid_atom_Sequence;
    LV2_URID urid_atom_Object;
    LV2_URID urid_atom_Path;
    LV2_URID urid_atom_String;
    LV2_URID urid_atom_URID;
    LV2_URID urid_midi_event;
    LV2_URID urid_patch_Set;
    LV2_URID urid_patch_Get;
    LV2_URID urid_patch_property;
    LV2_URID urid_patch_value;
    LV2_URID urid_sample_param;

    // Trigger state tracking
    float lastTriggerVal;
    uint32_t samplesSinceLastTrigger;
    uint32_t samplesSinceLastMidiCc;
    uint8_t lastMidiCcVal;
    int lastSampleSlot;

    std::string currentSamplePath;
    std::string bundlePath;

    AudioDSP::CyberStompBoxEngine engine;
    double sampleRate;

    void loadSample(const std::string& path) {
        if (path.empty()) return;
        if (engine.loadSampleFile(path)) {
            currentSamplePath = path;
        }
    }
};

static LV2_Handle instantiate(
    const LV2_Descriptor*     descriptor,
    double                    sample_rate,
    const char*               bundle_path,
    const LV2_Feature* const* features)
{
    (void)descriptor;
    CyberStompBoxLV2* self = new CyberStompBoxLV2();
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->bundlePath = bundle_path ? bundle_path : "";

    self->inL = nullptr;
    self->inR = nullptr;
    self->outL = nullptr;
    self->outR = nullptr;
    self->control_in = nullptr;
    self->notify_out = nullptr;
    self->bypass = nullptr;
    self->trigger_port = nullptr;
    self->sample_slot = nullptr;
    self->pitch = nullptr;
    self->decay = nullptr;
    self->punch = nullptr;
    self->sub_thump = nullptr;
    self->tone = nullptr;
    self->fixed_vel = nullptr;
    self->guitar_vol = nullptr;
    self->stomp_vol = nullptr;
    self->output_mode = nullptr;
    self->led_activity = nullptr;

    self->lastTriggerVal = -1.0f;
    self->samplesSinceLastTrigger = 999999;
    self->samplesSinceLastMidiCc = 999999;
    self->lastMidiCcVal = 0;
    self->lastSampleSlot = -1;

    // Scan features for URID map
    const LV2_URID_Map* map = nullptr;
    for (int i = 0; features && features[i]; ++i) {
        if (!std::strcmp(features[i]->URI, LV2_URID__map)) {
            map = (const LV2_URID_Map*)features[i]->data;
            break;
        }
    }

    if (map) {
        self->urid_atom_Sequence  = map->map(map->handle, LV2_ATOM__Sequence);
        self->urid_atom_Object    = map->map(map->handle, LV2_ATOM__Object);
        self->urid_atom_Path      = map->map(map->handle, LV2_ATOM__Path);
        self->urid_atom_String    = map->map(map->handle, LV2_ATOM__String);
        self->urid_atom_URID      = map->map(map->handle, LV2_ATOM__URID);
        self->urid_midi_event     = map->map(map->handle, LV2_MIDI__MidiEvent);
        self->urid_patch_Set      = map->map(map->handle, LV2_PATCH__Set);
        self->urid_patch_Get      = map->map(map->handle, LV2_PATCH__Get);
        self->urid_patch_property = map->map(map->handle, LV2_PATCH__property);
        self->urid_patch_value    = map->map(map->handle, LV2_PATCH__value);
        self->urid_sample_param   = map->map(map->handle, CYBER_STOMP_PARAM_SAMPLE);
    }

    self->engine.init(sample_rate);

    // Load default factory sample
    self->loadSample(FACTORY_SAMPLE_PATHS[0]);
    if (self->currentSamplePath.empty()) {
        // Fallback to bundle samples directory
        std::string fallback = self->bundlePath + "/samples/01_Acoustic_Cajon.wav";
        self->loadSample(fallback);
    }

    return (LV2_Handle)self;
}

static void connect_port(
    LV2_Handle instance,
    uint32_t   port,
    void*      data)
{
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN_L:   self->inL          = (const float*)data; break;
        case PORT_AUDIO_IN_R:   self->inR          = (const float*)data; break;
        case PORT_AUDIO_OUT_L:  self->outL         = (float*)data;       break;
        case PORT_AUDIO_OUT_R:  self->outR         = (float*)data;       break;
        case PORT_CONTROL:      self->control_in   = (const LV2_Atom_Sequence*)data; break;
        case PORT_NOTIFY:       self->notify_out   = (LV2_Atom_Sequence*)data;       break;
        case PORT_BYPASS:       self->bypass       = (const float*)data; break;
        case PORT_TRIGGER:      self->trigger_port = (const float*)data; break;
        case PORT_SAMPLE_SLOT:  self->sample_slot  = (const float*)data; break;
        case PORT_PITCH:        self->pitch        = (const float*)data; break;
        case PORT_DECAY:        self->decay        = (const float*)data; break;
        case PORT_PUNCH:        self->punch        = (const float*)data; break;
        case PORT_SUB_THUMP:    self->sub_thump    = (const float*)data; break;
        case PORT_TONE:         self->tone         = (const float*)data; break;
        case PORT_FIXED_VEL:    self->fixed_vel    = (const float*)data; break;
        case PORT_GUITAR_VOL:   self->guitar_vol   = (const float*)data; break;
        case PORT_STOMP_VOL:    self->stomp_vol    = (const float*)data; break;
        case PORT_OUTPUT_MODE:  self->output_mode  = (const float*)data; break;
        case PORT_LED_ACTIVITY: self->led_activity = (float*)data;       break;
        default: break;
    }
}

static void activate(LV2_Handle instance)
{
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)instance;
    if (self) {
        self->engine.reset();
        self->lastTriggerVal = -1.0f;
        self->samplesSinceLastTrigger = 999999;
        self->samplesSinceLastMidiCc = 999999;
        self->lastMidiCcVal = 0;
        self->lastSampleSlot = -1;
    }
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)instance;
    if (!self || !self->outL) return;

    // Handle bypass
    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed) {
        if (self->inL && self->outL && self->inL != self->outL) {
            std::memcpy(self->outL, self->inL, sample_count * sizeof(float));
        } else if (!self->inL && self->outL) {
            std::memset(self->outL, 0, sample_count * sizeof(float));
        }

        if (self->inR && self->outR && self->inR != self->outR) {
            std::memcpy(self->outR, self->inR, sample_count * sizeof(float));
        } else if (!self->inR && self->outR) {
            if (self->inL) {
                std::memcpy(self->outR, self->inL, sample_count * sizeof(float));
            } else {
                std::memset(self->outR, 0, sample_count * sizeof(float));
            }
        }
        if (self->led_activity) *self->led_activity = 0.0f;
        return;
    }

    // 1. Check Sample Slot Selection (0..5 = Factory sample, 6 = Custom File)
    if (self->sample_slot) {
        int slot = static_cast<int>(*self->sample_slot + 0.5f);
        if (slot != self->lastSampleSlot) {
            if (slot >= 0 && slot < 6) {
                self->loadSample(FACTORY_SAMPLE_PATHS[slot]);
            }
            self->lastSampleSlot = slot;
        }
    }

    // 2. Update Playback Parameters
    if (self->pitch)       self->engine.setPitchSemitones(*self->pitch);
    if (self->decay)       self->engine.setDecayTime(*self->decay);
    if (self->punch)       self->engine.setPunch(*self->punch);
    if (self->sub_thump)   self->engine.setSubThumpDb(*self->sub_thump);
    if (self->tone)        self->engine.setTone(*self->tone);
    if (self->fixed_vel)   self->engine.setFixedVelocity(*self->fixed_vel >= 0.5f);
    if (self->guitar_vol)  self->engine.setGuitarGainDb(*self->guitar_vol);
    if (self->stomp_vol)   self->engine.setStompGainDb(*self->stomp_vol);
    if (self->output_mode) self->engine.setSplitOutput(*self->output_mode >= 0.5f);

    // Advance sample timers for toggle vs momentary release detection
    self->samplesSinceLastTrigger += sample_count;
    self->samplesSinceLastMidiCc += sample_count;

    const uint32_t minToggleSamples = static_cast<uint32_t>(self->sampleRate * 0.10f); // ~100ms guard

    // 3. Footswitch Control Port Trigger (MIDI Learn & GUI Stomp Pad)
    if (self->trigger_port) {
        float currentTrig = *self->trigger_port;
        if (self->lastTriggerVal < -0.5f) {
            self->lastTriggerVal = currentTrig;
        } else {
            float diff = std::abs(currentTrig - self->lastTriggerVal);
            if (diff > 0.2f) {
                // Rising edge (0 -> 1): ALWAYS trigger!
                if (currentTrig > self->lastTriggerVal) {
                    self->engine.trigger(1.0f);
                    self->samplesSinceLastTrigger = 0;
                }
                // Falling edge (1 -> 0): trigger if > 100ms (toggle switch 2nd stomp)
                else if (self->samplesSinceLastTrigger >= minToggleSamples) {
                    self->engine.trigger(1.0f);
                    self->samplesSinceLastTrigger = 0;
                }
                self->lastTriggerVal = currentTrig;
            }
        }
    }

    // 4. Parse Control Port (MIDI & Patch:Set for Sample Selection)
    if (self->control_in) {
        LV2_ATOM_SEQUENCE_FOREACH(self->control_in, ev) {
            // A. MIDI Events (Nektar Pacer Notes & CCs)
            if (ev->body.type == self->urid_midi_event && ev->body.size >= 1) {
                const uint8_t* msg = (const uint8_t*)(ev + 1);
                uint8_t status = msg[0] & 0xF0;

                // MIDI Note On
                if (status == 0x90 && ev->body.size >= 3) {
                    uint8_t vel = msg[2];
                    if (vel > 0) {
                        self->engine.trigger(static_cast<float>(vel) / 127.0f);
                    }
                }
                // MIDI CC
                else if (status == 0xB0 && ev->body.size >= 3) {
                    uint8_t val = msg[2];
                    if (val >= 64 && self->lastMidiCcVal < 64) {
                        self->engine.trigger(static_cast<float>(val) / 127.0f);
                        self->samplesSinceLastMidiCc = 0;
                    } else if (val < 64 && self->lastMidiCcVal >= 64) {
                        if (self->samplesSinceLastMidiCc >= minToggleSamples) {
                            self->engine.trigger(1.0f);
                            self->samplesSinceLastMidiCc = 0;
                        }
                    }
                    self->lastMidiCcVal = val;
                }
            }
            // B. Patch:Set (MOD-UI File Selector)
            else if (ev->body.type == self->urid_atom_Object) {
                const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;
                if (obj->body.otype == self->urid_patch_Set) {
                    const LV2_Atom* property = nullptr;
                    const LV2_Atom* value = nullptr;
                    lv2_atom_object_get(obj,
                                        self->urid_patch_property, &property,
                                        self->urid_patch_value,    &value,
                                        0);

                    if (property && value && property->type == self->urid_atom_URID) {
                        uint32_t key = ((const LV2_Atom_URID*)property)->body;
                        if (key == self->urid_sample_param) {
                            if (value->type == self->urid_atom_Path || value->type == self->urid_atom_String) {
                                const char* path = (const char*)(value + 1);
                                if (path && path[0]) {
                                    self->loadSample(path);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 5. Audio Processing
    const float* inL = self->inL;
    const float* inR = self->inR ? self->inR : inL;
    float* outL = self->outL;
    float* outR = self->outR ? self->outR : outL;

    self->engine.process(inL, inR, outL, outR, sample_count);

    // 6. LED Activity Telemetry
    if (self->led_activity) {
        *self->led_activity = self->engine.getActivityLed();
    }
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)instance;
    if (self) delete self;
}

// State Interface (save & restore loaded sample across sessions)
static LV2_State_Status state_save(
    LV2_Handle                instance,
    LV2_State_Store_Function store,
    LV2_State_Handle         handle,
    uint32_t                 flags,
    const LV2_Feature* const* features)
{
    (void)flags;
    (void)features;
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)instance;
    if (!self || self->currentSamplePath.empty()) {
        return LV2_STATE_SUCCESS;
    }

    store(handle,
          self->urid_sample_param,
          self->currentSamplePath.c_str(),
          self->currentSamplePath.length() + 1,
          self->urid_atom_Path,
          LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

    return LV2_STATE_SUCCESS;
}

static LV2_State_Status state_restore(
    LV2_Handle                  instance,
    LV2_State_Retrieve_Function retrieve,
    LV2_State_Handle            handle,
    uint32_t                    flags,
    const LV2_Feature* const*   features)
{
    (void)flags;
    (void)features;
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)instance;
    if (!self) return LV2_STATE_ERR_UNKNOWN;

    size_t size = 0;
    uint32_t type = 0;
    uint32_t val_flags = 0;
    const void* val = retrieve(handle, self->urid_sample_param, &size, &type, &val_flags);

    if (val && size > 0) {
        const char* path = (const char*)val;
        self->loadSample(path);
    }

    return LV2_STATE_SUCCESS;
}

static const void* extension_data(const char* uri)
{
    static const LV2_State_Interface state = { state_save, state_restore };
    if (!std::strcmp(uri, LV2_STATE__interface)) {
        return &state;
    }
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    CYBER_STOMP_BOX_URI,
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
