/*
 * Cyber Feedback Super Mono - Dev / Tuner Edition
 * Boutique Clean Sustain & Feedbacker Pedal (Mono) for MODEP / MOD Desktop.
 */

#include "lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef PLUGIN_URI
#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-feedback-super-mono"
#endif

enum PortIndex {
    PORT_AUDIO_IN          = 0,
    PORT_AUDIO_OUT         = 1,
    PORT_BYPASS            = 2,
    PORT_FEEDBACK          = 3,   // Expression pedal target (0.0 to 1.0)
    PORT_SUSTAIN_LEVEL     = 4,   // Level of sustained signal (0.0 to 1.5, default 1.0)
    PORT_CROSSFADE_MS      = 5,   // Crossfade window ms (5.0 to 100.0, default 20.0)
    PORT_LOOP_LEN_MIN      = 6,   // Min loop length ms (10.0 to 80.0, default 30.0)
    PORT_LOOP_NUDGE        = 7,   // Loop point micro-nudge samples (-50 to +50, default 0)
    PORT_RELEASE_MS        = 8,   // Release time ms when pedal backed off (50 to 3000, default 400)
    PORT_WAVER_DEPTH       = 9,   // Pitch / micro-drift depth (0.0 to 10.0, default 1.5)
    PORT_WAVER_RATE        = 10,  // Waver LFO speed Hz (0.1 to 8.0, default 1.2)
    PORT_SPEAKER_DISTRESS  = 11,  // Speaker cone compliance amount (0.0 to 1.0, default 0.35)
    PORT_ROOM_IN_LOOP      = 12,  // 0: Post Room, 1: Room in Feedback Loop
    PORT_ROOM_SIZE         = 13,  // Room size ms (5 to 300, default 180)
    PORT_ROOM_DECAY        = 14,  // Room decay sec (0.1 to 3.0, default 2.0)
    PORT_ROOM_MIX          = 15   // Room output mix % (0 to 100, default 25.0)
};

// -------------------------------------------------------------------------
// Supercharged Speaker Distress & Cone Compliance Engine (Mono)
// -------------------------------------------------------------------------
class SuperchargedSpeakerDistress {
public:
    float speakerEnv;
    float speakerThermalEnv;
    float speakerConeHistory;
    float spkAtk;
    float spkRel;
    float spkThermalRel;

    void init(double sampleRate) {
        speakerEnv = 0.0f;
        speakerThermalEnv = 0.0f;
        speakerConeHistory = 0.0f;
        spkAtk = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0015f));
        spkRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.045f));
        spkThermalRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.350f));
    }

    void reset() {
        speakerEnv = speakerThermalEnv = speakerConeHistory = 0.0f;
    }

    inline float process(float in, float driveAmount, float asymAmount, double sampleRate) {
        if (driveAmount < 0.01f) return in;
        float s = in;
        float rect = fabsf(s);

        if (rect > speakerEnv) {
            speakerEnv += spkAtk * (rect - speakerEnv);
        } else {
            speakerEnv += spkRel * (rect - speakerEnv);
        }
        speakerThermalEnv += spkThermalRel * (speakerEnv - speakerThermalEnv);

        float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 1.5f);
        float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.35f);
        s = s * comp * thermalComp;

        float coneStress = s * (1.0f + driveAmount * 1.2f);
        float t = tanhf(coneStress);
        float coneOut = t - asymAmount * (t * t);

        float dampingFc = 7500.0f - driveAmount * 2500.0f;
        if (dampingFc < 2500.0f) dampingFc = 2500.0f;
        float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
        float a0 = w / (1.0f + w);
        speakerConeHistory += a0 * (coneOut - speakerConeHistory);

        s = (1.0f - driveAmount * 0.5f) * coneOut + (driveAmount * 0.5f) * speakerConeHistory;
        s *= (1.0f + driveAmount * 0.10f);
        return s;
    }
};

// -------------------------------------------------------------------------
// Acoustic Cabinet Decay Diffuser (Mono)
// -------------------------------------------------------------------------
class CabinetAcousticTail {
private:
    float d1[997], d2[1453], d3[1987], d4[2741];
    int idx1, idx2, idx3, idx4;
    float s1, s2, s3, s4;

public:
    void init() {
        idx1 = idx2 = idx3 = idx4 = 0;
        s1 = s2 = s3 = s4 = 0.0f;
        memset(d1, 0, sizeof(d1));
        memset(d2, 0, sizeof(d2));
        memset(d3, 0, sizeof(d3));
        memset(d4, 0, sizeof(d4));
    }

