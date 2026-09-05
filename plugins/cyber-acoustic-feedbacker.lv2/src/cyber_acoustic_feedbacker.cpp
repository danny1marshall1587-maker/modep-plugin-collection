/*
 * Cyber Acoustic Feedbacker & Natural Infinite Sustainer - LV2 Plugin
 * Copyright (c) 2026 Cyber Audio
 *
 * Physical Closed-Loop Electro-Acoustic String Feedback Simulator (Dev Edition 8):
 *  - 100% Pristine Dry Signal Path with zero latency.
 *  - Continuous Expression-Centered Wiggle LFO:
 *    * Expression pedal sets the center bias point.
 *    * An independent, smooth LFO wiggles the trigger value above and below the center,
 *      creating an organic, rhythmic cyclic engagement/disengagement wave across the threshold.
 *  - Hold-Drone Pluck Policy (Play Over Sustained Feedback):
 *    * When fully pressed or hold-drone is enabled, new notes picked do NOT cut off
 *      the active feedback drone, allowing you to play lead/chords while the feedback
 *      loop soars underneath.
 *  - Integrated High-Density Acoustic Early-Reflection Room Simulator:
 *    * Toggle to route room sound inside the closed loop to completely blur seams.
 *  - Closed-Loop Speaker Distress Recirculation, Magnetic Pickup Saturation, Karplus String Damping.
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
#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-acoustic-feedbacker"
#endif

enum PortIndex {
    PORT_AUDIO_IN_L        = 0,
    PORT_AUDIO_IN_R        = 1,
    PORT_AUDIO_OUT_L       = 2,
    PORT_AUDIO_OUT_R       = 3,
    PORT_BYPASS            = 4,
    PORT_TRIGGER           = 5,  // Feedback / Expression Pedal (0.0 to 1.0)
    PORT_GAIN              = 6,  // Feedback Gain (0 to 100%)
    PORT_DISTRESS          = 7,  // Speaker Distress (0 to 100%)
    PORT_TAIL              = 8,  // Tail Decay Time (0.1 to 5.0 s)
    PORT_MIX               = 9,  // Feedback Mix (0 to 100%)

    // Dev Tuner Physical & Acoustic Calibration Ports
    PORT_TAKEOVER_THRESH   = 10, // Takeover Threshold (70% to 100%, default 90%)
    PORT_TRANSITION_TIME   = 11, // Transition Fade Time (50ms to 20000ms, default 500ms)
    PORT_LOOP_WINDOW       = 12, // String Window Length (20ms to 500ms, default 120ms)
    PORT_LOOP_DAMPING      = 13, // String High Damping (1000Hz to 18000Hz, default 7500Hz)
    PORT_VOL_MATCH         = 14, // Volume Match Ratio (50% to 150%, default 100%)
    PORT_BLOOM_RATE        = 15, // Bloom Rise Time (0.1s to 5.0s, default 0.8s)
    PORT_ATTACK_LOCKOUT    = 16, // Attack Lockout Delay (50ms to 800ms, default 200ms)
    PORT_LOOP_ATTACK       = 17, // Ingress Attack Ramp (10ms to 300ms, default 50ms)
    PORT_LOOP_RELEASE      = 18, // Seam Release Vector (5ms to 200ms, default 40ms)
    PORT_LOOP_DRIFT        = 19, // Micro-Phase Drift (0 to 100%, default 25%)

    // Physical Electro-Acoustic Coupling Controls
    PORT_SPK_RECIRC        = 20, // Speaker -> String Recirculation (0% to 100%, default 60%)
    PORT_PICKUP_SAT        = 21, // Magnetic Pickup Core Saturation (0% to 100%, default 40%)
    PORT_AIR_WOBBLE_RATE   = 22, // Acoustic Standing Wave Rate (0.05Hz to 2.5Hz, default 0.45Hz)
    PORT_AIR_WOBBLE_DEPTH  = 23, // Acoustic Standing Wave Depth (0% to 50%, default 15%)

    // Room Simulation & Feedback Matrix Controls
    PORT_ROOM_IN_LOOP      = 24, // Toggle: 0.0 = Room on Output, 1.0 = Room in Feedback Loop
    PORT_ROOM_SIZE         = 25, // Room Size / Reflection Delay (5ms to 300ms, default 45ms)
    PORT_ROOM_DECAY        = 26, // Room Reflection Decay (0.1s to 3.0s, default 0.8s)
    PORT_ROOM_DAMPING      = 27, // Room Wall Damping (1000Hz to 16000Hz, default 6500Hz)
    PORT_ROOM_MIX          = 28, // Room Level in Output (0% to 100%, default 30%)
    PORT_ROOM_FEED         = 29, // Room Recirculation into Loop (0% to 100%, default 50%)

    // Continuous Trigger Modulation & Hold-Drone Controls
    PORT_TRIG_LFO_RATE     = 30, // Trigger Wiggle LFO Rate (0.05Hz to 5.0Hz, default 0.6Hz)
    PORT_TRIG_LFO_DEPTH    = 31, // Trigger Wiggle LFO Depth (0% to 50%, default 20%)
    PORT_HOLD_ON_PLUCK     = 32  // Toggle: 1.0 = Keep Sustaining Drone when picking new notes
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
        spkAtk = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0015f));      // 1.5ms attack
        spkRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.045f));       // 45ms release
        spkThermalRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.350f));// 350ms thermal sag
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

        // Compliance & thermal compression
        float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 2.0f);
        float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.50f);
        s = s * comp * thermalComp;

        // Asymmetric cone saturation
        float coneStress = s * (1.0f + driveAmount * 1.6f);
        float t = tanhf(coneStress);
        float coneOut = t - asymAmount * (t * t);

        // Dynamic voice-coil damping
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
        if (fb > 0.85f) fb = 0.85f;

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

        float recirc_a = in_mono + damp_b * fb * 0.5f - damp_c * fb * 0.3f;
        float recirc_b = in_mono + damp_c * fb * 0.5f - damp_d * fb * 0.3f;
        float recirc_c = in_mono + damp_d * fb * 0.5f - damp_a * fb * 0.3f;
        float recirc_d = in_mono + damp_a * fb * 0.5f - damp_b * fb * 0.3f;

        buf_a[write_pos] = tanhf(recirc_a);
        buf_b[write_pos] = tanhf(recirc_b);
        buf_c[write_pos] = tanhf(recirc_c);
        buf_d[write_pos] = tanhf(recirc_d);

        write_pos = (write_pos + 1) & (MAX_ROOM_SAMPLES - 1);

        out_room_l = (damp_a + damp_c - damp_b * 0.5f) * 0.55f;
        out_room_r = (damp_b + damp_d - damp_a * 0.5f) * 0.55f;
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
// Physical Closed-Loop Vibrating String Simulator with Pluck Drone Policy
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
            // Pluck Policy: If hold_on_pluck is true (or pedal is pushed all the way forward),
            // do NOT unlock or kill the active sustaining loop! Continue sustaining.
            if (!hold_on_pluck && effective_trigger < 0.95f) {
                is_locked = false;
                crossfade_progress = 0.0f;
                acoustic_return_l = acoustic_return_r = 0.0f;
            }
        } else if (samples_since_pluck < 2000000) {
            samples_since_pluck++;
        }

        // 1. Live capture buffer
        if (!is_locked) {
            buf_l[write_idx] = in_l;
            buf_r[write_idx] = in_r;
            write_idx = (write_idx + 1) & (MAX_BUF - 1);
        } else {
            // Closed-loop: softly blend returning acoustic sound wave back into the string
            float existing_l = buf_l[write_idx];
            float existing_r = buf_r[write_idx];
            buf_l[write_idx] = existing_l * 0.70f + acoustic_return_l * 0.30f;
            buf_r[write_idx] = existing_r * 0.70f + acoustic_return_r * 0.30f;
            write_idx = (write_idx + 1) & (MAX_BUF - 1);
        }

        // 2. RMS tracking
        float inst_power = 0.5f * (in_l * in_l + in_r * in_r);
        ring_rms += 0.005f * (inst_power - ring_rms);

        // 3. Transient Lockout & Tail Verification
        uint32_t lockout_samples = (uint32_t)(attack_lockout_sec * (float)sample_rate);
        bool past_attack_phase = (samples_since_pluck >= lockout_samples);
        bool note_is_decaying_or_flat = (env_velocity <= 0.0002f);
        bool can_latch = past_attack_phase && note_is_decaying_or_flat && (guitar_env > 0.0015f);

        bool trigger_active = (effective_trigger >= takeover_threshold);

        if (trigger_active) {
            if (!is_locked && can_latch) {
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
                float fade_rate = 1.0f / (transition_sec * (float)sample_rate + 1.0f);
                crossfade_progress = std::min(1.0f, crossfade_progress + fade_rate);
            }
        } else {
            float fade_rate = 1.0f / (0.060f * (float)sample_rate + 1.0f);
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

        // 6. Magnetic Pickup Saturation
        float pickup_l = saturate_pickup(raw_string_l, pickup_sat_amt);
        float pickup_r = saturate_pickup(raw_string_r, pickup_sat_amt);

        // 7. String Damping Filter
        float damp_w = 2.0f * (float)M_PI * damping_hz / (float)sample_rate;
        float damp_a = damp_w / (1.0f + damp_w);
        string_damp_l += damp_a * (pickup_l - string_damp_l);
        string_damp_r += damp_a * (pickup_r - string_damp_r);

        float sustained_l = string_damp_l;
        float sustained_r = string_damp_r;

        // 8. Equal-power sinusoidal crossfade
        float mix_wet = sinf(crossfade_progress * (float)M_PI * 0.5f);
        float mix_dry = cosf(crossfade_progress * (float)M_PI * 0.5f);

        out_l = in_l * mix_dry + sustained_l * mix_wet;
        out_r = in_r * mix_dry + sustained_r * mix_wet;
    }
};

// -------------------------------------------------------------------------
// Main CyberAcousticFeedbacker Plugin Class
// -------------------------------------------------------------------------
class CyberAcousticFeedbacker {
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
    float tail_env;

    float smoothed_trigger;

    // LV2 Port Pointers
    const float* p_in_l;
    const float* p_in_r;
    float* p_out_l;
    float* p_out_r;
    const float* p_bypass;
    const float* p_trigger;
    const float* p_gain;
    const float* p_distress;
    const float* p_tail;
    const float* p_mix;

    // Dev Tuner Port Pointers
    const float* p_takeover_thresh;
    const float* p_transition_time;
    const float* p_loop_window;
    const float* p_loop_damping;
    const float* p_vol_match;
    const float* p_bloom_rate;
    const float* p_attack_lockout;
    const float* p_loop_attack;
    const float* p_loop_release;
    const float* p_loop_drift;

    // Electro-Acoustic Coupling Controls
    const float* p_spk_recirc;
    const float* p_pickup_sat;
    const float* p_air_wobble_rate;
    const float* p_air_wobble_depth;

    // Room Simulation & Routing Controls
    const float* p_room_in_loop;
    const float* p_room_size;
    const float* p_room_decay;
    const float* p_room_damping;
    const float* p_room_mix;
    const float* p_room_feed;

    // Continuous Trigger Modulation & Hold-Drone Controls
    const float* p_trig_lfo_rate;
    const float* p_trig_lfo_depth;
    const float* p_hold_on_pluck;

public:
    CyberAcousticFeedbacker(double sr) : sample_rate(sr) {
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
        tail_env = 0.0f;
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
            case PORT_GAIN:              p_gain = (const float*)data; break;
            case PORT_DISTRESS:          p_distress = (const float*)data; break;
            case PORT_TAIL:              p_tail = (const float*)data; break;
            case PORT_MIX:               p_mix = (const float*)data; break;
            case PORT_TAKEOVER_THRESH:   p_takeover_thresh = (const float*)data; break;
            case PORT_TRANSITION_TIME:   p_transition_time = (const float*)data; break;
            case PORT_LOOP_WINDOW:       p_loop_window = (const float*)data; break;
            case PORT_LOOP_DAMPING:      p_loop_damping = (const float*)data; break;
            case PORT_VOL_MATCH:         p_vol_match = (const float*)data; break;
            case PORT_BLOOM_RATE:        p_bloom_rate = (const float*)data; break;
            case PORT_ATTACK_LOCKOUT:    p_attack_lockout = (const float*)data; break;
            case PORT_LOOP_ATTACK:       p_loop_attack = (const float*)data; break;
            case PORT_LOOP_RELEASE:      p_loop_release = (const float*)data; break;
            case PORT_LOOP_DRIFT:        p_loop_drift = (const float*)data; break;
            case PORT_SPK_RECIRC:        p_spk_recirc = (const float*)data; break;
            case PORT_PICKUP_SAT:        p_pickup_sat = (const float*)data; break;
            case PORT_AIR_WOBBLE_RATE:   p_air_wobble_rate = (const float*)data; break;
            case PORT_AIR_WOBBLE_DEPTH:  p_air_wobble_depth = (const float*)data; break;
            case PORT_ROOM_IN_LOOP:      p_room_in_loop = (const float*)data; break;
            case PORT_ROOM_SIZE:         p_room_size = (const float*)data; break;
            case PORT_ROOM_DECAY:        p_room_decay = (const float*)data; break;
            case PORT_ROOM_DAMPING:      p_room_damping = (const float*)data; break;
            case PORT_ROOM_MIX:          p_room_mix = (const float*)data; break;
            case PORT_ROOM_FEED:         p_room_feed = (const float*)data; break;
            case PORT_TRIG_LFO_RATE:     p_trig_lfo_rate = (const float*)data; break;
            case PORT_TRIG_LFO_DEPTH:    p_trig_lfo_depth = (const float*)data; break;
            case PORT_HOLD_ON_PLUCK:     p_hold_on_pluck = (const float*)data; break;
        }
    }

    void run(uint32_t sample_count) {
        bool bypass = (p_bypass && *p_bypass < 0.5f);
        if (bypass) {
            if (p_out_l != p_in_l) memcpy(p_out_l, p_in_l, sample_count * sizeof(float));
            if (p_out_r && p_in_r && p_out_r != p_in_r) memcpy(p_out_r, p_in_r, sample_count * sizeof(float));
            return;
        }

        // Primary controls
        float raw_trigger = (p_trigger ? *p_trigger : 0.0f);
        float target_trigger = std::max(0.0f, std::min(1.0f, raw_trigger));
        float gain_knob = (p_gain ? *p_gain : 75.0f) * 0.01f;
        float distress_knob = (p_distress ? *p_distress : 50.0f) * 0.01f;
        float tail_sec = std::max(0.1f, std::min(5.0f, (p_tail ? *p_tail : 1.8f)));
        float mix_knob = (p_mix ? *p_mix : 50.0f) * 0.01f;

        // Dev Tuner parameters
        float takeover_threshold = (p_takeover_thresh ? *p_takeover_thresh : 90.0f) * 0.01f;
        float transition_sec = std::max(0.05f, std::min(20.0f, (p_transition_time ? *p_transition_time : 500.0f) * 0.001f));
        float loop_sec = (p_loop_window ? *p_loop_window : 120.0f) * 0.001f;
        float damping_hz = std::max(1000.0f, std::min(18000.0f, (p_loop_damping ? *p_loop_damping : 7500.0f)));
        float vol_match_ratio = (p_vol_match ? *p_vol_match : 100.0f) * 0.01f;
        float bloom_sec = std::max(0.1f, std::min(5.0f, (p_bloom_rate ? *p_bloom_rate : 0.8f)));
        float attack_lockout_sec = (p_attack_lockout ? *p_attack_lockout : 200.0f) * 0.001f;
        float loop_attack_sec = (p_loop_attack ? *p_loop_attack : 50.0f) * 0.001f;
        float loop_release_sec = (p_loop_release ? *p_loop_release : 40.0f) * 0.001f;
        float loop_drift = (p_loop_drift ? *p_loop_drift : 25.0f) * 0.01f;

        // Electro-Acoustic Coupling Controls
        float spk_recirc_amt = (p_spk_recirc ? *p_spk_recirc : 60.0f) * 0.01f;
        float pickup_sat_amt = (p_pickup_sat ? *p_pickup_sat : 40.0f) * 0.01f;
        float air_wobble_rate = std::max(0.05f, std::min(2.5f, (p_air_wobble_rate ? *p_air_wobble_rate : 0.45f)));
        float air_wobble_depth = (p_air_wobble_depth ? *p_air_wobble_depth : 15.0f) * 0.01f;

        // Room Simulation Controls
        bool room_in_loop = (p_room_in_loop && *p_room_in_loop > 0.5f);
        float room_size_sec = (p_room_size ? *p_room_size : 45.0f) * 0.001f;
        float room_decay_sec = std::max(0.1f, std::min(3.0f, (p_room_decay ? *p_room_decay : 0.8f)));
        float room_damping_hz = std::max(1000.0f, std::min(16000.0f, (p_room_damping ? *p_room_damping : 6500.0f)));
        float room_mix_amt = (p_room_mix ? *p_room_mix : 30.0f) * 0.01f;
        float room_feed_amt = (p_room_feed ? *p_room_feed : 50.0f) * 0.01f;

        // Trigger Modulation & Hold-Drone Controls
        float trig_lfo_rate = std::max(0.05f, std::min(5.0f, (p_trig_lfo_rate ? *p_trig_lfo_rate : 0.6f)));
        float trig_lfo_depth = (p_trig_lfo_depth ? *p_trig_lfo_depth : 20.0f) * 0.01f;
        bool hold_on_pluck = (p_hold_on_pluck && *p_hold_on_pluck > 0.5f);

        // Slew rates
        float pedal_atk_rate = 1.0f - expf(-1.0f / (0.025f * (float)sample_rate));
        float pedal_rel_rate = 1.0f - expf(-1.0f / (0.040f * (float)sample_rate));
        float tail_atk_rate = 1.0f - expf(-1.0f / (0.05f * (float)sample_rate));
        float tail_rel_rate = 1.0f - expf(-1.0f / (tail_sec * (float)sample_rate));

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

            // Continuous Trigger Wiggle LFO (modulates around user pedal position)
            trig_lfo_phase += (float)(2.0 * M_PI * trig_lfo_rate / sample_rate);
            if (trig_lfo_phase >= 2.0f * (float)M_PI) trig_lfo_phase -= 2.0f * (float)M_PI;

            float lfo_offset = sinf(trig_lfo_phase) * trig_lfo_depth;
            // Modulated trigger: pedal acts as center bias
            float modulated_trigger = target_trigger;
            if (target_trigger > 0.05f && trig_lfo_depth > 0.001f) {
                modulated_trigger = std::max(0.0f, std::min(1.0f, target_trigger + lfo_offset));
            }

            // Slew tracking of effective trigger
            if (modulated_trigger > smoothed_trigger) {
                smoothed_trigger += (modulated_trigger - smoothed_trigger) * pedal_atk_rate;
            } else {
                smoothed_trigger += (modulated_trigger - smoothed_trigger) * pedal_rel_rate;
            }

            // Heel-down cleanup
            if (smoothed_trigger < 0.005f && target_trigger < 0.01f) {
                is_sustaining = false;
                tail_env = 0.0f;
                string_sim.reset();
                room_sim.reset();
                float lim_l, lim_r;
                output_limiter.process(in_l, in_r, lim_l, lim_r);
                p_out_l[i] = lim_l;
                if (p_out_r) p_out_r[i] = lim_r;
                continue;
            }

            // Sustaining state
            if (guitar_env > 0.0015f) {
                is_sustaining = true;
            } else if (smoothed_trigger >= takeover_threshold && is_sustaining) {
                is_sustaining = true;
            } else if (guitar_env < 0.00015f) {
                is_sustaining = false;
            }

            float target_tail_env = (is_sustaining && smoothed_trigger > 0.02f) ? 1.0f : 0.0f;
            if (target_tail_env > tail_env) tail_env += (target_tail_env - tail_env) * tail_atk_rate;
            else tail_env += (target_tail_env - tail_env) * tail_rel_rate;

            // Pure Unpitched Acoustic Leveler
            float max_leveler_gain = 30.0f + smoothed_trigger * 220.0f * (0.8f + gain_knob * 0.4f);
            float floor_offset = 0.010f * (1.0f - smoothed_trigger * 0.90f);

            float sustain_gain = 1.0f;
            if (guitar_env > 0.00001f) {
                sustain_gain = 0.26f / (guitar_env + floor_offset);
                if (sustain_gain > max_leveler_gain) sustain_gain = max_leveler_gain;
            }

            float sustained_l = in_l * sustain_gain * tail_env;
            float sustained_r = in_r * sustain_gain * tail_env;

            // Physical String Vibration Process
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

            // Output blending
            float processed_feedback_l = acoustic_feedback_l + room_l * room_mix_amt;
            float processed_feedback_r = acoustic_feedback_r + room_r * room_mix_amt;

            float wet_amount = smoothed_trigger * mix_knob * (1.0f + gain_knob * 0.6f) * tail_env;
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
        tail_env = 0.0f;
        smoothed_trigger = 0.0f;
    }
};

// -------------------------------------------------------------------------
// LV2 C API Wrapper
// -------------------------------------------------------------------------
static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double rate,
                             const char* bundle_path,
                             const LV2_Feature* const* features) {
    (void)descriptor;
    (void)bundle_path;
    (void)features;
    return (LV2_Handle)new CyberAcousticFeedbacker(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberAcousticFeedbacker*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    ((CyberAcousticFeedbacker*)instance)->reset();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberAcousticFeedbacker*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {
    // No specific deactivation
}

static void cleanup(LV2_Handle instance) {
    delete (CyberAcousticFeedbacker*)instance;
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

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : NULL;
}
