/*
 * Cyber Cloud Bloom Mono - Ambient Swell-Diffused Pad Delay LV2 Plugin
 * Copyright (c) 2026 Cyber Audio
 */

#include "lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-cloud-bloom-mono"
#define MAX_DELAY_SEC 3.0f

enum PortIndex {
    PORT_AUDIO_IN    = 0,
    PORT_AUDIO_OUT   = 1,
    PORT_BYPASS      = 2,
    PORT_TIME        = 3,
    PORT_FEEDBACK    = 4,
    PORT_BLOOM       = 5,
    PORT_SMEAR       = 6,
    PORT_WARMTH      = 7,
    PORT_MOD         = 8,
    PORT_SHIMMER     = 9,
    PORT_HOLD        = 10,
    PORT_MIX         = 11
};

struct AllpassStage {
    float* buffer;
    int size;
    int index;
    float coeff;

    void init(int sz, float c) {
        size = std::max(1, sz);
        coeff = c;
        buffer = (float*)calloc(size, sizeof(float));
        index = 0;
    }

    void free_mem() {
        if (buffer) { free(buffer); buffer = nullptr; }
    }

    float process(float in_s) {
        if (!buffer) return in_s;
        float buf_out = buffer[index];
        float v = in_s - coeff * buf_out;
        float out_s = buf_out + coeff * v;
        buffer[index] = v;
        index = (index + 1) % size;
        return out_s;
    }

    void reset() {
        if (buffer) memset(buffer, 0, size * sizeof(float));
        index = 0;
    }
};

class CyberCloudBloomMono {
private:
    double sample_rate;
    float* delay_buffer;
    int buffer_size;
    int write_pos;

    // 8-stage cloud diffuser
    AllpassStage diffuser[8];

    float envelope_tracker;
    float swell_gain;
    float lfo_phase;
    float tone_state;
    float dc_x, dc_y;
    float smooth_delay_time;

    const float* p_in;
    float*       p_out;
    const float* p_bypass;
    const float* p_time;
    const float* p_feedback;
    const float* p_bloom;
    const float* p_smear;
    const float* p_warmth;
    const float* p_mod;
    const float* p_shimmer;
    const float* p_hold;
    const float* p_mix;

public:
    CyberCloudBloomMono(double sr) : sample_rate(sr) {
        buffer_size = (int)(MAX_DELAY_SEC * sample_rate) + 64;
        delay_buffer = (float*)calloc(buffer_size, sizeof(float));

        int prime_delays[8] = { 149, 233, 379, 563, 757, 997, 1289, 1601 };
        float scale = (float)(sample_rate / 48000.0);
        for (int i = 0; i < 8; ++i) {
            int sz = (int)(prime_delays[i] * scale);
            diffuser[i].init(sz, 0.62f);
        }
        reset();
    }

    ~CyberCloudBloomMono() {
        if (delay_buffer) free(delay_buffer);
        for (int i = 0; i < 8; ++i) diffuser[i].free_mem();
    }

    void reset() {
        if (delay_buffer) memset(delay_buffer, 0, buffer_size * sizeof(float));
        write_pos = 0;
        envelope_tracker = 0.0f;
        swell_gain = 0.0f;
        lfo_phase = 0.0f;
        tone_state = 0.0f;
        dc_x = dc_y = 0.0f;
        smooth_delay_time = 0.5f * (float)sample_rate;
        for (int i = 0; i < 8; ++i) diffuser[i].reset();
    }

    void connect_port(uint32_t port, void* data) {
        switch (port) {
            case PORT_AUDIO_IN:  p_in = (const float*)data; break;
            case PORT_AUDIO_OUT: p_out = (float*)data; break;
            case PORT_BYPASS:    p_bypass = (const float*)data; break;
            case PORT_TIME:      p_time = (const float*)data; break;
            case PORT_FEEDBACK:  p_feedback = (const float*)data; break;
            case PORT_BLOOM:     p_bloom = (const float*)data; break;
            case PORT_SMEAR:     p_smear = (const float*)data; break;
            case PORT_WARMTH:    p_warmth = (const float*)data; break;
            case PORT_MOD:       p_mod = (const float*)data; break;
            case PORT_SHIMMER:   p_shimmer = (const float*)data; break;
            case PORT_HOLD:      p_hold = (const float*)data; break;
            case PORT_MIX:       p_mix = (const float*)data; break;
            default: break;
        }
    }