    inline float process(float in, float tailSec, double sampleRate) {
        float fb = powf(0.001f, 1500.0f / (tailSec * (float)sampleRate));
        if (fb > 0.88f) fb = 0.88f;

        float out1 = -0.5f * in + d1[idx1];
        d1[idx1] = in + 0.5f * out1;
        if (++idx1 >= 997) idx1 = 0;

        float out2 = -0.5f * out1 + d2[idx2];
        d2[idx2] = out1 + 0.5f * out2;
        if (++idx2 >= 1453) idx2 = 0;

        float out3 = -0.5f * out2 + d3[idx3];
        d3[idx3] = out2 + 0.5f * out3;
        if (++idx3 >= 1987) idx3 = 0;

        float out4 = -0.5f * out3 + d4[idx4];
        d4[idx4] = out3 + 0.5f * out4;
        if (++idx4 >= 2741) idx4 = 0;

        s1 = out1 * fb;
        s2 = out2 * fb;
        s3 = out3 * fb;
        s4 = out4 * fb;

        return (s1 + s2 + s3 + s4) * 0.25f;
    }
};

// -------------------------------------------------------------------------
// High-Density Acoustic Early-Reflection Room Simulator (Mono)
// -------------------------------------------------------------------------
class HighDensityRoomSimulatorMono {
private:
    static const int MAX_ROOM_SAMPLES = 16384;
    float buf_a[MAX_ROOM_SAMPLES];
    float buf_b[MAX_ROOM_SAMPLES];
    float buf_c[MAX_ROOM_SAMPLES];

    int write_pos;
    float damp_a, damp_b, damp_c;

public:
    void init() {
        memset(buf_a, 0, sizeof(buf_a));
        memset(buf_b, 0, sizeof(buf_b));
        memset(buf_c, 0, sizeof(buf_c));
        write_pos = 0;
        damp_a = damp_b = damp_c = 0.0f;
    }

    void reset() {
        init();
    }

    inline float process(float in, float room_size_sec, float room_decay_sec, float damping_hz, double sample_rate) {
        int delay_a = (int)(room_size_sec * 0.618f * (float)sample_rate);
        int delay_b = (int)(room_size_sec * 0.853f * (float)sample_rate);
        int delay_c = (int)(room_size_sec * 1.000f * (float)sample_rate);

        delay_a = std::max(64, std::min(MAX_ROOM_SAMPLES - 1, delay_a));
        delay_b = std::max(64, std::min(MAX_ROOM_SAMPLES - 1, delay_b));
        delay_c = std::max(64, std::min(MAX_ROOM_SAMPLES - 1, delay_c));

        float damp_w = 2.0f * (float)M_PI * damping_hz / (float)sample_rate;
        float damp_coeff = damp_w / (1.0f + damp_w);

        float fb = powf(0.001f, (room_size_sec * 1.5f) / (room_decay_sec + 0.01f));
        if (fb > 0.80f) fb = 0.80f;

        int read_a = (write_pos - delay_a + MAX_ROOM_SAMPLES) & (MAX_ROOM_SAMPLES - 1);
        int read_b = (write_pos - delay_b + MAX_ROOM_SAMPLES) & (MAX_ROOM_SAMPLES - 1);
        int read_c = (write_pos - delay_c + MAX_ROOM_SAMPLES) & (MAX_ROOM_SAMPLES - 1);

        float tap_a = buf_a[read_a];
        float tap_b = buf_b[read_b];
        float tap_c = buf_c[read_c];

        damp_a += damp_coeff * (tap_a - damp_a);
        damp_b += damp_coeff * (tap_b - damp_b);
        damp_c += damp_coeff * (tap_c - damp_c);

        float recirc_a = in + damp_b * fb * 0.45f - damp_c * fb * 0.25f;
        float recirc_b = in + damp_c * fb * 0.45f - damp_a * fb * 0.25f;
        float recirc_c = in + damp_a * fb * 0.45f - damp_b * fb * 0.25f;

        buf_a[write_pos] = tanhf(recirc_a);
        buf_b[write_pos] = tanhf(recirc_b);
        buf_c[write_pos] = tanhf(recirc_c);

        write_pos = (write_pos + 1) & (MAX_ROOM_SAMPLES - 1);

        return (damp_a + damp_b + damp_c) * 0.333f;
    }
};

