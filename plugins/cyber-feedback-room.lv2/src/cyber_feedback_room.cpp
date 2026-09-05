/*
 * Cyber Feedback Room (Stereo)
 * Pure electro-acoustic feedback resonator & room matrix pedal.
 * Clean, production-ready version with only Feedback + Reverb controls exposed.
 * Uses the EXACT DSP engine from Cyber Acoustic Feedbacker with all tuned parameters
 * hardcoded directly from the default preset.
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
#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-feedback-room"
#endif

enum PortIndex {
    PORT_AUDIO_IN_L        = 0,
    PORT_AUDIO_IN_R        = 1,
    PORT_AUDIO_OUT_L       = 2,
    PORT_AUDIO_OUT_R       = 3,
    PORT_BYPASS            = 4,
    PORT_TRIGGER           = 5,  // Feedback / Expression Pedal (0.0 to 1.0)
    PORT_ROOM_IN_LOOP      = 6,  // Toggle: 0.0 = Room on Output, 1.0 = Room in Feedback Loop
    PORT_ROOM_SIZE         = 7,  // Room Size / Reflection Delay (5ms to 300ms, default 298.85ms)
    PORT_ROOM_DECAY        = 8,  // Room Reflection Decay (0.1s to 3.0s, default 3.0s)
    PORT_ROOM_DAMPING      = 9,  // Room Wall Damping (1000Hz to 16000Hz, default 16000Hz)
    PORT_ROOM_FEED         = 10, // Room Recirculation into Loop (0% to 100%, default 100%)
    PORT_ROOM_MIX          = 11  // Room Level in Output (0% to 100%, default 28.35%)
};

// -------------------------------------------------------------------------
// Supercharged Speaker Distress & Cone Compliance Engine (Unpitched)
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
        float s = in;
        float rect = fabsf(s);

        if (rect > speakerEnv) {
            speakerEnv += spkAtk * (rect - speakerEnv);
        } else {
            speakerEnv += spkRel * (rect - speakerEnv);
        }
        speakerThermalEnv += spkThermalRel * (speakerEnv - speakerThermalEnv);

        float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 2.0f);
        float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.50f);
        s = s * comp * thermalComp;

        float coneStress = s * (1.0f + driveAmount * 1.6f);
        float t = tanhf(coneStress);
        float coneOut = t - asymAmount * (t * t);

        float dampingFc = 6800.0f - driveAmount * 2400.0f;
        if (dampingFc < 2400.0f) dampingFc = 2400.0f;
        float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
        float a0 = w / (1.0f + w);
        speakerConeHistory += a0 * (coneOut - speakerConeHistory);

        s = (1.0f - driveAmount * 0.65f) * coneOut + (driveAmount * 0.65f) * speakerConeHistory;
        s *= (1.0f + driveAmount * 0.15f);
        return s;
    }
};

// -------------------------------------------------------------------------
// Acoustic Cabinet Decay Diffuser (Natural Tail Decay, Zero Comb Filter)
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
// High-Density Acoustic Early-Reflection Room Simulator & Diffuser
// -------------------------------------------------------------------------
class HighDensityRoomSimulator {
private:
    static const int MAX_ROOM_SAMPLES = 16384;
    float buf_a[MAX_ROOM_SAMPLES];
    float buf_b[MAX_ROOM_SAMPLES];
    float buf_c[MAX_ROOM_SAMPLES];
    float buf_d[MAX_ROOM_SAMPLES];

    int write_pos;
    float damp_a, damp_b, damp_c, damp_d;

public:
    void init() {
        memset(buf_a, 0, sizeof(buf_a));
        memset(buf_b, 0, sizeof(buf_b));
        memset(buf_c, 0, sizeof(buf_c));
        memset(buf_d, 0, sizeof(buf_d));
        write_pos = 0;
        damp_a = damp_b = damp_c = damp_d = 0.0f;
    }

    void reset() {
        init();
    }

    inline void process(float in_l, float in_r,
                        float room_size_sec, float room_decay_sec, float damping_hz,
                        double sample_rate,
                        float& out_room_l, float& out_room_r) {

        float in_mono = 0.5f * (in_l + in_r);

        int delay_a = (int)(room_size_sec * 0.618f * (float)sample_rate);
        int delay_b = (int)(room_size_sec * 0.853f * (float)sample_rate);
        int delay_c = (int)(room_size_sec * 1.000f * (float)sample_rate);
        int delay_d = (int)(room_size_sec * 1.237f * (float)sample_rate);

        delay_a = std::max(64, std::min(MAX_ROOM_SAMPLES - 1, delay_a));
        delay_b = std::max(64, std::min(MAX_ROOM_SAMPLES - 1, delay_b));
        delay_c = std::max(64, std::min(MAX_ROOM_SAMPLES - 1, delay_c));
        delay_d = std::max(64, std::min(MAX_ROOM_SAMPLES - 1, delay_d));

        float damp_w = 2.0f * (float)M_PI * damping_hz / (float)sample_rate;
        float damp_coeff = damp_w / (1.0f + damp_w);

        float fb = powf(0.001f, (room_size_sec * 1.5f) / (room_decay_sec + 0.01f));
        if (fb > 0.80f) fb = 0.80f;

        int read_a = (write_pos - delay_a + MAX_ROOM_SAMPLES) & (MAX_ROOM_SAMPLES - 1);
        int read_b = (write_pos - delay_b + MAX_ROOM_SAMPLES) & (MAX_ROOM_SAMPLES - 1);
        int read_c = (write_pos - delay_c + MAX_ROOM_SAMPLES) & (MAX_ROOM_SAMPLES - 1);
        int read_d = (write_pos - delay_d + MAX_ROOM_SAMPLES) & (MAX_ROOM_SAMPLES - 1);

        float tap_a = buf_a[read_a];
        float tap_b = buf_b[read_b];
        float tap_c = buf_c[read_c];
        float tap_d = buf_d[read_d];

        damp_a += damp_coeff * (tap_a - damp_a);
        damp_b += damp_coeff * (tap_b - damp_b);
        damp_c += damp_coeff * (tap_c - damp_c);
        damp_d += damp_coeff * (tap_d - damp_d);

        float recirc_a = in_mono + damp_b * fb * 0.4f - damp_c * fb * 0.25f;
        float recirc_b = in_mono + damp_c * fb * 0.4f - damp_d * fb * 0.25f;
        float recirc_c = in_mono + damp_d * fb * 0.4f - damp_a * fb * 0.25f;
        float recirc_d = in_mono + damp_a * fb * 0.4f - damp_b * fb * 0.25f;

        buf_a[write_pos] = tanhf(recirc_a);
        buf_b[write_pos] = tanhf(recirc_b);
        buf_c[write_pos] = tanhf(recirc_c);
        buf_d[write_pos] = tanhf(recirc_d);

        write_pos = (write_pos + 1) & (MAX_ROOM_SAMPLES - 1);

        out_room_l = (damp_a + damp_c - damp_b * 0.5f) * 0.50f;
        out_room_r = (damp_b + damp_d - damp_a * 0.5f) * 0.50f;
    }
};

// -------------------------------------------------------------------------
// Transparent Safety Ceiling Limiter
// -------------------------------------------------------------------------
class OutputCeilingLimiter {
private:
    float gain_env;
    float atk_coeff;
    float rel_coeff;
    float ceiling;
    float knee_threshold;
    float margin;

public:
    void init(double sampleRate, float ceilingDb = -6.0f) {
        ceiling = powf(10.0f, ceilingDb / 20.0f);
        knee_threshold = ceiling * 0.85f;
        margin = ceiling - knee_threshold;
        gain_env = 1.0f;
        atk_coeff = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0005f));
        rel_coeff = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0600f));
    }

    inline void process(float in_l, float in_r, float& out_l, float& out_r) {
        float peak = std::max(fabsf(in_l), fabsf(in_r));
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

        float scaled_l = in_l * gain_env;
        float scaled_r = in_r * gain_env;

        out_l = shape_sample(scaled_l);
        out_r = shape_sample(scaled_r);
    }

    inline float shape_sample(float x) {
        float ax = fabsf(x);
        if (ax <= knee_threshold) return x;
        float excess = ax - knee_threshold;
        float compressed = knee_threshold + margin * tanhf(excess / margin);
        return (x < 0.0f) ? -compressed : compressed;
    }
};

// -------------------------------------------------------------------------
// True Infinite Sustainer & String Simulator (No Dropouts, No Thumping)
// -------------------------------------------------------------------------
class PhysicalStringFeedbackSimulator {
public:
    static const int MAX_BUF = 65536;

private:
    float buf_l[MAX_BUF];
    float buf_r[MAX_BUF];
    int write_idx;

    bool is_locked;
    float crossfade_progress;
    int lock_origin;
    float phase_a, phase_b;
    int current_loop_len;

    float drift_phase;
    float air_phase;

    float target_volume;
    float loop_volume_gain;
    float captured_note_rms;
    float ring_rms;

    uint32_t samples_since_pluck;
    float prev_env;
    float env_velocity;

    // Subsonic DC & Thump Highpass Filters (2-pole Butterworth 65 Hz)
    float hp_x1_l, hp_x2_l, hp_y1_l, hp_y2_l;
    float hp_x1_r, hp_x2_r, hp_y1_r, hp_y2_r;

    // String Core Damping Filter (1-pole lowpass)
    float string_damp_l, string_damp_r;
    float acoustic_return_l, acoustic_return_r;

    inline float read_hermite(const float* buffer, float pos) {
        int i1 = (int)pos;
        int i0 = (i1 - 1 + MAX_BUF) & (MAX_BUF - 1);
        int i2 = (i1 + 1) & (MAX_BUF - 1);
        int i3 = (i1 + 2) & (MAX_BUF - 1);
        i1 = i1 & (MAX_BUF - 1);

        float frac = pos - (float)((int)pos);
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

    inline float saturate_pickup(float x, float drive) {
        if (drive < 0.01f) return x;
        float scaled = x * (1.0f + drive * 1.5f);
        return tanhf(scaled) - 0.08f * drive * (scaled * scaled);
    }

    // Highpass thump filter
    inline float filter_thump_l(float in, float b0, float b1, float b2, float a1, float a2) {
        float out = b0 * in + b1 * hp_x1_l + b2 * hp_x2_l - a1 * hp_y1_l - a2 * hp_y2_l;
        hp_x2_l = hp_x1_l; hp_x1_l = in;
        hp_y2_l = hp_y1_l; hp_y1_l = out;
        return out;
    }

    inline float filter_thump_r(float in, float b0, float b1, float b2, float a1, float a2) {
        float out = b0 * in + b1 * hp_x1_r + b2 * hp_x2_r - a1 * hp_y1_r - a2 * hp_y2_r;
        hp_x2_r = hp_x1_r; hp_x1_r = in;
        hp_y2_r = hp_y1_r; hp_y1_r = out;
        return out;
    }

public:
    void init() {
        memset(buf_l, 0, sizeof(buf_l));
        memset(buf_r, 0, sizeof(buf_r));
        write_idx = 0;
        is_locked = false;
        crossfade_progress = 0.0f;
        lock_origin = 0;
        phase_a = 0.0f;
        phase_b = 0.0f;
        drift_phase = 0.0f;
        air_phase = 0.0f;
        current_loop_len = 5760;
        target_volume = 1.0f;
        loop_volume_gain = 1.0f;
        captured_note_rms = 0.0f;
        ring_rms = 0.0f;
        samples_since_pluck = 999999;
        prev_env = 0.0f;
        env_velocity = 0.0f;
        string_damp_l = string_damp_r = 0.0f;
        acoustic_return_l = acoustic_return_r = 0.0f;
        hp_x1_l = hp_x2_l = hp_y1_l = hp_y2_l = 0.0f;
        hp_x1_r = hp_x2_r = hp_y1_r = hp_y2_r = 0.0f;
    }

    void reset() {
        init();
    }

    inline void inject_acoustic_return(float returned_l, float returned_r, float recirc_amount) {
        acoustic_return_l = returned_l * recirc_amount;
        acoustic_return_r = returned_r * recirc_amount;
    }

    inline void process(float in_l, float in_r,
                        float effective_trigger, float guitar_env, bool is_new_pluck,
                        bool hold_on_pluck,
                        float takeover_threshold, float transition_sec, float loop_sec,
                        float damping_hz, float vol_match_ratio,
                        float attack_lockout_sec, float loop_attack_sec,
                        float loop_release_sec, float drift_ratio,
                        float pickup_sat_amt, float air_wobble_rate, float air_wobble_depth,
                        double sample_rate,
                        float& out_l, float& out_r) {

        env_velocity = guitar_env - prev_env;
        prev_env = guitar_env;

        if (is_new_pluck) {
            samples_since_pluck = 0;
            // Only release lock on a new pluck if hold_on_pluck is false AND pedal is backed off
            if (!hold_on_pluck && effective_trigger < takeover_threshold) {
                is_locked = false;
                crossfade_progress = 0.0f;
            }
        } else if (samples_since_pluck < 2000000) {
            samples_since_pluck++;
        }

        // 1. Constantly capture guitar in circular ring buffer when UNLOCKED
        // Once locked, DO NOT overwrite the clean sampled buffer with decaying audio!
        if (!is_locked) {
            buf_l[write_idx] = in_l;
            buf_r[write_idx] = in_r;
            write_idx = (write_idx + 1) & (MAX_BUF - 1);
        }

        // 2. Continuous RMS tracking
        float inst_power = 0.5f * (in_l * in_l + in_r * in_r);
        ring_rms += 0.005f * (inst_power - ring_rms);

        // 3. Transient Lockout & Tail Verification
        uint32_t lockout_samples = (uint32_t)(attack_lockout_sec * (float)sample_rate);
        bool past_attack_phase = (samples_since_pluck >= lockout_samples);
        bool note_is_decaying_or_flat = (env_velocity <= 0.0002f);
        bool can_latch = past_attack_phase && note_is_decaying_or_flat && (guitar_env > 0.0012f);

        bool trigger_active = (effective_trigger >= takeover_threshold);

        if (trigger_active) {
            if (!is_locked && can_latch) {
                // FREEZE THE CLEAN NOTE TAIL PERMANENTLY INTO THE RECIRCULATING STRING
                is_locked = true;
                current_loop_len = (int)(loop_sec * (float)sample_rate);
                if (current_loop_len < 384) current_loop_len = 384;
                if (current_loop_len > MAX_BUF / 2) current_loop_len = MAX_BUF / 2;

                int safety_offset = (int)(loop_attack_sec * 0.5f * (float)sample_rate);
                lock_origin = (write_idx - current_loop_len - safety_offset + MAX_BUF * 2) & (MAX_BUF - 1);
                phase_a = 0.0f;
                phase_b = (float)current_loop_len * 0.5f;

                captured_note_rms = sqrtf(std::max(ring_rms, 0.0001f));

                float loop_rms = 0.0001f;
                for (int s = 0; s < current_loop_len; s += 8) {
                    int idx = (lock_origin + s) & (MAX_BUF - 1);
                    float s_mono = 0.5f * (buf_l[idx] + buf_r[idx]);
                    loop_rms += s_mono * s_mono;
                }
                loop_rms = sqrtf(loop_rms / (float)(current_loop_len / 8));

                loop_volume_gain = (captured_note_rms / (loop_rms + 1e-6f)) * vol_match_ratio;
                if (loop_volume_gain > 2.5f) loop_volume_gain = 2.5f;
                if (loop_volume_gain < 0.4f) loop_volume_gain = 0.4f;
            }

            if (is_locked) {
                // Crossfade in smoothly to 100% held drone
                float fade_rate = 1.0f / (transition_sec * (float)sample_rate + 1.0f);
                crossfade_progress = std::min(1.0f, crossfade_progress + fade_rate);
            }
        } else {
            // Pedal backed off below takeover: smooth release out
            float fade_rate = 1.0f / (0.080f * (float)sample_rate + 1.0f);
            crossfade_progress = std::max(0.0f, crossfade_progress - fade_rate);
            if (crossfade_progress <= 0.0f) {
                is_locked = false;
            }
        }

        if (crossfade_progress <= 0.0001f) {
            out_l = in_l;
            out_r = in_r;
            return;
        }

        // 4. Acoustic Standing-Wave Air Wobble LFO
        air_phase += (float)(2.0 * M_PI * air_wobble_rate / sample_rate);
        if (air_phase >= 2.0f * (float)M_PI) air_phase -= 2.0f * (float)M_PI;
        float air_wobble = 1.0f + sinf(air_phase) * air_wobble_depth;

        // Micro-drift
        drift_phase += (float)(2.0 * M_PI * 0.65 / sample_rate);
        if (drift_phase >= 2.0f * (float)M_PI) drift_phase -= 2.0f * (float)M_PI;
        float max_drift_samples = drift_ratio * (0.003f * (float)sample_rate);
        float drift_mod_l = sinf(drift_phase) * max_drift_samples;
        float drift_mod_r = cosf(drift_phase) * max_drift_samples;

        // 5. Dual 4-Point Hermite Read Heads with Seam Release Vector
        phase_a += 1.0f;
        if (phase_a >= (float)current_loop_len) phase_a -= (float)current_loop_len;
        phase_b += 1.0f;
        if (phase_b >= (float)current_loop_len) phase_b -= (float)current_loop_len;

        float w_a = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * phase_a / (float)current_loop_len));
        float w_b = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * phase_b / (float)current_loop_len));

        float pos_a_l = (float)lock_origin + phase_a + drift_mod_l;
        float pos_a_r = (float)lock_origin + phase_a + drift_mod_r;
        float pos_b_l = (float)lock_origin + phase_b + drift_mod_l;
        float pos_b_r = (float)lock_origin + phase_b + drift_mod_r;

        while (pos_a_l < 0.0f) pos_a_l += (float)MAX_BUF;
        while (pos_a_r < 0.0f) pos_a_r += (float)MAX_BUF;
        while (pos_b_l < 0.0f) pos_b_l += (float)MAX_BUF;
        while (pos_b_r < 0.0f) pos_b_r += (float)MAX_BUF;

        float sample_a_l = read_hermite(buf_l, pos_a_l);
        float sample_a_r = read_hermite(buf_r, pos_a_r);
        float sample_b_l = read_hermite(buf_l, pos_b_l);
        float sample_b_r = read_hermite(buf_r, pos_b_r);

        float raw_string_l = (sample_a_l * w_a + sample_b_l * w_b) * loop_volume_gain * air_wobble;
        float raw_string_r = (sample_a_r * w_a + sample_b_r * w_b) * loop_volume_gain * air_wobble;

        // 6. Anti-Thump Subsonic High-Pass Filter (65 Hz 2-pole Butterworth)
        // Eliminates the cyclic low-frequency thumping beat entirely
        float w0 = 2.0f * (float)M_PI * 65.0f / (float)sample_rate;
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

        float dethump_l = filter_thump_l(raw_string_l, nb0, nb1, nb2, na1, na2);
        float dethump_r = filter_thump_r(raw_string_r, nb0, nb1, nb2, na1, na2);

        // Add soft acoustic feedback recirculation from speaker/room
        dethump_l += acoustic_return_l * 0.25f;
        dethump_r += acoustic_return_r * 0.25f;

        // 7. Magnetic Pickup Saturation
        float pickup_l = saturate_pickup(dethump_l, pickup_sat_amt);
        float pickup_r = saturate_pickup(dethump_r, pickup_sat_amt);

        // 8. String Damping Filter
        float damp_w = 2.0f * (float)M_PI * damping_hz / (float)sample_rate;
        float damp_a = damp_w / (1.0f + damp_w);
        string_damp_l += damp_a * (pickup_l - string_damp_l);
        string_damp_r += damp_a * (pickup_r - string_damp_r);

        float sustained_l = string_damp_l;
        float sustained_r = string_damp_r;

        // 9. Equal-power sinusoidal crossfade between live note and infinite sustain
        float mix_wet = sinf(crossfade_progress * (float)M_PI * 0.5f);
        float mix_dry = cosf(crossfade_progress * (float)M_PI * 0.5f);

        out_l = in_l * mix_dry + sustained_l * mix_wet;
        out_r = in_r * mix_dry + sustained_r * mix_wet;
    }
};

// -------------------------------------------------------------------------
// Main CyberAcousticFeedbacker Plugin Class
// -------------------------------------------------------------------------
class CyberFeedbackRoom {
private:
    double sample_rate;

    SuperchargedSpeakerDistress distress_l;
    SuperchargedSpeakerDistress distress_r;

    CabinetAcousticTail tail_diffuser_l;
    CabinetAcousticTail tail_diffuser_r;

    HighDensityRoomSimulator room_sim;
    OutputCeilingLimiter output_limiter;
    PhysicalStringFeedbackSimulator string_sim;

    // Trigger Wiggle LFO State
    float trig_lfo_phase;

    // Envelope Detectors
    float guitar_env;
    float fast_env;
    float env_atk_coeff;
    float env_rel_coeff;
    bool is_sustaining;

    float smoothed_trigger;

    // LV2 Port Pointers (Exactly 12 Ports)
    const float* p_in_l;
    const float* p_in_r;
    float* p_out_l;
    float* p_out_r;
    const float* p_bypass;
    const float* p_trigger;
    const float* p_room_in_loop;
    const float* p_room_size;
    const float* p_room_decay;
    const float* p_room_damping;
    const float* p_room_feed;
    const float* p_room_mix;

public:
    CyberFeedbackRoom(double sr) : sample_rate(sr) {
        distress_l.init(sample_rate);
        distress_r.init(sample_rate);

        tail_diffuser_l.init();
        tail_diffuser_r.init();

        room_sim.init();
        output_limiter.init(sample_rate, -6.0f);
        string_sim.init();

        trig_lfo_phase = 0.0f;
        guitar_env = 0.0f;
        fast_env = 0.0f;
        env_atk_coeff = 1.0f - expf(-1.0f / ((float)sample_rate * 0.0030f));
        env_rel_coeff = 1.0f - expf(-1.0f / ((float)sample_rate * 0.2500f));
        is_sustaining = false;
        smoothed_trigger = 0.0f;
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_AUDIO_IN_L:        p_in_l = (const float*)data; break;
            case PORT_AUDIO_IN_R:        p_in_r = (const float*)data; break;
            case PORT_AUDIO_OUT_L:       p_out_l = (float*)data; break;
            case PORT_AUDIO_OUT_R:       p_out_r = (float*)data; break;
            case PORT_BYPASS:            p_bypass = (const float*)data; break;
            case PORT_TRIGGER:           p_trigger = (const float*)data; break;
            case PORT_ROOM_IN_LOOP:      p_room_in_loop = (const float*)data; break;
            case PORT_ROOM_SIZE:         p_room_size = (const float*)data; break;
            case PORT_ROOM_DECAY:        p_room_decay = (const float*)data; break;
            case PORT_ROOM_DAMPING:      p_room_damping = (const float*)data; break;
            case PORT_ROOM_FEED:         p_room_feed = (const float*)data; break;
            case PORT_ROOM_MIX:          p_room_mix = (const float*)data; break;
        }
    }

    void run(uint32_t sample_count) {
        bool bypass = (p_bypass && *p_bypass < 0.5f);
        if (bypass) {
            if (p_out_l != p_in_l) memcpy(p_out_l, p_in_l, sample_count * sizeof(float));
            if (p_out_r && p_in_r && p_out_r != p_in_r) memcpy(p_out_r, p_in_r, sample_count * sizeof(float));
            return;
        }

        // Primary user-exposed feedback control
        float raw_trigger = (p_trigger ? *p_trigger : 0.70f);
        float target_trigger = std::max(0.0f, std::min(1.0f, raw_trigger));

        // Reverb controls
        bool room_in_loop = (p_room_in_loop ? (*p_room_in_loop > 0.5f) : true);
        float room_size_sec = (p_room_size ? *p_room_size : 298.84765625f) * 0.001f;
        float room_decay_sec = std::max(0.1f, std::min(3.0f, (p_room_decay ? *p_room_decay : 3.0f)));
        float room_damping_hz = std::max(1000.0f, std::min(16000.0f, (p_room_damping ? *p_room_damping : 16000.0f)));
        float room_mix_amt = (p_room_mix ? *p_room_mix : 28.3482132f) * 0.01f;
        float room_feed_amt = (p_room_feed ? *p_room_feed : 100.0f) * 0.01f;

        // Exact Hardcoded Tuned Parameters from default preset / screenshot
        const float gain_knob = 1.0f;                       // 100.0%
        const float distress_knob = 1.0f;                   // 100.0%
        const float tail_sec = 3.91445303f;                 // 3.91 s
        const float mix_knob = 0.237723217f;                // 23.77%
        const float takeover_threshold = 0.9506138611f;     // 95.06%
        const float transition_sec = 5.2044921875f;         // 5204.49 ms
        const float loop_sec = 0.33098214722f;              // 330.98 ms
        const float damping_hz = 13579.24121094f;           // 13579.24 Hz
        const float vol_match_ratio = 1.0f;                 // 100.0%
        const float bloom_sec = 3.9472661f;                 // 3.95 s
        const float attack_lockout_sec = 0.66858258057f;    // 668.58 ms
        const float loop_attack_sec = 0.297734375f;         // 297.73 ms
        const float loop_release_sec = 0.19923828125f;      // 199.24 ms
        const float loop_drift = 0.9838169861f;             // 98.38%
        const float spk_recirc_amt = 0.9955357361f;         // 99.55%
        const float pickup_sat_amt = 1.0f;                  // 100.0%
        const float air_wobble_rate = 2.50f;                // 2.50 Hz
        const float air_wobble_depth = 0.498046875f;        // 49.80%
        const float trig_lfo_rate = 1.87862694f;            // 1.88 Hz
        const float trig_lfo_depth = 0.0588727713f;         // 5.89%
        const bool hold_on_pluck = true;                    // Hold Drone active

        // Slew rates
        float pedal_atk_rate = 1.0f - expf(-1.0f / (0.025f * (float)sample_rate));
        float pedal_rel_rate = 1.0f - expf(-1.0f / (0.040f * (float)sample_rate));

        float distress_drive = distress_knob * 1.8f + 0.6f;
        float asym_amount = 0.15f + distress_knob * 0.25f;

        for (uint32_t i = 0; i < sample_count; ++i) {
            float in_l = p_in_l[i];
            float in_r = (p_in_r ? p_in_r[i] : in_l);
            float in_mono = 0.5f * (in_l + in_r);

            // Envelope detection
            float in_rect = fabsf(in_mono);
            if (in_rect > guitar_env) {
                guitar_env += env_atk_coeff * (in_rect - guitar_env);
            } else {
                guitar_env += env_rel_coeff * (in_rect - guitar_env);
            }

            // Pluck detection
            bool is_new_pluck = false;
            if (in_rect > fast_env * 2.2f && in_rect > 0.025f) {
                is_new_pluck = true;
            }
            if (in_rect > fast_env) fast_env += 0.15f * (in_rect - fast_env);
            else fast_env += 0.002f * (in_rect - fast_env);

            // Trigger Wiggle LFO
            trig_lfo_phase += (float)(2.0 * M_PI * trig_lfo_rate / sample_rate);
            if (trig_lfo_phase >= 2.0f * (float)M_PI) trig_lfo_phase -= 2.0f * (float)M_PI;

            float lfo_offset = sinf(trig_lfo_phase) * trig_lfo_depth;
            float modulated_trigger = target_trigger;
            if (target_trigger > 0.05f && trig_lfo_depth > 0.001f) {
                modulated_trigger = std::max(0.0f, std::min(1.0f, target_trigger + lfo_offset));
            }

            if (modulated_trigger > smoothed_trigger) {
                smoothed_trigger += (modulated_trigger - smoothed_trigger) * pedal_atk_rate;
            } else {
                smoothed_trigger += (modulated_trigger - smoothed_trigger) * pedal_rel_rate;
            }

            // Heel-down cleanup only when pedal is genuinely released
            if (smoothed_trigger < 0.005f && target_trigger < 0.01f) {
                is_sustaining = false;
                string_sim.reset();
                room_sim.reset();
                float lim_l, lim_r;
                output_limiter.process(in_l, in_r, lim_l, lim_r);
                p_out_l[i] = lim_l;
                if (p_out_r) p_out_r[i] = lim_r;
                continue;
            }

            // Acoustic Leveler for pre-takeover bloom
            float max_leveler_gain = 30.0f + smoothed_trigger * 220.0f * (0.8f + gain_knob * 0.4f);
            float floor_offset = 0.010f * (1.0f - smoothed_trigger * 0.90f);

            float sustain_gain = 1.0f;
            if (guitar_env > 0.00001f) {
                sustain_gain = 0.26f / (guitar_env + floor_offset);
                if (sustain_gain > max_leveler_gain) sustain_gain = max_leveler_gain;
            }

            float sustained_l = in_l * sustain_gain;
            float sustained_r = in_r * sustain_gain;

            // Physical String Vibration Process (Infinite Circular Sustainer)
            float string_out_l, string_out_r;
            string_sim.process(sustained_l, sustained_r,
                               smoothed_trigger, guitar_env, is_new_pluck,
                               hold_on_pluck,
                               takeover_threshold, transition_sec, loop_sec,
                               damping_hz, vol_match_ratio,
                               attack_lockout_sec, loop_attack_sec,
                               loop_release_sec, loop_drift,
                               pickup_sat_amt, air_wobble_rate, air_wobble_depth,
                               sample_rate,
                               string_out_l, string_out_r);

            // Supercharged Speaker Distress
            float distressed_l = distress_l.process(string_out_l, distress_drive, asym_amount, sample_rate);
            float distressed_r = distress_r.process(string_out_r, distress_drive, asym_amount, sample_rate);

            // Cabinet Reflections
            float tailed_l = tail_diffuser_l.process(distressed_l, tail_sec, sample_rate);
            float tailed_r = tail_diffuser_r.process(distressed_r, tail_sec, sample_rate);

            float acoustic_feedback_l = distressed_l * 0.75f + tailed_l * 0.45f;
            float acoustic_feedback_r = distressed_r * 0.75f + tailed_r * 0.45f;

            // Room Simulator
            float room_l, room_r;
            room_sim.process(acoustic_feedback_l, acoustic_feedback_r,
                             room_size_sec, room_decay_sec, room_damping_hz,
                             sample_rate,
                             room_l, room_r);

            // Closed-Loop Injection
            float total_acoustic_return_l = acoustic_feedback_l * spk_recirc_amt;
            float total_acoustic_return_r = acoustic_feedback_r * spk_recirc_amt;

            if (room_in_loop) {
                total_acoustic_return_l += room_l * room_feed_amt;
                total_acoustic_return_r += room_r * room_feed_amt;
            }

            string_sim.inject_acoustic_return(total_acoustic_return_l, total_acoustic_return_r, 1.0f);

            // Output blending: wet amount tied strictly to expression pedal engagement
            float processed_feedback_l = acoustic_feedback_l + room_l * room_mix_amt;
            float processed_feedback_r = acoustic_feedback_r + room_r * room_mix_amt;

            float wet_amount = smoothed_trigger * mix_knob * (1.0f + gain_knob * 0.6f);
            float raw_out_l = in_l + processed_feedback_l * wet_amount;
            float raw_out_r = in_r + processed_feedback_r * wet_amount;

            float limited_l, limited_r;
            output_limiter.process(raw_out_l, raw_out_r, limited_l, limited_r);

            p_out_l[i] = limited_l;
            if (p_out_r) p_out_r[i] = limited_r;
        }
    }

    void reset() {
        distress_l.reset();
        distress_r.reset();
        tail_diffuser_l.init();
        tail_diffuser_r.init();
        room_sim.reset();
        output_limiter.init(sample_rate, -6.0f);
        string_sim.reset();
        trig_lfo_phase = 0.0f;
        guitar_env = 0.0f;
        fast_env = 0.0f;
        is_sustaining = false;
        smoothed_trigger = 0.0f;
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
    return (LV2_Handle)new CyberFeedbackRoom(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberFeedbackRoom*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    ((CyberFeedbackRoom*)instance)->reset();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberFeedbackRoom*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {
    (void)instance;
}

static void cleanup(LV2_Handle instance) {
    delete (CyberFeedbackRoom*)instance;
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
    return index == 0 ? &descriptor : NULL;
}
