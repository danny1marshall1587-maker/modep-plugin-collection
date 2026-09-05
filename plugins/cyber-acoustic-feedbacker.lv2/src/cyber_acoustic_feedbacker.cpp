/*
 * Cyber Acoustic Feedbacker & Polyphonic Sustainer - LV2 Plugin
 * Copyright (c) 2026 Cyber Audio
 *
 * Physically-Modeled Acoustic String Feedback & Supercharged Speaker Distress:
 *  - 100% Pristine Dry Signal Path (Path A) with zero latency.
 *  - Infinite Toe-Down Sustain: When Expression Pedal is Toe Down (>= 80%),
 *    acoustic regeneration keeps the feedback singing indefinitely as long as you like.
 *  - Expression Heel Control: Pulling the pedal back to Heel gracefully fades
 *    out the feedback through the adjustable TAIL length to dead silence.
 *  - Instant Note Overwrite: Striking a new note instantly overpowers the old sustain.
 *  - Supercharged Speaker Distress Emulation: Pushed hard into non-linear cone
 *    compliance compression, voice-coil thermal sag, and asymmetric cone excursion
 *    to naturally blossom the guitar notes into singing 2nd/3rd harmonics.
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

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-acoustic-feedbacker"
#define GRAIN_BUF_SIZE 4096

enum PortIndex {
    PORT_AUDIO_IN_L    = 0,
    PORT_AUDIO_IN_R    = 1,
    PORT_AUDIO_OUT_L   = 2,
    PORT_AUDIO_OUT_R   = 3,
    PORT_BYPASS        = 4,
    PORT_TRIGGER       = 5,
    PORT_MODE          = 6,
    PORT_BLOOM         = 7,
    PORT_GAIN          = 8,
    PORT_HARMONIC      = 9,
    PORT_WARMTH        = 10,
    PORT_TAIL          = 11,
    PORT_MIX           = 12
};


// -------------------------------------------------------------------------
// Dual-Head Granular Harmonic Pitch Shifter (Feed-Forward, Zero-Estimate)
// -------------------------------------------------------------------------
class GranularPitchShifter {
private:
    float buffer[GRAIN_BUF_SIZE];
    int write_idx;
    float phase;

public:
    void init() {
        memset(buffer, 0, sizeof(buffer));
        write_idx = 0;
        phase = 0.0f;
    }

    inline float process(float in, float pitch_ratio) {
        buffer[write_idx] = in;

        // Unison optimization: clean low-delay tap
        if (fabsf(pitch_ratio - 1.0f) < 0.005f) {
            int read_idx = (write_idx - 64 + GRAIN_BUF_SIZE) & (GRAIN_BUF_SIZE - 1);
            write_idx = (write_idx + 1) & (GRAIN_BUF_SIZE - 1);
            return buffer[read_idx];
        }

        const int window_len = 1024; // ~21.3ms window at 48kHz
        float delta_phase = (1.0f - pitch_ratio) / (float)window_len;
        phase += delta_phase;
        while (phase < 0.0f) phase += 1.0f;
        while (phase >= 1.0f) phase -= 1.0f;

        float phase1 = phase;
        float phase2 = phase + 0.5f;
        if (phase2 >= 1.0f) phase2 -= 1.0f;

        // Raised-cosine crossfade windows (w1 + w2 == 1.0f)
        float w1 = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * phase1));
        float w2 = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * phase2));

        float delay1 = 64.0f + phase1 * (float)window_len;
        float delay2 = 64.0f + phase2 * (float)window_len;

        // Linear interpolation read for head 1
        float r1 = (float)write_idx - delay1;
        while (r1 < 0.0f) r1 += (float)GRAIN_BUF_SIZE;
        int i1 = (int)r1;
        float frac1 = r1 - (float)i1;
        int i1_next = (i1 + 1) & (GRAIN_BUF_SIZE - 1);
        float samp1 = buffer[i1] + frac1 * (buffer[i1_next] - buffer[i1]);

        // Linear interpolation read for head 2
        float r2 = (float)write_idx - delay2;
        while (r2 < 0.0f) r2 += (float)GRAIN_BUF_SIZE;
        int i2 = (int)r2;
        float frac2 = r2 - (float)i2;
        int i2_next = (i2 + 1) & (GRAIN_BUF_SIZE - 1);
        float samp2 = buffer[i2] + frac2 * (buffer[i2_next] - buffer[i2]);

        write_idx = (write_idx + 1) & (GRAIN_BUF_SIZE - 1);

        return (w1 * samp1 + w2 * samp2);
    }
};

// -------------------------------------------------------------------------
// Supercharged Speaker Distress & Cone Compliance Engine
// (Ported and maximized from Cyber Audio Dumble/Matchless Dynamic Speaker Circuit)
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
        spkAtk = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0015f));      // 1.5ms excursion attack
        spkRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.045f));       // 45ms excursion release
        spkThermalRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.350f));// 350ms thermal sag release
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

        // Dynamic compliance compression & thermal compression
        float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 2.2f);
        float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.55f);
        s = s * comp * thermalComp;

        // Asymmetrical physical cone stress non-linearity
        float coneStress = s * (1.0f + driveAmount * 1.8f);
        float t = tanhf(coneStress);
        float coneOut = t - asymAmount * (t * t);

        // Dynamic voice-coil HF mechanical damping as cone excursion grows
        float dampingFc = 6400.0f - driveAmount * 2600.0f;
        if (dampingFc < 2200.0f) dampingFc = 2200.0f;
        float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
        float a0 = w / (1.0f + w);
        speakerConeHistory += a0 * (coneOut - speakerConeHistory);

        s = (1.0f - driveAmount * 0.70f) * coneOut + (driveAmount * 0.70f) * speakerConeHistory;
        s *= (1.0f + driveAmount * 0.20f);
        return s;
    }
};

// -------------------------------------------------------------------------
// Acoustic Cabinet Decay Diffuser (Natural Tail Decay, Zero Comb Whistle)
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

        float rd3 = d3[idx3];
        s3 += 0.35f * (rd3 - s3);
        d3[idx3] = out2 + s3 * fb;
        if (++idx3 >= 1987) idx3 = 0;

        float rd4 = d4[idx4];
        s4 += 0.35f * (rd4 - s4);
        d4[idx4] = out2 + s4 * fb;
        if (++idx4 >= 2741) idx4 = 0;

        return (s3 + s4) * 0.5f;
    }
};

// -------------------------------------------------------------------------
// Master Output Ceiling Limiter (-6.0 dBFS Peak Clamping & Protection)
// Guarantees output bus never blasts past -6.0 dBFS (0.501187) with soft-knee transparency
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
        ceiling = powf(10.0f, ceilingDb / 20.0f); // 0.501187f for -6.0 dBFS
        knee_threshold = ceiling * 0.85f;         // 0.4260f (~ -7.4 dBFS)
        margin = ceiling - knee_threshold;        // 0.07518f
        gain_env = 1.0f;
        atk_coeff = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0005f)); // 0.5ms fast attack
        rel_coeff = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0600f)); // 60ms smooth release
    }

    inline void process(float in_l, float in_r, float& out_l, float& out_r) {
        float peak = std::max(fabsf(in_l), fabsf(in_r));
        float target_gain = 1.0f;
        if (peak > knee_threshold) {
            target_gain = knee_threshold / (peak + 1e-6f);
            if (target_gain > 1.0f) target_gain = 1.0f;
        }

        // Fast attack, smooth release
        if (target_gain < gain_env) {
            gain_env += atk_coeff * (target_gain - gain_env);
        } else {
            gain_env += rel_coeff * (target_gain - gain_env);
        }

        float scaled_l = in_l * gain_env;
        float scaled_r = in_r * gain_env;

        // Zero-overshoot soft-knee saturation clamp
        out_l = shape_sample(scaled_l);
        out_r = shape_sample(scaled_r);
    }

    inline float shape_sample(float x) {
        float ax = fabsf(x);
        if (ax <= knee_threshold) {
            return x; // 100% linear transparency below knee
        }
        float excess = ax - knee_threshold;
        float compressed = knee_threshold + margin * tanhf(excess / margin);
        return (x < 0.0f) ? -compressed : compressed;
    }
};

// -------------------------------------------------------------------------
// Silent Guitar Pitch Detector & Energy Compensation Engine
// Completely silent sidechain analysis - generates ZERO audio!
// Uses Normalized Square Difference Function (NSDF / McLeod Pitch Method)
// with zero-crossing gating and sub-sample parabolic interpolation.
// Tracks fundamental frequency f0 to provide dynamic energy boost for thin strings.
// -------------------------------------------------------------------------
class SilentGuitarPitchTracker {
public:
    static const int HISTORY_SIZE = 2048;
    static const int DEC_SIZE = 1024;
    static const int CORR_WINDOW = 384;

private:
    double sample_rate;
    double dec_sample_rate;
    float history[HISTORY_SIZE];
    int write_idx;
    int sample_count;
    int analysis_interval;

    float dec_buf[DEC_SIZE];
    float nsdf[400];

    float dc_x1, dc_y1;
    float detected_freq;
    float smoothed_freq;
    float energy_boost;
    float smoothed_boost;

    int min_lag;
    int max_lag;

public:
    void init(double sr) {
        sample_rate = sr > 0.0 ? sr : 48000.0;
        dec_sample_rate = sample_rate * 0.5; // Decimate by 2 (24 kHz)
        
        memset(history, 0, sizeof(history));
        write_idx = 0;
        sample_count = 0;
        // Fast ~10ms analysis update rate (100 Hz refresh rate)
        analysis_interval = (int)(sample_rate * 0.010);
        if (analysis_interval < 256) analysis_interval = 256;

        // Freq range: 65 Hz to 1450 Hz
        min_lag = (int)(dec_sample_rate / 1450.0);
        max_lag = (int)(dec_sample_rate / 65.0);
        if (max_lag > 390) max_lag = 390;

        dc_x1 = dc_y1 = 0.0f;
        detected_freq = 0.0f;
        smoothed_freq = 110.0f; // Default A2
        energy_boost = 1.0f;
        smoothed_boost = 1.0f;
    }

    inline void process_sample(float in) {
        history[write_idx] = in;
        write_idx = (write_idx + 1) & (HISTORY_SIZE - 1);

        sample_count++;
        if (sample_count >= analysis_interval) {
            sample_count = 0;
            analyze();
        }

        // Smooth energy boost with 30ms time constant for silky smooth gain modulation
        float boost_coeff = 1.0f - expf(-1.0f / ((float)sample_rate * 0.030f));
        smoothed_boost += boost_coeff * (energy_boost - smoothed_boost);
    }

    inline float get_energy_boost() const {
        return smoothed_boost;
    }

    inline float get_detected_freq() const {
        return smoothed_freq;
    }

private:
    void analyze() {
        const int raw_len = 1536;
        float raw[raw_len];
        int start_idx = (write_idx - raw_len + HISTORY_SIZE) & (HISTORY_SIZE - 1);
        for (int i = 0; i < raw_len; ++i) {
            raw[i] = history[(start_idx + i) & (HISTORY_SIZE - 1)];
        }

        // 1. DC Blocker, Peak Measurement & 2x Decimation
        int dec_len = raw_len / 2; // 768 samples
        float peak = 0.0f;
        const float R = 0.995f;

        for (int i = 0; i < dec_len; ++i) {
            float x1 = raw[2 * i];
            float y1 = x1 - dc_x1 + R * dc_y1;
            dc_x1 = x1; dc_y1 = y1;
            if (fabsf(y1) > peak) peak = fabsf(y1);

            float x2 = raw[2 * i + 1];
            float y2 = x2 - dc_x1 + R * dc_y1;
            dc_x1 = x2; dc_y1 = y2;
            if (fabsf(y2) > peak) peak = fabsf(y2);

            dec_buf[i] = 0.5f * (y1 + y2);
        }

        // Silence / quiet check: fade boost smoothly to 1.0
        if (peak < 0.0008f) {
            energy_boost = 1.0f;
            return;
        }

        // 2. Normalized Square Difference Function (NSDF)
        for (int lag = 0; lag < max_lag; ++lag) {
            float dot = 0.0f, e1 = 0.0f, e2 = 0.0f;
            for (int j = 0; j < CORR_WINDOW; j += 2) {
                float a0 = dec_buf[j];
                float b0 = dec_buf[j + lag];
                float a1 = dec_buf[j + 1];
                float b1 = dec_buf[j + 1 + lag];

                dot += a0 * b0 + a1 * b1;
                e1  += a0 * a0 + a1 * a1;
                e2  += b0 * b0 + b1 * b1;
            }
            nsdf[lag] = (2.0f * dot) / (e1 + e2 + 1e-12f);
        }

        // 3. Peak Detection after the first zero-crossing
        bool crossed_zero = false;
        float global_max = 0.0f;
        int best_lag = 0;

        for (int lag = 1; lag < max_lag - 1; ++lag) {
            if (!crossed_zero) {
                if (nsdf[lag] < 0.0f) {
                    crossed_zero = true;
                }
            } else {
                if (nsdf[lag] > nsdf[lag - 1] && nsdf[lag] >= nsdf[lag + 1] && nsdf[lag] > 0.20f) {
                    if (nsdf[lag] > global_max) {
                        global_max = nsdf[lag];
                    }
                }
            }
        }

        if (global_max < 0.25f) {
            energy_boost = 1.0f;
            return;
        }

        crossed_zero = false;
        for (int lag = 1; lag < max_lag - 1; ++lag) {
            if (!crossed_zero) {
                if (nsdf[lag] < 0.0f) crossed_zero = true;
            } else {
                if (nsdf[lag] > nsdf[lag - 1] && nsdf[lag] >= nsdf[lag + 1]) {
                    if (nsdf[lag] >= 0.70f * global_max && nsdf[lag] > 0.25f) {
                        best_lag = lag;
                        break;
                    }
                }
            }
        }

        if (best_lag == 0) {
            energy_boost = 1.0f;
            return;
        }

        // 4. Parabolic Interpolation for Sub-Sample Precision
        float y_prev = nsdf[best_lag - 1];
        float y_curr = nsdf[best_lag];
        float y_next = nsdf[best_lag + 1];
        float denom = y_prev - 2.0f * y_curr + y_next;
        float delta = (fabsf(denom) > 1e-9f) ? 0.5f * (y_prev - y_next) / denom : 0.0f;
        float refined_lag = (float)best_lag + delta;

        if (refined_lag > 1.0f) {
            detected_freq = (float)(dec_sample_rate / refined_lag);
        } else {
            detected_freq = 0.0f;
        }

        if (detected_freq >= 65.0f && detected_freq <= 1600.0f) {
            smoothed_freq = 0.70f * smoothed_freq + 0.30f * detected_freq;
        }

        // 5. Musical High-String Energy Compensation Curve
        // - Heavy wound strings (E2 ~ 82Hz, A2 ~ 110Hz) and full chords: 1.0x (0 dB boost)
        // - D3 ~ 147Hz, G3 ~ 196Hz: 1.4x - 1.9x (+3 to +6 dB)
        // - Plain steel B string (B3 ~ 247Hz): ~2.3x (+7.3 dB)
        // - High E string (E4 ~ 330Hz): ~2.8x (+8.9 dB)
        // - Upper solo frets (12th to 24th fret, 500-1320 Hz): ~3.5x - 3.8x (+10.8 to +11.6 dB)
        float freq_norm = smoothed_freq / 110.0f;
        if (freq_norm < 1.0f) {
            energy_boost = 1.0f;
        } else {
            float octaves_above_a2 = log2f(freq_norm);
            energy_boost = 1.0f + 1.15f * octaves_above_a2;
            if (energy_boost > 3.8f) energy_boost = 3.8f;
            if (energy_boost < 1.0f) energy_boost = 1.0f;
        }
    }
};

// -------------------------------------------------------------------------
// Seamless Infinite Feedback Hold Engine (Zero-Decay Looper at 100% Toe Down)
// Captures live mature guitar + speaker distress audio in a circular ring buffer
// and uses dual-head raised-cosine crossfading to hold the note forever with zero click
// and zero interpolation loss when the guitar string stops vibrating.
// -------------------------------------------------------------------------
class InfiniteFeedbackHold {
public:
    static const int RING_SIZE = 4096;
private:
    float ring_l[RING_SIZE];
    float ring_r[RING_SIZE];
    int write_idx;
    int loop_len;
    int lock_start_idx;
    float read_phase1, read_phase2;
    bool is_locked;
    float lock_crossfade;
    float note_peak;

public:
    void init() {
        reset();
    }

    void reset() {
        memset(ring_l, 0, sizeof(ring_l));
        memset(ring_r, 0, sizeof(ring_r));
        write_idx = 0; loop_len = 256; lock_start_idx = 0;
        read_phase1 = 0.0f; read_phase2 = 128.0f;
        is_locked = false; lock_crossfade = 0.0f; note_peak = 0.0f;
    }

    void set_pitch(float freq, double sample_rate) {
        if (freq >= 65.0f && freq <= 1500.0f && !is_locked) {
            int len = (int)std::round(sample_rate / freq);
            if (len < 64) len *= 4;
            else if (len < 128) len *= 2;
            if (len > RING_SIZE / 2) len = RING_SIZE / 2;
            loop_len = len;
        }
    }

    inline void process(float in_l, float in_r, float trigger, float guitar_env, bool is_new_pluck,
                        float& out_l, float& out_r) {
        bool is_toe_down = (trigger >= 0.96f);

        if (is_new_pluck || guitar_env > note_peak) {
            if (guitar_env > note_peak) note_peak = guitar_env;
            if (is_new_pluck && guitar_env > 0.02f) {
                is_locked = false;
                note_peak = guitar_env;
            }
        }

        if (!is_locked) {
            ring_l[write_idx] = in_l;
            ring_r[write_idx] = in_r;
            write_idx = (write_idx + 1) & (RING_SIZE - 1);
        }

        if (is_toe_down) {
            bool should_lock = (!is_locked && note_peak > 0.015f && guitar_env < note_peak * 0.60f && guitar_env > 0.0005f);
            if (should_lock) {
                is_locked = true;
                lock_start_idx = (write_idx - loop_len + RING_SIZE) & (RING_SIZE - 1);
                read_phase1 = 0.0f;
                read_phase2 = (float)loop_len * 0.5f;
            }

            if (is_locked) {
                lock_crossfade += 0.020f * (1.0f - lock_crossfade);
            } else {
                lock_crossfade += 0.030f * (0.0f - lock_crossfade);
            }
        } else {
            is_locked = false;
            note_peak = guitar_env;
            lock_crossfade += 0.030f * (0.0f - lock_crossfade);
        }

        if (lock_crossfade < 0.001f) {
            out_l = in_l;
            out_r = in_r;
            return;
        }

        read_phase1 += 1.0f;
        if (read_phase1 >= (float)loop_len) read_phase1 -= (float)loop_len;
        read_phase2 += 1.0f;
        if (read_phase2 >= (float)loop_len) read_phase2 -= (float)loop_len;

        float w1 = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * read_phase1 / (float)loop_len));
        float w2 = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * read_phase2 / (float)loop_len));

        int i1 = (lock_start_idx + (int)read_phase1) & (RING_SIZE - 1);
        int i2 = (lock_start_idx + (int)read_phase2) & (RING_SIZE - 1);

        float held_l = ring_l[i1] * w1 + ring_l[i2] * w2;
        float held_r = ring_r[i1] * w1 + ring_r[i2] * w2;

        out_l = in_l * (1.0f - lock_crossfade) + held_l * lock_crossfade;
        out_r = in_r * (1.0f - lock_crossfade) + held_r * lock_crossfade;
    }
};

// -------------------------------------------------------------------------
// Main CyberAcousticFeedbacker Plugin Class
// -------------------------------------------------------------------------
class CyberAcousticFeedbacker {
private:
    double sample_rate;

    // Pitch Shifters for Harmonics (L and R)
    GranularPitchShifter shifter_l;
    GranularPitchShifter shifter_r;

    // Supercharged Speaker Distress Engines (L and R)
    SuperchargedSpeakerDistress distress_l;
    SuperchargedSpeakerDistress distress_r;

    // Acoustic Cabinet Tail Diffusers (L and R)
    CabinetAcousticTail tail_diffuser_l;
    CabinetAcousticTail tail_diffuser_r;

    // Master Output Ceiling Limiter (-6.0 dBFS)
    OutputCeilingLimiter output_limiter;

    // Silent Guitar Pitch Detector & Energy Compensation Engine
    SilentGuitarPitchTracker pitch_tracker;

    // Seamless Infinite Feedback Hold Engine
    InfiniteFeedbackHold infinite_hold;
    float fast_env;

    // Guitar String Envelope Detector with Hysteresis Impetus
    float guitar_env;
    float env_atk_coeff;
    float env_rel_coeff;
    bool is_sustaining;
    float tail_env;

    // Expression & Harmonic Morph Slew
    float smoothed_trigger;
    float smoothed_ratio;
    float morph_progress;

    // Port Pointers
    const float* p_in_l;
    const float* p_in_r;
    float* p_out_l;
    float* p_out_r;
    const float* p_bypass;
    const float* p_trigger;
    const float* p_mode;
    const float* p_bloom;
    const float* p_gain;
    const float* p_harmonic;
    const float* p_warmth;
    const float* p_tail;
    const float* p_mix;

public:
    CyberAcousticFeedbacker(double sr) : sample_rate(sr) {
        shifter_l.init();
        shifter_r.init();

        distress_l.init(sample_rate);
        distress_r.init(sample_rate);

        tail_diffuser_l.init();
        tail_diffuser_r.init();

        output_limiter.init(sample_rate, -6.0f);
        pitch_tracker.init(sample_rate);
        infinite_hold.init();
        fast_env = 0.0f;

        env_atk_coeff = 1.0f - expf(-1.0f / ((float)sample_rate * 0.0030f)); // 3.0ms attack
        env_rel_coeff = 1.0f - expf(-1.0f / ((float)sample_rate * 0.2500f)); // 250ms impetus release
        is_sustaining = false;
        tail_env = 0.0f;

        smoothed_trigger = 0.0f;
        smoothed_ratio = 1.0f;
        morph_progress = 0.0f;
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_AUDIO_IN_L:  p_in_l = (const float*)data; break;
            case PORT_AUDIO_IN_R:  p_in_r = (const float*)data; break;
            case PORT_AUDIO_OUT_L: p_out_l = (float*)data; break;
            case PORT_AUDIO_OUT_R: p_out_r = (float*)data; break;
            case PORT_BYPASS:      p_bypass = (const float*)data; break;
            case PORT_TRIGGER:     p_trigger = (const float*)data; break;
            case PORT_MODE:        p_mode = (const float*)data; break;
            case PORT_BLOOM:       p_bloom = (const float*)data; break;
            case PORT_GAIN:        p_gain = (const float*)data; break;
            case PORT_HARMONIC:    p_harmonic = (const float*)data; break;
            case PORT_WARMTH:      p_warmth = (const float*)data; break;
            case PORT_TAIL:        p_tail = (const float*)data; break;
            case PORT_MIX:         p_mix = (const float*)data; break;
        }
    }

    void run(uint32_t sample_count) {
        bool bypass = (p_bypass && *p_bypass < 0.5f);
        if (bypass) {
            if (p_out_l != p_in_l) memcpy(p_out_l, p_in_l, sample_count * sizeof(float));
            if (p_out_r && p_in_r && p_out_r != p_in_r) memcpy(p_out_r, p_in_r, sample_count * sizeof(float));
            return;
        }

        // Control parameters
        float raw_trigger = (p_trigger ? *p_trigger : 0.0f);
        float target_trigger = std::max(0.0f, std::min(1.0f, raw_trigger));

        int mode = (int)std::round(p_mode ? *p_mode : 0.0f); // 0=Poly Sustainer, 1=Harmonic Bloom, 2=Raw Cranked
        float raw_bloom = (p_bloom ? *p_bloom : 1.2f);
        float bloom_sec = (raw_bloom > 10.0f) ? (0.2f + (raw_bloom / 100.0f) * 3.8f) : std::max(0.1f, std::min(5.0f, raw_bloom));

        float gain_knob = (p_gain ? *p_gain : 75.0f) * 0.01f;
        int harmonic_mode = (int)std::round(p_harmonic ? *p_harmonic : 1.0f);
        float warmth_knob = (p_warmth ? *p_warmth : 50.0f) * 0.01f;   // Speaker distress drive
        
        // Tail knob: 0% = 0.2s (tight cut), 50% = 1.8s (natural room decay), 100% = 4.0s (ambient sustain trail)
        float raw_tail = (p_tail ? *p_tail : 50.0f);
        float tail_sec = (raw_tail > 10.0f) ? (0.2f + (raw_tail / 100.0f) * 3.8f) : std::max(0.1f, std::min(5.0f, raw_tail));
        
        float mix_knob = (p_mix ? *p_mix : 50.0f) * 0.01f;

        // Target pitch ratio for harmonic overtone
        float target_ratio = 1.0f;
        switch (harmonic_mode) {
            case 0: target_ratio = 1.0f; break;        // Unison Fundamental
            case 1: target_ratio = 1.498307f; break;   // 5th (+7 semitones)
            case 2: target_ratio = 2.0f; break;        // Octave (+12 semitones)
            case 3: target_ratio = 2.996614f; break;   // Octave + 5th (+19 semitones)
            case 4: target_ratio = 4.0f; break;        // 2nd Octave (+24 semitones)
            default: target_ratio = 1.498307f; break;
        }

        // Expression Bloom slew
        float bloom_rate = 1.0f - expf(-1.0f / (bloom_sec * (float)sample_rate));
        float release_rate = 1.0f - expf(-1.0f / (0.08f * (float)sample_rate)); // Instant clean release on heel down
        float ratio_smooth_rate = 1.0f - expf(-1.0f / (0.02f * (float)sample_rate));

        // Fast, responsive foot expression tracking (25ms attack, 40ms release)
        float pedal_atk_rate = 1.0f - expf(-1.0f / (0.025f * (float)sample_rate));
        float pedal_rel_rate = 1.0f - expf(-1.0f / (0.040f * (float)sample_rate));

        // Tail envelope rates
        float tail_atk_rate = 1.0f - expf(-1.0f / (0.05f * (float)sample_rate));
        float tail_rel_rate = 1.0f - expf(-1.0f / (tail_sec * (float)sample_rate));

        // Supercharged Speaker Distress Drive
        float distress_drive = warmth_knob * 1.8f + 0.6f;
        float asym_amount = 0.15f + warmth_knob * 0.25f; // Stronger 2nd harmonic pull when cranked

        for (uint32_t i = 0; i < sample_count; ++i) {
            float in_l = p_in_l[i];
            float in_r = (p_in_r ? p_in_r[i] : in_l);
            float in_mono = 0.5f * (in_l + in_r);

            // 1. SILENT PITCH TRACKER & HIGH-STRING ENERGY COMPENSATION
            // Completely silent sidechain analysis - generates ZERO audio!
            // Automatically detects when player is on high B/E strings or upper frets
            // and supplies extra sustain and acoustic feedback energy.
            pitch_tracker.process_sample(in_mono);
            float energy_boost = pitch_tracker.get_energy_boost();
            float detected_hz = pitch_tracker.get_detected_freq();
            infinite_hold.set_pitch(detected_hz, sample_rate);

            // 2. ASYMMETRIC ENVELOPE DETECTOR ON GUITAR INPUT
            float in_rect = fabsf(in_mono);
            if (in_rect > guitar_env) {
                guitar_env += env_atk_coeff * (in_rect - guitar_env);
            } else {
                guitar_env += env_rel_coeff * (in_rect - guitar_env);
            }

            // Fast envelope for pluck detection
            bool is_new_pluck = false;
            if (in_rect > fast_env * 2.2f && in_rect > 0.025f) {
                is_new_pluck = true;
            }
            if (in_rect > fast_env) fast_env += 0.15f * (in_rect - fast_env);
            else fast_env += 0.002f * (in_rect - fast_env);

            // 3. EXPRESSION VCA & FAST FOOT TRACKING
            if (target_trigger > smoothed_trigger) {
                smoothed_trigger += (target_trigger - smoothed_trigger) * pedal_atk_rate;
            } else {
                smoothed_trigger += (target_trigger - smoothed_trigger) * pedal_rel_rate;
            }

            // If expression pedal is heel-down (<0.005), clean dry passthrough protected by ceiling limiter
            if (smoothed_trigger < 0.005f) {
                is_sustaining = false;
                tail_env = 0.0f;
                infinite_hold.reset();
                float lim_l, lim_r;
                output_limiter.process(in_l, in_r, lim_l, lim_r);
                p_out_l[i] = lim_l;
                if (p_out_r) p_out_r[i] = lim_r;
                continue;
            }

            // 4. CONTINUOUS ARTISTIC SUSTAIN & 100% TOE-DOWN INFINITE FEEDBACK LOCK
            // Up to 96% of pedal travel (CC 0 to 121, CV 0V to 9.6V):
            // - The pedal acts as a smooth, continuous, expressive sustain & bloom control.
            // - Pitch-aware adaptation prevents high thin strings from cutting out prematurely.
            // ONLY at 100% Toe Down (>= 0.96f / CC 122-127 / CV 9.6V-10V):
            // - Full Infinite Feedback Lock engages, sustaining indefinitely as long as you like!
            bool is_full_toe = (smoothed_trigger >= 0.96f);
            float onset_threshold = 0.0015f / sqrtf(energy_boost);
            float dropout_threshold = 0.00015f / (energy_boost * energy_boost);
            if (guitar_env > onset_threshold) {
                is_sustaining = true;
            } else if (is_full_toe && is_sustaining) {
                // 100% Toe Down: full infinite feedback lock!
                is_sustaining = true;
            } else if (guitar_env < dropout_threshold) {
                is_sustaining = false;
            }

            // 5. NATURAL TAIL ENVELOPE
            // Smooth acoustic room decay when pedal is released or strings are muted
            float target_tail_env = (is_sustaining && smoothed_trigger > 0.02f) ? 1.0f : 0.0f;
            if (target_tail_env > tail_env) {
                tail_env += (target_tail_env - tail_env) * tail_atk_rate;
            } else {
                tail_env += (target_tail_env - tail_env) * tail_rel_rate;
            }

            // 6. DYNAMIC STRING SUSTAINER (Continuous Foot-Controlled E-Bow Leveler with Pitch Boost)
            // Progressively scales from subtle singing sustain up to maximum feedback bloom.
            // Automatically injects higher gain ceiling into high strings to match low-string kinetic energy.
            float max_leveler_gain = (30.0f + smoothed_trigger * 220.0f * (0.8f + gain_knob * 0.4f)) * energy_boost;
            float floor_offset = (0.010f * (1.0f - smoothed_trigger * 0.90f)) / energy_boost;

            float sustain_gain = 1.0f;
            if (guitar_env > 0.00001f) {
                sustain_gain = (0.26f * energy_boost) / (guitar_env + floor_offset);
                if (sustain_gain > max_leveler_gain) sustain_gain = max_leveler_gain;
            }

            float sustained_l = in_l * sustain_gain * tail_env;
            float sustained_r = in_r * sustain_gain * tail_env;

            // 7. HARMONIC OVERTONE GENERATION
            // In Mode 1 (Harmonic Bloom), ratio smoothly morphs from unison up to harmonic
            float active_ratio = target_ratio;
            if (mode == 1 && harmonic_mode > 0) {
                morph_progress += (smoothed_trigger - morph_progress) * (bloom_rate * 0.7f);
                active_ratio = 1.0f + morph_progress * (target_ratio - 1.0f);
            } else if (mode == 0) {
                // Poly Sustainer: pure fundamental sustain
                active_ratio = 1.0f;
            }
            smoothed_ratio += (active_ratio - smoothed_ratio) * ratio_smooth_rate;

            float harm_l = shifter_l.process(sustained_l, smoothed_ratio);
            float harm_r = shifter_r.process(sustained_r, smoothed_ratio);

            // Blend sustained fundamental with harmonic overtone
            float harm_mix = (mode == 0) ? 0.0f : 0.50f;
            float pre_distress_l = sustained_l * (1.0f - harm_mix * 0.5f) + harm_l * harm_mix;
            float pre_distress_r = sustained_r * (1.0f - harm_mix * 0.5f) + harm_r * harm_mix;

            // 8. SUPERCHARGED SPEAKER DISTRESS EMULATION (With High-Note Excitation)
            float active_distress_drive = (warmth_knob * 1.8f + 0.6f) * (0.80f + 0.20f * energy_boost);
            float distressed_l = distress_l.process(pre_distress_l, active_distress_drive, asym_amount, sample_rate);
            float distressed_r = distress_r.process(pre_distress_r, active_distress_drive, asym_amount, sample_rate);

            // 9. ACOUSTIC CABINET TAIL DIFFUSION
            float tailed_l = tail_diffuser_l.process(distressed_l, tail_sec, sample_rate);
            float tailed_r = tail_diffuser_r.process(distressed_r, tail_sec, sample_rate);

            // Blend direct distressed feedback with acoustic cabinet tail
            float final_feedback_l = distressed_l * 0.75f + tailed_l * 0.45f;
            float final_feedback_r = distressed_r * 0.75f + tailed_r * 0.45f;

            // 9.5. INFINITE FEEDBACK HOLD AT 100% TOE DOWN
            // Seamlessly holds the captured guitar & cabinet feedback when strings die down
            float held_feedback_l, held_feedback_r;
            infinite_hold.process(final_feedback_l, final_feedback_r, smoothed_trigger, guitar_env, is_new_pluck,
                                  held_feedback_l, held_feedback_r);

            // 10. FINAL OUTPUT MIX: 100% PRISTINE CLEAN GUITAR + ROARING SPEAKER FEEDBACK & TAIL
            float string_coupling_boost = 1.0f + (energy_boost - 1.0f) * 0.45f;
            float wet_amount = smoothed_trigger * mix_knob * (1.0f + gain_knob * 0.6f) * tail_env * string_coupling_boost;
            float raw_out_l = in_l + held_feedback_l * wet_amount;
            float raw_out_r = in_r + held_feedback_r * wet_amount;

            // 11. MASTER OUTPUT CEILING LIMITER (-6.0 dBFS Peak Ceiling)
            // Guarantees output bus never blasts past 0 dBFS into master bus clipping
            float limited_l, limited_r;
            output_limiter.process(raw_out_l, raw_out_r, limited_l, limited_r);

            p_out_l[i] = limited_l;
            if (p_out_r) {
                p_out_r[i] = limited_r;
            }
        }
    }

    void reset() {
        shifter_l.init();
        shifter_r.init();
        distress_l.reset();
        distress_r.reset();
        tail_diffuser_l.init();
        tail_diffuser_r.init();
        output_limiter.init(sample_rate, -6.0f);
        pitch_tracker.init(sample_rate);
        infinite_hold.reset();
        guitar_env = 0.0f;
        fast_env = 0.0f;
        is_sustaining = false;
        tail_env = 0.0f;
        smoothed_trigger = 0.0f;
        smoothed_ratio = 1.0f;
        morph_progress = 0.0f;
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double rate,
                             const char* path,
                             const LV2_Feature* const* features) {
    return new CyberAcousticFeedbacker(rate);
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

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    delete (CyberAcousticFeedbacker*)instance;
}

static const void* extension_data(const char* uri) {
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

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #define LV2_EXPORT __declspec(dllexport)
#else
  #define LV2_EXPORT __attribute__((visibility("default")))
#endif

LV2_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : NULL;
}

#ifdef __cplusplus
}
#endif