// -------------------------------------------------------------------------
// Transparent Safety Ceiling Limiter (Mono)
// -------------------------------------------------------------------------
class OutputCeilingLimiterMono {
private:
    float gain_env;
    float atk_coeff;
    float rel_coeff;
    float ceiling;
    float knee_threshold;
    float margin;

public:
    void init(double sampleRate, float ceilingDb = -4.0f) {
        ceiling = powf(10.0f, ceilingDb / 20.0f);
        knee_threshold = ceiling * 0.88f;
        margin = ceiling - knee_threshold;
        gain_env = 1.0f;
        atk_coeff = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0005f));
        rel_coeff = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0600f));
    }

    inline float process(float in) {
        float peak = fabsf(in);
        float target_gain = 1.0f;
        if (peak > knee_threshold) {
            target_gain = knee_threshold / (peak + 1e-6f);
            if (target_gain > 1.0f) target_gain = 1.0f;
        }

        if (target_gain < gain_env) {
            gain_env += atk_coeff * (target_gain - gain_env);
        } else {
            gain_env += rel_coeff * (target_gain - gain_env);
        }

        float scaled = in * gain_env;
        float ax = fabsf(scaled);
        if (ax <= knee_threshold) return scaled;
        float excess = ax - knee_threshold;
        float compressed = knee_threshold + margin * tanhf(excess / margin);
        return (scaled < 0.0f) ? -compressed : compressed;
    }
};

// -------------------------------------------------------------------------
// Zero-Crossing Micro-Sampler & Clean Sustainer (Mono)
// -------------------------------------------------------------------------
class ZeroCrossingMicroSamplerMono {
public:
    static const int MAX_BUF = 192000;

private:
    float buf[MAX_BUF];
    int write_idx;

    bool is_locked;
    float crossfade_progress;

    int loop_start_idx;
    int loop_len;
    float phase_a;
    float phase_b;

    float waver_phase;
    float envelope_follower;
    float target_envelope;

    float loop_gain;

    float hp_x1, hp_x2, hp_y1, hp_y2;