    void run(uint32_t sample_count) {
        if (!p_out) return;
        const float* in = p_in ? p_in : p_out;
        float* out = p_out;

        if (p_bypass && *p_bypass < 0.5f) {
            if (out != in) memcpy(out, in, sample_count * sizeof(float));
            return;
        }

        float time_ms = p_time ? *p_time : 500.0f;
        float target_samples = (time_ms * 0.001f) * (float)sample_rate;
        float feedback = p_feedback ? (*p_feedback * 0.01f) : 0.5f;
        float bloom = p_bloom ? (*p_bloom * 0.01f) : 0.5f;
        float smear = p_smear ? (*p_smear * 0.01f) : 0.6f;
        float warmth = p_warmth ? (*p_warmth * 0.01f) : 0.5f;
        float mod_depth = p_mod ? (*p_mod * 0.01f) : 0.3f;
        bool is_hold = (p_hold && *p_hold >= 0.5f);
        float mix = p_mix ? (*p_mix * 0.01f) : 0.4f;

        float swell_attack = 1.0f - std::exp(-1.0f / (std::max(0.01f, bloom * 0.6f) * (float)sample_rate));
        float swell_decay  = 1.0f - std::exp(-1.0f / (0.150f * (float)sample_rate));

        float lfo_inc = (float)(2.0 * M_PI * 0.8 / sample_rate);
        float tone_cutoff = 1500.0f + (1.0f - warmth) * 10000.0f;
        float tone_alpha = 1.0f - std::exp((float)(-2.0 * M_PI * tone_cutoff / sample_rate));

        for (int i = 0; i < 8; ++i) diffuser[i].coeff = 0.3f + smear * 0.45f;

        for (uint32_t s = 0; s < sample_count; ++s) {
            float in_s = in[s];

            smooth_delay_time += (target_samples - smooth_delay_time) * 0.002f;

            // SlowGear envelope
            float in_abs = std::abs(in_s);
            if (in_abs > envelope_tracker) envelope_tracker += swell_attack * (in_abs - envelope_tracker);
            else envelope_tracker += swell_decay * (in_abs - envelope_tracker);

            float target_swell = std::clamp(envelope_tracker * 4.0f, 0.0f, 1.0f);
            swell_gain += (target_swell - swell_gain) * swell_attack;
            float bowed_in = in_s * swell_gain;

            // LFO tape wobble
            float mod_samples = std::sin(lfo_phase) * (mod_depth * 15.0f);
            lfo_phase += lfo_inc;
            if (lfo_phase >= (float)(2.0 * M_PI)) lfo_phase -= (float)(2.0 * M_PI);

            float rpos = (float)write_pos - (smooth_delay_time + mod_samples);
            while (rpos < 0.0f) rpos += buffer_size;
            while (rpos >= buffer_size) rpos -= buffer_size;
            int r_idx = (int)rpos;
            float delayed = delay_buffer[r_idx];

            // 8-stage diffuser
            float diff = delayed;
            for (int i = 0; i < 8; ++i) diff = diffuser[i].process(diff);

            // DC block
            float dc_clean = diff - dc_x + 0.999f * dc_y;
            dc_x = diff;
            dc_y = dc_clean;

            // Warmth tone
            tone_state += tone_alpha * (dc_clean - tone_state);
            float warm_diff = tone_state;

            // Tape saturation
            float wet_s = std::tanh(warm_diff * 0.9f);

            // Feedback logic
            float fb_coeff = is_hold ? 1.0f : feedback;
            float to_write = (is_hold ? 0.0f : bowed_in) + wet_s * fb_coeff;
            delay_buffer[write_pos] = to_write;
            write_pos = (write_pos + 1) % buffer_size;

            out[s] = in_s * (1.0f - mix * 0.5f) + wet_s * mix;
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double sample_rate, const char* bundle_path, const LV2_Feature* const* features) {
    (void)descriptor; (void)bundle_path; (void)features;
    return (LV2_Handle)new CyberCloudBloomMono(sample_rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberCloudBloomMono*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    ((CyberCloudBloomMono*)instance)->reset();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberCloudBloomMono*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance) {
    delete (CyberCloudBloomMono*)instance;
}

static const void* extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
