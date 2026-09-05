/*
 * Cyber PureSustain Delay Mono - Seamless Crossfade Pad Delay LV2 Plugin
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

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-puresustain-delay-mono"
#define MAX_DELAY_SEC 2.5f

enum PortIndex {
    PORT_AUDIO_IN      = 0,
    PORT_AUDIO_OUT     = 1,
    PORT_BYPASS        = 2,
    PORT_TIME          = 3,
    PORT_FEEDBACK      = 4,
    PORT_MELT          = 5,
    PORT_TONE          = 6,
    PORT_DUCK          = 7,
    PORT_MIX           = 8,
    PORT_MOD_ON        = 9,
    PORT_MOD_DEPTH     = 10,
    PORT_MOD_RATE      = 11,
    PORT_HP_FREQ       = 12,
    PORT_LP_FREQ       = 13,
    PORT_FILTER_Q      = 14
};

class CyberPureSustainDelayMono {
private:
    double sample_rate;
    float* delay_buf;
    int buffer_size;
    float write_pos;

    float smooth_delay_time;
    float current_feedback;
    float melt_fade;
    float lfo_phase;

    // Filter states (SVF 2-pole)
    float svf_hp_ic1, svf_hp_ic2;
    float svf_lp_ic1, svf_lp_ic2;
    float tone_filter_state;
    float dc_state_x, dc_state_y;
    float duck_envelope;

    const float* p_in;
    float*       p_out;
    const float* p_bypass;
    const float* p_time;
    const float* p_feedback;
    const float* p_melt;
    const float* p_tone;
    const float* p_duck;
    const float* p_mix;
    const float* p_mod_on;
    const float* p_mod_depth;
    const float* p_mod_rate;
    const float* p_hp_freq;
    const float* p_lp_freq;
    const float* p_filter_q;

    static inline float hermite_interpolate(float ym1, float y0, float y1, float y2, float x) {
        float c = (y1 - ym1) * 0.5f;
        float v = y0 - y1;
        float w = c + v;
        float a = w + v + (y2 - y0) * 0.5f;
        float b_neg = w + a;
        return ((((a * x) - b_neg) * x + c) * x + y0);
    }

    float read_delay(float delay_samples) {
        float rpos = write_pos - delay_samples;
        while (rpos < 0.0f) rpos += buffer_size;
        while (rpos >= buffer_size) rpos -= buffer_size;

        int i1 = (int)rpos;
        float frac = rpos - (float)i1;
        int i0 = (i1 - 1 + buffer_size) % buffer_size;
        int i2 = (i1 + 1) % buffer_size;
        int i3 = (i1 + 2) % buffer_size;

        return hermite_interpolate(delay_buf[i0], delay_buf[i1], delay_buf[i2], delay_buf[i3], frac);
    }

public:
    CyberPureSustainDelayMono(double sr) : sample_rate(sr) {
        buffer_size = (int)(MAX_DELAY_SEC * sample_rate) + 64;
        delay_buf = (float*)calloc(buffer_size, sizeof(float));
        reset();
    }

    ~CyberPureSustainDelayMono() {
        if (delay_buf) free(delay_buf);
    }

    void reset() {
        if (delay_buf) memset(delay_buf, 0, buffer_size * sizeof(float));
        write_pos = 0.0f;
        smooth_delay_time = 0.4f * (float)sample_rate;
        current_feedback = 0.4f;
        melt_fade = 0.0f;
        lfo_phase = 0.0f;
        svf_hp_ic1 = svf_hp_ic2 = 0.0f;
        svf_lp_ic1 = svf_lp_ic2 = 0.0f;
        tone_filter_state = 0.0f;
        dc_state_x = dc_state_y = 0.0f;
        duck_envelope = 0.0f;
    }

    void connect_port(uint32_t port, void* data) {
        switch (port) {
            case PORT_AUDIO_IN:  p_in = (const float*)data; break;
            case PORT_AUDIO_OUT: p_out = (float*)data; break;
            case PORT_BYPASS:    p_bypass = (const float*)data; break;
            case PORT_TIME:      p_time = (const float*)data; break;
            case PORT_FEEDBACK:  p_feedback = (const float*)data; break;
            case PORT_MELT:      p_melt = (const float*)data; break;
            case PORT_TONE:      p_tone = (const float*)data; break;
            case PORT_DUCK:      p_duck = (const float*)data; break;
            case PORT_MIX:       p_mix = (const float*)data; break;
            case PORT_MOD_ON:    p_mod_on = (const float*)data; break;
            case PORT_MOD_DEPTH: p_mod_depth = (const float*)data; break;
            case PORT_MOD_RATE:  p_mod_rate = (const float*)data; break;
            case PORT_HP_FREQ:   p_hp_freq = (const float*)data; break;
            case PORT_LP_FREQ:   p_lp_freq = (const float*)data; break;
            case PORT_FILTER_Q:  p_filter_q = (const float*)data; break;
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

        float target_time_ms = p_time ? *p_time : 400.0f;
        target_time_ms = std::clamp(target_time_ms, 10.0f, 2000.0f);
        float target_samples = (target_time_ms * 0.001f) * (float)sample_rate;

        float target_fb = p_feedback ? (*p_feedback * 0.01f) : 0.45f;
        target_fb = std::clamp(target_fb, 0.0f, 1.15f);

        float melt_amount = p_melt ? (*p_melt * 0.01f) : 0.0f;
        float tone_val = p_tone ? (*p_tone * 0.01f) : 0.6f;
        float duck_depth = p_duck ? (*p_duck * 0.01f) : 0.0f;
        float wet_mix = p_mix ? (*p_mix * 0.01f) : 0.4f;

        bool mod_enabled = (!p_mod_on || *p_mod_on >= 0.5f);
        float mod_depth_ms = (mod_enabled && p_mod_depth) ? *p_mod_depth : 0.0f;
        float mod_rate_hz = p_mod_rate ? *p_mod_rate : 1.2f;

        float hp_hz = p_hp_freq ? *p_hp_freq : 80.0f;
        float lp_hz = p_lp_freq ? *p_lp_freq : 8000.0f;
        float filter_q = p_filter_q ? *p_filter_q : 0.707f;

        float lfo_inc = (float)(2.0 * M_PI * mod_rate_hz / sample_rate);
        float mod_depth_samples = (mod_depth_ms * 0.001f) * (float)sample_rate;

        float g_hp = std::tan((float)(M_PI * hp_hz / sample_rate));
        float k_hp = 1.0f / filter_q;
        float a1_hp = 1.0f / (1.0f + g_hp * (g_hp + k_hp));
        float a2_hp = g_hp * a1_hp;
        float a3_hp = g_hp * a2_hp;

        float g_lp = std::tan((float)(M_PI * lp_hz / sample_rate));
        float a1_lp = 1.0f / (1.0f + g_lp * (g_lp + k_hp));
        float a2_lp = g_lp * a1_lp;
        float a3_lp = g_lp * a2_lp;

        float tone_cutoff = 1000.0f + tone_val * 12000.0f;
        float tone_alpha = 1.0f - std::exp((float)(-2.0 * M_PI * tone_cutoff / sample_rate));

        float duck_att = 1.0f - std::exp(-1.0f / (0.008f * (float)sample_rate));
        float duck_rel = 1.0f - std::exp(-1.0f / (0.250f * (float)sample_rate));

        for (uint32_t i = 0; i < sample_count; ++i) {
            float in_s = in[i];

            smooth_delay_time += (target_samples - smooth_delay_time) * 0.002f;
            current_feedback += (target_fb - current_feedback) * 0.001f;

            float lfo = std::sin(lfo_phase);
            lfo_phase += lfo_inc;
            if (lfo_phase >= (float)(2.0 * M_PI)) lfo_phase -= (float)(2.0 * M_PI);

            float total_delay = smooth_delay_time + lfo * mod_depth_samples;
            total_delay = std::clamp(total_delay, 10.0f, (float)(buffer_size - 10));

            float delayed = read_delay(total_delay);

            if (melt_amount > 0.001f) {
                float melt_offset = smooth_delay_time * 0.5f;
                float delayed_b = read_delay(std::clamp(total_delay + melt_offset, 10.0f, (float)(buffer_size - 10)));
                float melt_mix = 0.5f * (1.0f + std::sin(lfo_phase * 0.5f));
                delayed = (delayed * (1.0f - melt_mix * melt_amount * 0.5f)) + (delayed_b * (melt_mix * melt_amount * 0.5f));
            }

            // DC block
            float dc_clean = delayed - dc_state_x + 0.999f * dc_state_y;
            dc_state_x = delayed;
            dc_state_y = dc_clean;

            // Tone damping
            tone_filter_state += tone_alpha * (dc_clean - tone_filter_state);
            float filtered = tone_filter_state;

            // Cytomic SVF HP
            float v3_hp = filtered - svf_hp_ic2;
            float v1_hp = a1_hp * svf_hp_ic1 + a2_hp * v3_hp;
            float v2_hp = svf_hp_ic2 + a2_hp * svf_hp_ic1 + a3_hp * v3_hp;
            svf_hp_ic1 = 2.0f * v1_hp - svf_hp_ic1;
            svf_hp_ic2 = 2.0f * v2_hp - svf_hp_ic2;
            filtered = filtered - k_hp * v1_hp - v2_hp;

            // Cytomic SVF LP
            float v3_lp = filtered - svf_lp_ic2;
            float v1_lp = a1_lp * svf_lp_ic1 + a2_lp * v3_lp;
            float v2_lp = svf_lp_ic2 + a2_lp * svf_lp_ic1 + a3_lp * v3_lp;
            svf_lp_ic1 = 2.0f * v1_lp - svf_lp_ic1;
            svf_lp_ic2 = 2.0f * v2_lp - svf_lp_ic2;
            filtered = v2_lp;

            // Tape saturation
            float sat = std::tanh(filtered * 0.9f);

            // Dynamic ducking
            float in_level = std::abs(in_s);
            if (in_level > duck_envelope) duck_envelope += duck_att * (in_level - duck_envelope);
            else duck_envelope += duck_rel * (in_level - duck_envelope);

            float duck_gain = 1.0f - std::clamp(duck_envelope * duck_depth * 2.5f, 0.0f, 0.85f);
            float wet_signal = sat * duck_gain;

            // Write back to delay line
            float to_buffer = in_s + sat * current_feedback;
            int wi = (int)write_pos;
            delay_buf[wi] = to_buffer;
            write_pos = (float)((wi + 1) % buffer_size);

            out[i] = in_s * (1.0f - wet_mix * 0.5f) + wet_signal * wet_mix;
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double sample_rate, const char* bundle_path, const LV2_Feature* const* features) {
    (void)descriptor; (void)bundle_path; (void)features;
    return (LV2_Handle)new CyberPureSustainDelayMono(sample_rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberPureSustainDelayMono*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    ((CyberPureSustainDelayMono*)instance)->reset();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberPureSustainDelayMono*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance) {
    delete (CyberPureSustainDelayMono*)instance;
}

static const void* extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