    inline float read_hermite(const float* buffer, float pos) {
        int i1 = (int)pos;
        int i0 = (i1 - 1 + MAX_BUF) % MAX_BUF;
        int i2 = (i1 + 1) % MAX_BUF;
        int i3 = (i1 + 2) % MAX_BUF;
        i1 = (i1 + MAX_BUF) % MAX_BUF;

        float frac = pos - (float)((int)pos);
        if (frac < 0.0f) frac += 1.0f;
        float frac2 = frac * frac;
        float frac3 = frac2 * frac;

        float y0 = buffer[i0];
        float y1 = buffer[i1];
        float y2 = buffer[i2];
        float y3 = buffer[i3];

        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    inline float filter_hp(float in, float b0, float b1, float b2, float a1, float a2) {
        float out = b0 * in + b1 * hp_x1 + b2 * hp_x2 - a1 * hp_y1 - a2 * hp_y2;
        hp_x2 = hp_x1; hp_x1 = in;
        hp_y2 = hp_y1; hp_y1 = out;
        return out;
    }

public:
    void init() {
        memset(buf, 0, sizeof(buf));
        write_idx = 0;
        is_locked = false;
        crossfade_progress = 0.0f;
        loop_start_idx = 0;
        loop_len = 2400;
        phase_a = 0.0f;
        phase_b = 1200.0f;
        waver_phase = 0.0f;
        envelope_follower = 0.0f;
        target_envelope = 1.0f;
        loop_gain = 1.0f;
        hp_x1 = hp_x2 = hp_y1 = hp_y2 = 0.0f;
    }

    void reset() {
        init();
    }

    int find_zero_crossing_pos_slope(int start_idx, int max_search) {
        for (int k = 0; k < max_search; ++k) {
            int curr = (start_idx - k + MAX_BUF * 2) % MAX_BUF;
            int prev = (curr - 1 + MAX_BUF) % MAX_BUF;
            if (buf[prev] <= 0.0f && buf[curr] > 0.0f) {
                return curr;
            }
        }
        return start_idx;
    }

    inline float process(float in,
                         float feedback_pedal,
                         float sustain_level,
                         float crossfade_ms,
                         float loop_len_min_ms,
                         float loop_nudge_samples,
                         float release_ms,
                         float waver_depth,
                         float waver_rate,
                         double sample_rate) {

        if (!is_locked) {
            buf[write_idx] = in;
            write_idx = (write_idx + 1) % MAX_BUF;
        }

        const float trigger_threshold = 0.45f;
        bool pedal_engaged = (feedback_pedal >= trigger_threshold);

        if (pedal_engaged) {
            if (!is_locked) {
                is_locked = true;

                int safety_offset = (int)(0.015f * (float)sample_rate);
                int search_origin = (write_idx - safety_offset + MAX_BUF) % MAX_BUF;

                int zc_end = find_zero_crossing_pos_slope(search_origin, (int)(0.080f * (float)sample_rate));
                int min_loop_samples = (int)(loop_len_min_ms * 0.001f * (float)sample_rate);
                if (min_loop_samples < 240) min_loop_samples = 240;

                int search_start = (zc_end - min_loop_samples + MAX_BUF) % MAX_BUF;
                int zc_start = find_zero_crossing_pos_slope(search_start, (int)(0.120f * (float)sample_rate));

                int nudge = (int)loop_nudge_samples;
                zc_start = (zc_start + nudge + MAX_BUF) % MAX_BUF;

                loop_len = (zc_end - zc_start + MAX_BUF) % MAX_BUF;
                if (loop_len < min_loop_samples) loop_len = min_loop_samples;
                if (loop_len > MAX_BUF / 4) loop_len = MAX_BUF / 4;
                loop_start_idx = zc_start;

                phase_a = 0.0f;
                phase_b = (float)loop_len * 0.5f;

                float loop_rms = 0.0001f;
                int step = std::max(1, loop_len / 64);
                int count = 0;
                for (int s = 0; s < loop_len; s += step) {
                    int idx = (loop_start_idx + s) % MAX_BUF;
                    float sm = buf[idx];
                    loop_rms += sm * sm;
                    count++;
                }
                loop_rms = sqrtf(loop_rms / (float)(count > 0 ? count : 1));

                float live_rms = sqrtf(in * in + 0.0001f);
                loop_gain = (live_rms / (loop_rms + 1e-5f)) * sustain_level;
                if (loop_gain > 2.0f) loop_gain = 2.0f;
                if (loop_gain < 0.5f) loop_gain = 0.5f;
            }

            float fade_in_rate = 1.0f / (crossfade_ms * 0.001f * (float)sample_rate + 1.0f);
            crossfade_progress = std::min(1.0f, crossfade_progress + fade_in_rate);
            target_envelope = (feedback_pedal - trigger_threshold) / (1.0f - trigger_threshold);
            if (target_envelope > 1.0f) target_envelope = 1.0f;
        } else {
            float release_rate = 1.0f / (release_ms * 0.001f * (float)sample_rate + 1.0f);
            crossfade_progress = std::max(0.0f, crossfade_progress - release_rate);
            target_envelope = 0.0f;
            if (crossfade_progress <= 0.0001f) {
                is_locked = false;
            }
        }

        float env_coeff = 1.0f - expf(-1.0f / ((float)sample_rate * 0.035f));
        envelope_follower += env_coeff * (target_envelope - envelope_follower);

        if (crossfade_progress <= 0.0001f && !is_locked) {
            return in;
        }

        waver_phase += (float)(2.0 * M_PI * waver_rate / sample_rate);
        if (waver_phase >= 2.0f * (float)M_PI) waver_phase -= 2.0f * (float)M_PI;
        float drift_offset = sinf(waver_phase) * waver_depth;

        phase_a += 1.0f;
        if (phase_a >= (float)loop_len) phase_a -= (float)loop_len;
        phase_b += 1.0f;
        if (phase_b >= (float)loop_len) phase_b -= (float)loop_len;

        float w_a = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * phase_a / (float)loop_len));
        float w_b = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * phase_b / (float)loop_len));

        float pos_a = (float)loop_start_idx + phase_a + drift_offset;
        float pos_b = (float)loop_start_idx + phase_b + drift_offset;

        while (pos_a < 0.0f) pos_a += (float)MAX_BUF;
        while (pos_a >= (float)MAX_BUF) pos_a -= (float)MAX_BUF;
        while (pos_b < 0.0f) pos_b += (float)MAX_BUF;
        while (pos_b >= (float)MAX_BUF) pos_b -= (float)MAX_BUF;

        float sample_a = read_hermite(buf, pos_a);
        float sample_b = read_hermite(buf, pos_b);

        float sustained_loop = (sample_a * w_a + sample_b * w_b) * loop_gain * envelope_follower;

        // Anti-thump 50Hz filter
        float w0 = 2.0f * (float)M_PI * 50.0f / (float)sample_rate;
        float cos_w0 = cosf(w0);
        float alpha = sinf(w0) * 0.7071f;
        float b0 = (1.0f + cos_w0) * 0.5f;
        float b1 = -(1.0f + cos_w0);
        float b2 = (1.0f + cos_w0) * 0.5f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cos_w0;
        float a2 = 1.0f - alpha;

        float nb0 = b0 / a0; float nb1 = b1 / a0; float nb2 = b2 / a0;
        float na1 = a1 / a0; float na2 = a2 / a0;

        float filtered = filter_hp(sustained_loop, nb0, nb1, nb2, na1, na2);

        float mix_wet = sinf(crossfade_progress * (float)M_PI * 0.5f);
        float mix_dry = cosf(crossfade_progress * (float)M_PI * 0.5f);

        return in * mix_dry + filtered * mix_wet;
    }
};

