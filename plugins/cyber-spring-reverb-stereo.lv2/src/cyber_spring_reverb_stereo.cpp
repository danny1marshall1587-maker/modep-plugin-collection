#include <lv2.h>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>

#define PLUGIN_URI "http://cyberaudio.com/plugins/cyber-spring-reverb-stereo"

enum PortIndex {
    PORT_AUDIO_IN_L  = 0,
    PORT_AUDIO_IN_R  = 1,
    PORT_AUDIO_OUT_L = 2,
    PORT_AUDIO_OUT_R = 3,
    PORT_DWELL       = 4,
    PORT_TONE        = 5,
    PORT_MIX         = 6,
    PORT_DRIP        = 7,
    PORT_DECAY       = 8,
    PORT_BYPASS      = 9
};

class CyberSpringReverbStereo {
private:
    const float* p_in_l;
    const float* p_in_r;
    float*       p_out_l;
    float*       p_out_r;
    const float* p_dwell;
    const float* p_tone;
    const float* p_mix;
    const float* p_drip;
    const float* p_decay;
    const float* p_bypass;

    double sample_rate;

    // 3 physical springs
    static const int NUM_SPRINGS = 3;
    static const int MAX_DELAY = 16384;
    float spring_buffers[NUM_SPRINGS][MAX_DELAY];
    int spring_delays[NUM_SPRINGS];
    int write_pos[NUM_SPRINGS];

    // Input diffusion
    static const int DIFF_DELAY1 = 128;
    static const int DIFF_DELAY2 = 192;
    float diff_buf1[DIFF_DELAY1];
    float diff_buf2[DIFF_DELAY2];
    int diff_idx1, diff_idx2;

    // 16-stage dispersive allpass chains per spring
    static const int DISP_STAGES = 16;
    float disp_x[NUM_SPRINGS][DISP_STAGES];
    float disp_y[NUM_SPRINGS][DISP_STAGES];

    // Damping and tone filters
    float lp_filter[NUM_SPRINGS];
    float out_tone_lp_l;
    float out_tone_lp_r;

    float smoothed_mix;
    float smoothed_decay;

    float process_dispersion(int sp, float input, float drip_amount) {
        float x = input;
        float a = 0.35f + 0.35f * drip_amount;
        for (int i = 0; i < DISP_STAGES; ++i) {
            float y = -a * x + disp_x[sp][i] + a * disp_y[sp][i];
            disp_x[sp][i] = x;
            disp_y[sp][i] = y;
            x = y;
        }
        return x;
    }

public:
    CyberSpringReverbStereo(double sr) : sample_rate(sr) {
        float scale = (float)(sr / 48000.0);
        spring_delays[0] = (int)(1613 * scale);
        spring_delays[1] = (int)(2137 * scale);
        spring_delays[2] = (int)(2741 * scale);
        reset();
    }

    void reset() {
        memset(spring_buffers, 0, sizeof(spring_buffers));
        memset(write_pos, 0, sizeof(write_pos));
        memset(diff_buf1, 0, sizeof(diff_buf1));
        memset(diff_buf2, 0, sizeof(diff_buf2));
        diff_idx1 = diff_idx2 = 0;
        memset(disp_x, 0, sizeof(disp_x));
        memset(disp_y, 0, sizeof(disp_y));
        memset(lp_filter, 0, sizeof(lp_filter));
        out_tone_lp_l = out_tone_lp_r = 0.0f;
        smoothed_mix = 0.35f;
        smoothed_decay = 0.85f;
    }

    void connect_port(uint32_t port, void* data) {
        switch (port) {
            case PORT_AUDIO_IN_L:  p_in_l = (const float*)data; break;
            case PORT_AUDIO_IN_R:  p_in_r = (const float*)data; break;
            case PORT_AUDIO_OUT_L: p_out_l = (float*)data; break;
            case PORT_AUDIO_OUT_R: p_out_r = (float*)data; break;
            case PORT_DWELL:       p_dwell = (const float*)data; break;
            case PORT_TONE:        p_tone = (const float*)data; break;
            case PORT_MIX:         p_mix = (const float*)data; break;
            case PORT_DRIP:        p_drip = (const float*)data; break;
            case PORT_DECAY:       p_decay = (const float*)data; break;
            case PORT_BYPASS:      p_bypass = (const float*)data; break;
            default: break;
        }
    }

