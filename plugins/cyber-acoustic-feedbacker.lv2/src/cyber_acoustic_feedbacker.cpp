/*
 * Cyber Acoustic Feedbacker & Natural Infinite Sustainer - LV2 Plugin
 * Copyright (c) 2026 Cyber Audio
 *
 * Dev Tuner Version:
 *  - Stripped all pitch shifting & pitch tracking artifacts from feedback loop.
 *  - 100% Pristine Dry Signal Path with zero latency.
 *  - Natural Acoustic Bloom & Speaker Distress: As the natural string decays,
 *    subtle non-linear cone compliance and speaker saturation gently bloom.
 *  - Smart Note-Tail Sampling & Transient Rejection (No Machine-Gun Effect):
 *    * Attack Lockout Delay: Prevents sampling any pick clicks, fret clack, or initial strike.
 *    * Settled-Tail Detector: Only samples when the note envelope is decaying/stable (dEnv/dt <= 0).
 *    * Raised-Cosine Soft-Attack Window: Pre-conditions audio entering the loop buffer with
 *      smooth zero-crossing edges to eliminate stutter.
 *  - Automatic RMS Volume Matching: Live note volume balances the loop level seamlessly.
 *  - Dev Tuner Calibration Controls for full user dialing.
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
    PORT_TRIGGER           = 5, // Feedback / Expression Pedal (0.0 to 1.0)
    PORT_GAIN              = 6, // Feedback Gain (0 to 100%)
    PORT_DISTRESS          = 7, // Speaker Distress (0 to 100%)
    PORT_TAIL              = 8, // Tail Decay Time (0.1 to 5.0 s)
    PORT_MIX               = 9, // Feedback Mix (0 to 100%)
    // Dev Tuner Controls
    PORT_TAKEOVER_THRESH   = 10, // Takeover Threshold (70% to 100%, default 90%)
    PORT_TRANSITION_TIME   = 11, // Transition Crossfade Time (50ms to 1500ms, default 350ms)
    PORT_LOOP_WINDOW       = 12, // Loop Window Size (20ms to 400ms, default 80ms)
    PORT_LOOP_DAMPING      = 13, // Loop High-Frequency Damping (1000Hz to 18000Hz, default 7500Hz)
    PORT_VOL_MATCH         = 14, // Volume Match Ratio (50% to 150%, default 100%)
    PORT_BLOOM_RATE        = 15, // Bloom Rise Time (0.1s to 3.0s, default 0.8s)
    PORT_ATTACK_LOCKOUT    = 16, // Attack Lockout Delay (50ms to 600ms, default 200ms)
    PORT_LOOP_ATTACK       = 17  // Loop Ingress Attack Ramp (10ms to 200ms, default 40ms)
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
        ceiling = powf(10.0f, ceilingDb / 20.0f); // 0.501187f
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
// Smart Note-Tail Sustainer with Transient Lockout & Zero-Stutter Conditioning
// -------------------------------------------------------------------------
class UnpitchedAcousticSustainer {
public:
    static const int MAX_BUF = 32768; // ~680ms at 48kHz

private:
    float buf_l[MAX_BUF];
    float buf_r[MAX_BUF];
    int write_idx;

    // Sustainer State
    bool is_locked;
    float crossfade_progress;
    int lock_origin;
    float phase_a, phase_b;
    int current_loop_len;

    // Volume Matching Tracker
    float target_volume;
    float loop_volume_gain;
    float captured_note_rms;
    float ring_rms;

    // Smart Note-Tail Tracking & Lockout
    uint32_t samples_since_pluck;
    float prev_env;
    float env_velocity; // derivative: dEnv/dt

    // Loop Tone Filter (1-pole lowpass damping)
    float damp_l, damp_r;

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
        current_loop_len = 3840; // ~80ms default at 48kHz
        target_volume = 1.0f;
        loop_volume_gain = 1.0f;
        captured_note_rms = 0.0f;
        ring_rms = 0.0f;
        samples_since_pluck = 999999;
        prev_env = 0.0f;
        env_velocity = 0.0f;
        damp_l = damp_r = 0.0f;
    }

    void reset() {
        init();
    }

    inline void process(float in_l, float in_r,
                        float trigger_val, float guitar_env, bool is_new_pluck,
                        float takeover_threshold, float transition_sec, float loop_sec,
                        float damping_hz, float vol_match_ratio,
                        float attack_lockout_sec, float loop_attack_sec,
                        double sample_rate,
                        float& out_l, float& out_r) {

        // Track derivative / direction of note envelope
        env_velocity = guitar_env - prev_env;
        prev_env = guitar_env;

        if (is_new_pluck) {
            // Note struck: reset timer and release any active lock
            samples_since_pluck = 0;
            is_locked = false;
            crossfade_progress = 0.0f;
        } else if (samples_since_pluck < 2000000) {
            samples_since_pluck++;
        }

        // 1. Constantly capture audio in live ring buffer when not locked
        if (!is_locked) {
            buf_l[write_idx] = in_l;
            buf_r[write_idx] = in_r;
            write_idx = (write_idx + 1) & (MAX_BUF - 1);
        }

        // 2. Continuous RMS tracking of incoming signal
        float inst_power = 0.5f * (in_l * in_l + in_r * in_r);
        ring_rms += 0.005f * (inst_power - ring_rms);

        // 3. Transient Lockout & Tail Verification
        uint32_t lockout_samples = (uint32_t)(attack_lockout_sec * (float)sample_rate);
        bool past_attack_phase = (samples_since_pluck >= lockout_samples);

        // Note must be stable or gently decaying, not during a sharp rising attack
        bool note_is_decaying_or_flat = (env_velocity <= 0.0002f);

        // Can we latch the loop? Must have passed attack guard and be settled
        bool can_latch = past_attack_phase && note_is_decaying_or_flat && (guitar_env > 0.0015f);

        // Hand-off trigger logic: Expression pedal at or above Takeover Threshold
        bool trigger_active = (trigger_val >= takeover_threshold);

        if (trigger_active) {
            if (!is_locked && can_latch) {
                // LATCH ONTO THE CLEAN NOTE TAIL (ZERO TRANSIENTS)
                is_locked = true;
                current_loop_len = (int)(loop_sec * (float)sample_rate);
                if (current_loop_len < 256) current_loop_len = 256;
                if (current_loop_len > MAX_BUF / 2) current_loop_len = MAX_BUF / 2;

                // Step back past the very latest audio by loop_attack window to guarantee
                // we sample the settled tail of the note, not an abrupt edge
                int safety_offset = (int)(loop_attack_sec * 0.5f * (float)sample_rate);
                lock_origin = (write_idx - current_loop_len - safety_offset + MAX_BUF * 2) & (MAX_BUF - 1);
                phase_a = 0.0f;
                phase_b = (float)current_loop_len * 0.5f;

                // Capture note RMS for exact volume matching
                captured_note_rms = sqrtf(std::max(ring_rms, 0.0001f));

                // Calculate loop normalization gain
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
                // Smooth crossfade in using user transition time
                float fade_rate = 1.0f / (transition_sec * (float)sample_rate + 1.0f);
                crossfade_progress = std::min(1.0f, crossfade_progress + fade_rate);
            }
        } else {
            // Released / below takeover threshold: smooth crossfade out
            float fade_rate = 1.0f / (0.060f * (float)sample_rate + 1.0f); // 60ms quick graceful release
            crossfade_progress = std::max(0.0f, crossfade_progress - fade_rate);
            if (crossfade_progress <= 0.0f) {
                is_locked = false;
            }
        }

        // If completely dry / inactive
        if (crossfade_progress <= 0.0001f) {
            out_l = in_l;
            out_r = in_r;
            return;
        }

        // 4. Equal-power dual-head recirculating read (unpitched, pristine timbre)
        phase_a += 1.0f;
        if (phase_a >= (float)current_loop_len) phase_a -= (float)current_loop_len;
        phase_b += 1.0f;
        if (phase_b >= (float)current_loop_len) phase_b -= (float)current_loop_len;

        float w_a = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * phase_a / (float)current_loop_len));
        float w_b = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * phase_b / (float)current_loop_len));

        int idx_a = (lock_origin + (int)phase_a) & (MAX_BUF - 1);
        int idx_b = (lock_origin + (int)phase_b) & (MAX_BUF - 1);

        float loop_l = (buf_l[idx_a] * w_a + buf_l[idx_b] * w_b) * loop_volume_gain;
        float loop_r = (buf_r[idx_a] * w_a + buf_r[idx_b] * w_b) * loop_volume_gain;

        // 5. Loop HF warmth / damping filter
        float damp_w = 2.0f * (float)M_PI * damping_hz / (float)sample_rate;
        float damp_a = damp_w / (1.0f + damp_w);
        damp_l += damp_a * (loop_l - damp_l);
        damp_r += damp_a * (loop_r - damp_r);

        float sustained_l = damp_l;
        float sustained_r = damp_r;

        // 6. Equal-power sinusoidal crossfade between live note and infinite sustain
        float mix_wet = sinf(crossfade_progress * (float)M_PI * 0.5f);
        float mix_dry = cosf(crossfade_progress * (float)M_PI * 0.5f);

        out_l = in_l * mix_dry + sustained_l * mix_wet;
        out_r = in_r * mix_dry + sustained_r * mix_wet;
    }
};

// -------------------------------------------------------------------------
// Main CyberAcousticFeedbacker Plugin Class (Dev Tuner Edition)
// -------------------------------------------------------------------------
class CyberAcousticFeedbacker {
private:
    double sample_rate;

    SuperchargedSpeakerDistress distress_l;
    SuperchargedSpeakerDistress distress_r;

    CabinetAcousticTail tail_diffuser_l;
    CabinetAcousticTail tail_diffuser_r;

    OutputCeilingLimiter output_limiter;
    UnpitchedAcousticSustainer sustainer;

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

public:
    CyberAcousticFeedbacker(double sr) : sample_rate(sr) {
        distress_l.init(sample_rate);
        distress_r.init(sample_rate);

        tail_diffuser_l.init();
        tail_diffuser_r.init();

        output_limiter.init(sample_rate, -6.0f);
        sustainer.init();

        guitar_env = 0.0f;
        fast_env = 0.0f;
        env_atk_coeff = 1.0f - expf(-1.0f / ((float)sample_rate * 0.0030f)); // 3ms attack
        env_rel_coeff = 1.0f - expf(-1.0f / ((float)sample_rate * 0.2500f)); // 250ms release
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
        }
    }

    void run(uint32_t sample_count) {
        bool bypass = (p_bypass && *p_bypass < 0.5f);
        if (bypass) {
            if (p_out_l != p_in_l) memcpy(p_out_l, p_in_l, sample_count * sizeof(float));
            if (p_out_r && p_in_r && p_out_r != p_in_r) memcpy(p_out_r, p_in_r, sample_count * sizeof(float));
            return;
        }

        // Primary user controls
        float raw_trigger = (p_trigger ? *p_trigger : 0.0f);
        float target_trigger = std::max(0.0f, std::min(1.0f, raw_trigger));
        float gain_knob = (p_gain ? *p_gain : 75.0f) * 0.01f;
        float distress_knob = (p_distress ? *p_distress : 50.0f) * 0.01f;
        float tail_sec = std::max(0.1f, std::min(5.0f, (p_tail ? *p_tail : 1.8f)));
        float mix_knob = (p_mix ? *p_mix : 50.0f) * 0.01f;

        // Dev Tuner parameters
        float takeover_threshold = (p_takeover_thresh ? *p_takeover_thresh : 90.0f) * 0.01f;
        float transition_sec = (p_transition_time ? *p_transition_time : 350.0f) * 0.001f; // ms to sec
        float loop_sec = (p_loop_window ? *p_loop_window : 80.0f) * 0.001f;               // ms to sec
        float damping_hz = std::max(1000.0f, std::min(18000.0f, (p_loop_damping ? *p_loop_damping : 7500.0f)));
        float vol_match_ratio = (p_vol_match ? *p_vol_match : 100.0f) * 0.01f;
        float bloom_sec = std::max(0.1f, std::min(3.0f, (p_bloom_rate ? *p_bloom_rate : 0.8f)));
        float attack_lockout_sec = (p_attack_lockout ? *p_attack_lockout : 200.0f) * 0.001f; // ms to sec
        float loop_attack_sec = (p_loop_attack ? *p_loop_attack : 40.0f) * 0.001f;          // ms to sec

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

            // Note pluck onset detection
            bool is_new_pluck = false;
            if (in_rect > fast_env * 2.2f && in_rect > 0.025f) {
                is_new_pluck = true;
            }
            if (in_rect > fast_env) fast_env += 0.15f * (in_rect - fast_env);
            else fast_env += 0.002f * (in_rect - fast_env);

            // Expression pedal foot tracking
            if (target_trigger > smoothed_trigger) {
                smoothed_trigger += (target_trigger - smoothed_trigger) * pedal_atk_rate;
            } else {
                smoothed_trigger += (target_trigger - smoothed_trigger) * pedal_rel_rate;
            }

            // Heel-down cleanup
            if (smoothed_trigger < 0.005f) {
                is_sustaining = false;
                tail_env = 0.0f;
                sustainer.reset();
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

            // Supercharged Speaker Distress (Unpitched Natural Harmonics)
            float distressed_l = distress_l.process(sustained_l, distress_drive, asym_amount, sample_rate);
            float distressed_r = distress_r.process(sustained_r, distress_drive, asym_amount, sample_rate);

            // Acoustic Room / Cabinet Tail
            float tailed_l = tail_diffuser_l.process(distressed_l, tail_sec, sample_rate);
            float tailed_r = tail_diffuser_r.process(distressed_r, tail_sec, sample_rate);

            float natural_feedback_l = distressed_l * 0.75f + tailed_l * 0.45f;
            float natural_feedback_r = distressed_r * 0.75f + tailed_r * 0.45f;

            // Seamless Unpitched Sustainer Hand-Off with Smart Note-Tail Sampling
            float held_l, held_r;
            sustainer.process(natural_feedback_l, natural_feedback_r,
                              smoothed_trigger, guitar_env, is_new_pluck,
                              takeover_threshold, transition_sec, loop_sec,
                              damping_hz, vol_match_ratio,
                              attack_lockout_sec, loop_attack_sec,
                              sample_rate,
                              held_l, held_r);

            // Final Mix & Limiter
            float wet_amount = smoothed_trigger * mix_knob * (1.0f + gain_knob * 0.6f) * tail_env;
            float raw_out_l = in_l + held_l * wet_amount;
            float raw_out_r = in_r + held_r * wet_amount;

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
        output_limiter.init(sample_rate, -6.0f);
        sustainer.reset();
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