// -------------------------------------------------------------------------
// Main CyberFeedbackSuperMono Plugin Class
// -------------------------------------------------------------------------
class CyberFeedbackSuperMono {
private:
    double sample_rate;

    ZeroCrossingMicroSamplerMono micro_sampler;
    SuperchargedSpeakerDistress distress;
    CabinetAcousticTail tail_diffuser;
    HighDensityRoomSimulatorMono room_sim;
    OutputCeilingLimiterMono output_limiter;

    float smoothed_feedback;

    // LV2 Port Pointers (16 Ports)
    const float* p_in;
    float* p_out;
    const float* p_bypass;
    const float* p_feedback;
    const float* p_sustain_level;
    const float* p_crossfade_ms;
    const float* p_loop_len_min;
    const float* p_loop_nudge;
    const float* p_release_ms;
    const float* p_waver_depth;
    const float* p_waver_rate;
    const float* p_speaker_distress;
    const float* p_room_in_loop;
    const float* p_room_size;
    const float* p_room_decay;
    const float* p_room_mix;

public:
    CyberFeedbackSuperMono(double sr) : sample_rate(sr) {
        micro_sampler.init();
        distress.init(sample_rate);
        tail_diffuser.init();
        room_sim.init();
        output_limiter.init(sample_rate, -4.0f);
        smoothed_feedback = 0.0f;
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_AUDIO_IN:          p_in = (const float*)data; break;
            case PORT_AUDIO_OUT:         p_out = (float*)data; break;
            case PORT_BYPASS:            p_bypass = (const float*)data; break;
            case PORT_FEEDBACK:          p_feedback = (const float*)data; break;
            case PORT_SUSTAIN_LEVEL:     p_sustain_level = (const float*)data; break;
            case PORT_CROSSFADE_MS:      p_crossfade_ms = (const float*)data; break;
            case PORT_LOOP_LEN_MIN:      p_loop_len_min = (const float*)data; break;
            case PORT_LOOP_NUDGE:        p_loop_nudge = (const float*)data; break;
            case PORT_RELEASE_MS:        p_release_ms = (const float*)data; break;
            case PORT_WAVER_DEPTH:       p_waver_depth = (const float*)data; break;
            case PORT_WAVER_RATE:        p_waver_rate = (const float*)data; break;
            case PORT_SPEAKER_DISTRESS:  p_speaker_distress = (const float*)data; break;
            case PORT_ROOM_IN_LOOP:      p_room_in_loop = (const float*)data; break;
            case PORT_ROOM_SIZE:         p_room_size = (const float*)data; break;
            case PORT_ROOM_DECAY:        p_room_decay = (const float*)data; break;
            case PORT_ROOM_MIX:          p_room_mix = (const float*)data; break;
        }
    }

    void run(uint32_t sample_count) {
        bool bypass = (p_bypass && *p_bypass < 0.5f);
        if (bypass) {
            if (p_out != p_in) memcpy(p_out, p_in, sample_count * sizeof(float));
            return;
        }

        float raw_feedback = (p_feedback ? *p_feedback : 0.0f);
        float target_feedback = std::max(0.0f, std::min(1.0f, raw_feedback));

        float sustain_level = (p_sustain_level ? *p_sustain_level : 1.0f);
        float crossfade_ms = (p_crossfade_ms ? *p_crossfade_ms : 20.0f);
        float loop_len_min = (p_loop_len_min ? *p_loop_len_min : 30.0f);
        float loop_nudge = (p_loop_nudge ? *p_loop_nudge : 0.0f);
        float release_ms = (p_release_ms ? *p_release_ms : 400.0f);
        float waver_depth = (p_waver_depth ? *p_waver_depth : 1.5f);
        float waver_rate = (p_waver_rate ? *p_waver_rate : 1.2f);
        float speaker_distress = (p_speaker_distress ? *p_speaker_distress : 0.35f);

        bool room_in_loop = (p_room_in_loop ? (*p_room_in_loop > 0.5f) : true);
        float room_size_sec = (p_room_size ? *p_room_size : 180.0f) * 0.001f;
        float room_decay_sec = (p_room_decay ? *p_room_decay : 2.0f);
        float room_mix_amt = (p_room_mix ? *p_room_mix : 25.0f) * 0.01f;

        float pedal_atk_rate = 1.0f - expf(-1.0f / (0.015f * (float)sample_rate));
        float pedal_rel_rate = 1.0f - expf(-1.0f / (0.035f * (float)sample_rate));

        float distress_drive = speaker_distress * 1.5f;
        float asym_amount = 0.10f + speaker_distress * 0.20f;

        for (uint32_t i = 0; i < sample_count; ++i) {
            float in = p_in[i];

            if (target_feedback > smoothed_feedback) {
                smoothed_feedback += (target_feedback - smoothed_feedback) * pedal_atk_rate;
            } else {
                smoothed_feedback += (target_feedback - smoothed_feedback) * pedal_rel_rate;
            }

            float sustained = micro_sampler.process(in,
                                                   smoothed_feedback,
                                                   sustain_level,
                                                   crossfade_ms,
                                                   loop_len_min,
                                                   loop_nudge,
                                                   release_ms,
                                                   waver_depth,
                                                   waver_rate,
                                                   sample_rate);

            float distressed = distress.process(sustained, distress_drive, asym_amount, sample_rate);
            float tailed = tail_diffuser.process(distressed, 2.5f, sample_rate);
            float acoustic = distressed * 0.80f + tailed * 0.35f;

            float room = room_sim.process(acoustic, room_size_sec, room_decay_sec, 12000.0f, sample_rate);

            float final_out = in;
            if (room_in_loop) {
                float blended_sustain = acoustic + room * room_mix_amt;
                final_out = in + (blended_sustain - in) * smoothed_feedback;
            } else {
                float fx = in + (acoustic - in) * smoothed_feedback;
                final_out = fx + room * room_mix_amt;
            }

            p_out[i] = output_limiter.process(final_out);
        }
    }

    void reset() {
        micro_sampler.reset();
        distress.reset();
        tail_diffuser.init();
        room_sim.reset();
        output_limiter.init(sample_rate, -4.0f);
        smoothed_feedback = 0.0f;
    }
};

// -------------------------------------------------------------------------
// LV2 Plugin Interface Callbacks
// -------------------------------------------------------------------------
static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double rate,
                             const char* bundle_path,
                             const LV2_Feature* const* features) {
    (void)descriptor;
    (void)bundle_path;
    (void)features;
    return (LV2_Handle)new CyberFeedbackSuperMono(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberFeedbackSuperMono*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    ((CyberFeedbackSuperMono*)instance)->reset();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberFeedbackSuperMono*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {
    ((CyberFeedbackSuperMono*)instance)->reset();
}

static void cleanup(LV2_Handle instance) {
    delete (CyberFeedbackSuperMono*)instance;
}

static const void* extension_data(const char* uri) {
    (void)uri;
    return NULL;
}

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    if (index == 0) return &descriptor;
    return NULL;
}