    void run(uint32_t sample_count) {
        if (!p_out_l) return;
        const float* inL = p_in_l ? p_in_l : p_out_l;
        const float* inR = p_in_r ? p_in_r : inL;
        float* outL = p_out_l;
        float* outR = p_out_r ? p_out_r : outL;

        if (p_bypass && *p_bypass < 0.5f) {
            if (outL != inL) memcpy(outL, inL, sample_count * sizeof(float));
            if (outR != inR) memcpy(outR, inR, sample_count * sizeof(float));
            return;
        }

        float dwell = p_dwell ? (*p_dwell * 0.01f) : 0.5f;
        float tone = p_tone ? (*p_tone * 0.01f) : 0.6f;
        float mix = p_mix ? (*p_mix * 0.01f) : 0.35f;
        float drip = p_drip ? (*p_drip * 0.01f) : 0.7f;
        float decay = p_decay ? (*p_decay * 0.01f) : 0.65f;

        float drive = 0.5f + 3.5f * dwell;
        float target_decay = 0.70f + 0.28f * decay;

        float tone_fc = 800.0f + tone * 7500.0f;
        float tone_coeff = 1.0f - std::exp((float)(-2.0 * 3.1415926535 * tone_fc / sample_rate));
        float damp_coeff = 0.15f + 0.30f * (1.0f - tone);

        for (uint32_t s = 0; s < sample_count; ++s) {
            smoothed_mix += 0.005f * (mix - smoothed_mix);
            smoothed_decay += 0.005f * (target_decay - smoothed_decay);

            float xL = inL[s];
            float xR = inR[s];
            float xMono = 0.5f * (xL + xR);

            // Tube driver soft clip
            float driven = std::tanh(xMono * drive);

            // Diffusion stage 1
            float d1 = diff_buf1[diff_idx1];
            float diff1_out = -0.6f * driven + d1;
            diff_buf1[diff_idx1] = driven + 0.6f * diff1_out;
            diff_idx1 = (diff_idx1 + 1) % DIFF_DELAY1;

            // Diffusion stage 2
            float d2 = diff_buf2[diff_idx2];
            float diff2_out = -0.5f * diff1_out + d2;
            diff_buf2[diff_idx2] = diff1_out + 0.5f * diff2_out;
            diff_idx2 = (diff_idx2 + 1) % DIFF_DELAY2;

            float diffused = diff2_out;

            // Read springs
            float sp_out[NUM_SPRINGS];
            for (int i = 0; i < NUM_SPRINGS; ++i) {
                int rpos = (write_pos[i] - spring_delays[i] + MAX_DELAY) % MAX_DELAY;
                sp_out[i] = spring_buffers[i][rpos];
            }

            // Cross-coupling
            float fb0 = smoothed_decay * (sp_out[0] * 0.55f + sp_out[1] * 0.30f - sp_out[2] * 0.15f);
            float fb1 = smoothed_decay * (sp_out[1] * 0.55f + sp_out[2] * 0.30f - sp_out[0] * 0.15f);
            float fb2 = smoothed_decay * (sp_out[2] * 0.55f + sp_out[0] * 0.30f - sp_out[1] * 0.15f);

            // Damping LP filter
            lp_filter[0] += damp_coeff * (fb0 - lp_filter[0]);
            lp_filter[1] += damp_coeff * (fb1 - lp_filter[1]);
            lp_filter[2] += damp_coeff * (fb2 - lp_filter[2]);

            // Dispersive allpass chains (the "boing / drip")
            float disp0 = process_dispersion(0, lp_filter[0] + diffused * 0.7f, drip);
            float disp1 = process_dispersion(1, lp_filter[1] + diffused * 0.5f, drip);
            float disp2 = process_dispersion(2, lp_filter[2] + diffused * 0.6f, drip);

            // Write back into springs
            spring_buffers[0][write_pos[0]] = disp0;
            spring_buffers[1][write_pos[1]] = disp1;
            spring_buffers[2][write_pos[2]] = disp2;

            for (int i = 0; i < NUM_SPRINGS; ++i) {
                write_pos[i] = (write_pos[i] + 1) % MAX_DELAY;
            }

            // Decorrelated stereo output matrix
            float wetL = sp_out[0] + 0.5f * sp_out[1] - 0.3f * sp_out[2];
            float wetR = sp_out[2] + 0.5f * sp_out[1] - 0.3f * sp_out[0];

            out_tone_lp_l += tone_coeff * (wetL - out_tone_lp_l);
            out_tone_lp_r += tone_coeff * (wetR - out_tone_lp_r);

            outL[s] = xL * (1.0f - smoothed_mix * 0.5f) + out_tone_lp_l * smoothed_mix;
            outR[s] = xR * (1.0f - smoothed_mix * 0.5f) + out_tone_lp_r * smoothed_mix;
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double sample_rate, const char* bundle_path, const LV2_Feature* const* features) {
    (void)descriptor; (void)bundle_path; (void)features;
    return (LV2_Handle)new CyberSpringReverbStereo(sample_rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberSpringReverbStereo*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    ((CyberSpringReverbStereo*)instance)->reset();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberSpringReverbStereo*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance) {
    delete (CyberSpringReverbStereo*)instance;
}

static const void* extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
