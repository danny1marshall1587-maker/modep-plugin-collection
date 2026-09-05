/*
 * Cyber Feedback Room (Mono)
 * Pure electro-acoustic feedback resonator & room matrix pedal.
 * Clean, production-ready version with only Feedback + Reverb controls exposed.
 * All internal sustainer / speaker / pick / LFO calibration hardcoded to perfection.
 */

#include <lv2/core/lv2.h>
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef PLUGIN_URI
#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-feedback-room-mono"
#endif

enum PortIndex {
    PORT_AUDIO_IN_L        = 0,
    PORT_AUDIO_IN_R        = 1,
    PORT_AUDIO_OUT_L       = 2,
    PORT_AUDIO_OUT_R       = 3,
    PORT_BYPASS            = 4,
    PORT_TRIGGER           = 5,  // Feedback / Expression Pedal (0.0 to 1.0)
    PORT_ROOM_IN_LOOP      = 6,  // Toggle: 0.0 = Room on Output, 1.0 = Room in Feedback Loop
    PORT_ROOM_SIZE         = 7,  // Room Size / Reflection Delay (5ms to 300ms, default 298.8ms)
    PORT_ROOM_DECAY        = 8,  // Room Reflection Decay (0.1s to 3.0s, default 3.0s)
    PORT_ROOM_DAMPING      = 9,  // Room Wall Damping (1000Hz to 16000Hz, default 16000Hz)
    PORT_ROOM_FEED         = 10, // Room Recirculation into Loop (0% to 100%, default 100%)
    PORT_ROOM_MIX          = 11  // Room Level in Output (0% to 100%, default 28.3%)
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
        spkAtk = 1.0f - expf(-1.0f / (0.0006f * (float)sampleRate));
        spkRel = 1.0f - expf(-1.0f / (0.0450f * (float)sampleRate));
        spkThermalRel = 1.0f - expf(-1.0f / (0.3500f * (float)sampleRate));
    }

    void reset() {
        speakerEnv = 0.0f;
        speakerThermalEnv = 0.0f;
        speakerConeHistory = 0.0f;
    }

    inline float process(float in, float distress_drive, float asym_amount, double sampleRate) {
        float absIn = fabsf(in);
        if (absIn > speakerEnv) {
            speakerEnv += (absIn - speakerEnv) * spkAtk;
        } else {
            speakerEnv += (absIn - speakerEnv) * spkRel;
        }

        if (speakerEnv > speakerThermalEnv) {
            speakerThermalEnv += (speakerEnv - speakerThermalEnv) * 0.005f;
        } else {
            speakerThermalEnv += (speakerEnv - speakerThermalEnv) * spkThermalRel;
        }

        float sag = 1.0f / (1.0f + speakerThermalEnv * 1.6f);
        float driven = in * distress_drive * sag;

        // Dynamic cone compliance excursion
        float cone_offset = asym_amount * (speakerEnv * 0.45f);
        driven += cone_offset;

        // Dynamic asymmetric soft saturation curve
        float out_dist;
        if (driven > 1.25f) {
            out_dist = 1.0f;
        } else if (driven < -1.25f) {
            out_dist = -1.0f;
        } else {
            out_dist = driven - (driven * driven * driven) * 0.2133f;
        }

        // Bass Cone Inertia Filtering
        float cone_inertia_cutoff = 140.0f + (1.0f - std::min(1.0f, speakerEnv)) * 260.0f;
        float cone_w0 = 2.0f * (float)M_PI * cone_inertia_cutoff / (float)sampleRate;
        speakerConeHistory += cone_w0 * (out_dist - speakerConeHistory);

        return out_dist * 0.70f + speakerConeHistory * 0.30f;
    }
};

// -------------------------------------------------------------------------
// Cabinet Acoustic Tail Diffuser (4 Allpass Filter Stages)
// -------------------------------------------------------------------------
class CabinetAcousticTail {
private:
    static const int AP_BUF_SIZE_1 = 191;
    static const int AP_BUF_SIZE_2 = 367;
    static const int AP_BUF_SIZE_3 = 541;
    static const int AP_BUF_SIZE_4 = 701;

    float ap_buf_1[AP_BUF_SIZE_1];
    float ap_buf_2[AP_BUF_SIZE_2];
    float ap_buf_3[AP_BUF_SIZE_3];
    float ap_buf_4[AP_BUF_SIZE_4];

    int ap_idx_1, ap_idx_2, ap_idx_3, ap_idx_4;
    float lpf_state;

public:
    void init() {
        memset(ap_buf_1, 0, sizeof(ap_buf_1));
        memset(ap_buf_2, 0, sizeof(ap_buf_2));
        memset(ap_buf_3, 0, sizeof(ap_buf_3));
        memset(ap_buf_4, 0, sizeof(ap_buf_4));
        ap_idx_1 = ap_idx_2 = ap_idx_3 = ap_idx_4 = 0;
        lpf_state = 0.0f;
    }

    void reset() {
        init();
    }

    inline float allpass(float in, float* buf, int& idx, int size, float g) {
        float buf_out = buf[idx];
        float out = -g * in + buf_out;
        buf[idx] = in + g * buf_out;
        if (++idx >= size) idx = 0;
        return out;
    }

    inline float process(float in, float decay_time, double sampleRate) {
        float g = 0.45f + std::min(1.0f, std::max(0.0f, decay_time / 5.0f)) * 0.22f;
        float s = in;
        s = allpass(s, ap_buf_1, ap_idx_1, AP_BUF_SIZE_1, g);
        s = allpass(s, ap_buf_2, ap_idx_2, AP_BUF_SIZE_2, g);
        s = allpass(s, ap_buf_3, ap_idx_3, AP_BUF_SIZE_3, g);
        s = allpass(s, ap_buf_4, ap_idx_4, AP_BUF_SIZE_4, g);

        // Warm cabinet wooden dampening
        float lpf_alpha = 1.0f - expf(-2.0f * (float)M_PI * 4500.0f / (float)sampleRate);
        lpf_state += lpf_alpha * (s - lpf_state);
        return lpf_state;
    }
};

// -------------------------------------------------------------------------
// High-Density Acoustic Room Simulator
// -------------------------------------------------------------------------
class HighDensityRoomSimulator {
private:
    static const int ROOM_MAX_DELAY = 192000;
    float dly_l[ROOM_MAX_DELAY];
    float dly_r[ROOM_MAX_DELAY];
    int write_idx;

    float damp_state_l;
    float damp_state_r;

    // Diffuser allpass buffers
    float ap_l1[641], ap_l2[1153];
    float ap_r1[757], ap_r2[1301];
    int ap_l1_idx, ap_l2_idx, ap_r1_idx, ap_r2_idx;

public:
    void init() {
        memset(dly_l, 0, sizeof(dly_l));
        memset(dly_r, 0, sizeof(dly_r));
        write_idx = 0;
        damp_state_l = 0.0f;
        damp_state_r = 0.0f;

        memset(ap_l1, 0, sizeof(ap_l1));
        memset(ap_l2, 0, sizeof(ap_l2));
        memset(ap_r1, 0, sizeof(ap_r1));
        memset(ap_r2, 0, sizeof(ap_r2));
        ap_l1_idx = ap_l2_idx = ap_r1_idx = ap_r2_idx = 0;
    }

    void reset() {
        init();
    }

    inline float allpass(float in, float* buf, int& idx, int size, float g) {
        float buf_out = buf[idx];
        float out = -g * in + buf_out;
        buf[idx] = in + g * buf_out;
        if (++idx >= size) idx = 0;
        return out;
    }

    inline void process(float in_l, float in_r,
                        float size_sec, float decay_sec, float damping_hz,
                        double sampleRate,
                        float& out_l, float& out_r) {
        float damp_alpha = 1.0f - expf(-2.0f * (float)M_PI * damping_hz / (float)sampleRate);

        int delay_samples_l = (int)(size_sec * sampleRate);
        int delay_samples_r = (int)(size_sec * 1.293f * sampleRate);

        delay_samples_l = std::max(64, std::min(ROOM_MAX_DELAY - 100, delay_samples_l));
        delay_samples_r = std::max(64, std::min(ROOM_MAX_DELAY - 100, delay_samples_r));

        float feedback_gain = expf(-3.0f * size_sec / decay_sec);
        feedback_gain = std::min(0.88f, feedback_gain);

        int read_idx_l = write_idx - delay_samples_l;
        if (read_idx_l < 0) read_idx_l += ROOM_MAX_DELAY;
        int read_idx_r = write_idx - delay_samples_r;
        if (read_idx_r < 0) read_idx_r += ROOM_MAX_DELAY;

        float wet_l = dly_l[read_idx_l];
        float wet_r = dly_r[read_idx_r];

        damp_state_l += damp_alpha * (wet_l - damp_state_l);
        damp_state_r += damp_alpha * (wet_r - damp_state_r);

        float fb_l = damp_state_l * feedback_gain;
        float fb_r = damp_state_r * feedback_gain;

        // Stereophonic room cross-mixing
        dly_l[write_idx] = in_l + fb_l * 0.70f + fb_r * 0.30f;
        dly_r[write_idx] = in_r + fb_r * 0.70f + fb_l * 0.30f;

        if (++write_idx >= ROOM_MAX_DELAY) write_idx = 0;

        float diff_l = allpass(damp_state_l, ap_l1, ap_l1_idx, 641, 0.45f);
        diff_l = allpass(diff_l, ap_l2, ap_l2_idx, 1153, 0.35f);

        float diff_r = allpass(damp_state_r, ap_r1, ap_r1_idx, 757, 0.45f);
        diff_r = allpass(diff_r, ap_r2, ap_r2_idx, 1301, 0.35f);

        out_l = diff_l;
        out_r = diff_r;
    }
};

// -------------------------------------------------------------------------
// Brickwall Output Ceiling Limiter
// -------------------------------------------------------------------------
class OutputCeilingLimiter {
private:
    float peak_env;

public:
    void init() {
        peak_env = 0.0f;
    }

    void reset() {
        peak_env = 0.0f;
    }

    inline void process(float in_l, float in_r, float& out_l, float& out_r) {
        float peak = std::max(fabsf(in_l), fabsf(in_r));
        if (peak > peak_env) {
            peak_env = peak;
        } else {
            peak_env += (peak - peak_env) * 0.002f;
        }

        float gain = 1.0f;
        if (peak_env > 0.98f) {
            gain = 0.98f / peak_env;
        }

        out_l = in_l * gain;
        out_r = in_r * gain;
    }
};

// -------------------------------------------------------------------------
// Physical String Vibration Sustainer (Zero-Pitch-Shift Endless Sustain)
// -------------------------------------------------------------------------
class PhysicalStringFeedbackSimulator {
private:
    static const int MAX_BUFFER_SAMPLES = 192000;
    float buf_l[MAX_BUFFER_SAMPLES];
    float buf_r[MAX_BUFFER_SAMPLES];
    int write_idx;

    // Dual Hermite Read Heads for Seamless Micro-Looping
    float head_a_phase;
    float head_b_phase;
    float head_spacing;

    // Internal Sustainer State
    bool is_locked;
    float lock_attenuation;
    float crossfade_progress;
    int samples_since_pluck;
    bool is_attack_lockout_active;

    // String Damping Filter States
    float string_damp_l;
    float string_damp_r;

    // Anti-Thump Subsonic 2-Pole Butterworth Highpass Filter (65 Hz)
    float hp_x1_l, hp_x2_l, hp_y1_l, hp_y2_l;
    float hp_x1_r, hp_x2_r, hp_y1_r, hp_y2_r;
    float hp_b0, hp_b1, hp_b2, hp_a1, hp_a2;

    // Micro-Phase Drift LFO
    float drift_phase;

    // Acoustic Return History (Loop Injection)
    float acoustic_inj_l;
    float acoustic_inj_r;

public:
    void init() {
        memset(buf_l, 0, sizeof(buf_l));
        memset(buf_r, 0, sizeof(buf_r));
        write_idx = 0;
        head_a_phase = 0.0f;
        head_b_phase = 0.0f;
        head_spacing = 0.5f;

        is_locked = false;
        lock_attenuation = 1.0f;
        crossfade_progress = 0.0f;
        samples_since_pluck = 999999;
        is_attack_lockout_active = false;

        string_damp_l = 0.0f;
        string_damp_r = 0.0f;

        hp_x1_l = hp_x2_l = hp_y1_l = hp_y2_l = 0.0f;
        hp_x1_r = hp_x2_r = hp_y1_r = hp_y2_r = 0.0f;

        hp_b0 = 1.0f; hp_b1 = -2.0f; hp_b2 = 1.0f;
        hp_a1 = 0.0f; hp_a2 = 0.0f;

        drift_phase = 0.0f;
        acoustic_inj_l = 0.0f;
        acoustic_inj_r = 0.0f;
    }

    void reset() {
        init();
    }

    void setup_subsonic_hpf(float cutoff_hz, double sampleRate) {
        float w0 = 2.0f * (float)M_PI * cutoff_hz / (float)sampleRate;
        float cos_w0 = cosf(w0);
        float sin_w0 = sinf(w0);
        float alpha = sin_w0 / (2.0f * 0.7071f);

        float a0 = 1.0f + alpha;
        hp_b0 = ((1.0f + cos_w0) / 2.0f) / a0;
        hp_b1 = (-(1.0f + cos_w0)) / a0;
        hp_b2 = ((1.0f + cos_w0) / 2.0f) / a0;
        hp_a1 = (-2.0f * cos_w0) / a0;
        hp_a2 = (1.0f - alpha) / a0;
    }

    inline void apply_subsonic_hpf(float in_l, float in_r, float& out_l, float& out_r) {
        float y_l = hp_b0 * in_l + hp_b1 * hp_x1_l + hp_b2 * hp_x2_l - hp_a1 * hp_y1_l - hp_a2 * hp_y2_l;
        hp_x2_l = hp_x1_l; hp_x1_l = in_l;
        hp_y2_l = hp_y1_l; hp_y1_l = y_l;
        out_l = y_l;

        float y_r = hp_b0 * in_r + hp_b1 * hp_x1_r + hp_b2 * hp_x2_r - hp_a1 * hp_y1_r - hp_a2 * hp_y2_r;
        hp_x2_r = hp_x1_r; hp_x1_r = in_r;
        hp_y2_r = hp_y1_r; hp_y1_r = y_r;
        out_r = y_r;
    }

    inline void inject_acoustic_return(float ret_l, float ret_r, float loop_gain) {
        acoustic_inj_l = ret_l * loop_gain;
        acoustic_inj_r = ret_r * loop_gain;
    }

    inline float hermite_interp(float x0, float x1, float x2, float x3, float frac) {
        float c0 = x1;
        float c1 = 0.5f * (x2 - x0);
        float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    inline float read_interpolated(const float* buf, float read_pos, int max_size) {
        int i1 = (int)floorf(read_pos);
        float frac = read_pos - (float)i1;
        int i0 = (i1 - 1 + max_size) % max_size;
        int i2 = (i1 + 1) % max_size;
        int i3 = (i1 + 2) % max_size;
        i1 = (i1 + max_size) % max_size;
        return hermite_interp(buf[i0], buf[i1], buf[i2], buf[i3], frac);
    }

    inline void process(float in_l, float in_r,
                        float trigger, float guitar_env, bool is_new_pluck,
                        bool hold_on_pluck,
                        float takeover_threshold, float transition_sec, float loop_sec,
                        float damping_hz, float vol_match_ratio,
                        float attack_lockout_sec, float loop_attack_sec,
                        float loop_release_sec, float loop_drift,
                        float pickup_sat_amt, float air_wobble_rate, float air_wobble_depth,
                        double sampleRate,
                        float& out_l, float& out_r) {

        int loop_samples = (int)(loop_sec * sampleRate);
        loop_samples = std::max(64, std::min(MAX_BUFFER_SAMPLES - 100, loop_samples));

        // 1. Attack Lockout Logic
        if (is_new_pluck) {
            samples_since_pluck = 0;
            is_attack_lockout_active = true;
            if (!hold_on_pluck) {
                is_locked = false;
                crossfade_progress = 0.0f;
            }
        } else {
            samples_since_pluck++;
        }

        if (samples_since_pluck > (int)(attack_lockout_sec * sampleRate)) {
            is_attack_lockout_active = false;
        }

        // 2. Continuous Buffer Writing (Live guitar incoming)
        if (!is_locked) {
            float inj_filtered_l, inj_filtered_r;
            apply_subsonic_hpf(in_l + acoustic_inj_l, in_r + acoustic_inj_r, inj_filtered_l, inj_filtered_r);

            buf_l[write_idx] = inj_filtered_l;
            buf_r[write_idx] = inj_filtered_r;
            if (++write_idx >= MAX_BUFFER_SAMPLES) write_idx = 0;
        }

        // 3. Takeover Trigger Detection
        bool should_sustain = (trigger >= takeover_threshold) && !is_attack_lockout_active;
        if (should_sustain && !is_locked) {
            is_locked = true;
            head_a_phase = 0.0f;
            head_b_phase = 0.5f;
            crossfade_progress = 0.0f;
        } else if (!should_sustain && !hold_on_pluck && is_locked) {
            is_locked = false;
        }

        // 4. Smooth Crossfade Transition
        float xfade_step = 1.0f / (transition_sec * (float)sampleRate);
        if (is_locked) {
            crossfade_progress = std::min(1.0f, crossfade_progress + xfade_step);
        } else {
            crossfade_progress = std::max(0.0f, crossfade_progress - xfade_step);
        }

        if (crossfade_progress <= 0.0001f) {
            out_l = in_l;
            out_r = in_r;
            return;
        }

        // 5. Dual Hermite Read Heads with Phase Drift Diffusion
        drift_phase += (float)(2.0 * M_PI * air_wobble_rate / sampleRate);
        if (drift_phase >= 2.0f * (float)M_PI) drift_phase -= 2.0f * (float)M_PI;

        float drift_offset = sinf(drift_phase) * air_wobble_depth * 0.003f * (float)sampleRate;
        float base_speed = 1.0f / (float)loop_samples;

        head_a_phase += base_speed;
        if (head_a_phase >= 1.0f) head_a_phase -= 1.0f;

        head_b_phase += base_speed;
        if (head_b_phase >= 1.0f) head_b_phase -= 1.0f;

        // Window Weighting (Hann Seam Blending)
        float win_a = 0.5f * (1.0f - cosf(head_a_phase * 2.0f * (float)M_PI));
        float win_b = 0.5f * (1.0f - cosf(head_b_phase * 2.0f * (float)M_PI));
        float win_sum = win_a + win_b;
        if (win_sum > 0.0001f) {
            win_a /= win_sum;
            win_b /= win_sum;
        }

        float pos_a = (float)write_idx - (head_a_phase * (float)loop_samples) + drift_offset;
        float pos_b = (float)write_idx - (head_b_phase * (float)loop_samples) - drift_offset;

        while (pos_a < 0.0f) pos_a += (float)MAX_BUFFER_SAMPLES;
        while (pos_b < 0.0f) pos_b += (float)MAX_BUFFER_SAMPLES;

        float sample_a_l = read_interpolated(buf_l, pos_a, MAX_BUFFER_SAMPLES);
        float sample_a_r = read_interpolated(buf_r, pos_a, MAX_BUFFER_SAMPLES);
        float sample_b_l = read_interpolated(buf_l, pos_b, MAX_BUFFER_SAMPLES);
        float sample_b_r = read_interpolated(buf_r, pos_b, MAX_BUFFER_SAMPLES);

        float raw_sustained_l = (sample_a_l * win_a + sample_b_l * win_b) * vol_match_ratio;
        float raw_sustained_r = (sample_a_r * win_a + sample_b_r * win_b) * vol_match_ratio;

        // 6. Magnetic Pickup Core Saturation
        if (pickup_sat_amt > 0.001f) {
            float sat_drive = 1.0f + pickup_sat_amt * 1.5f;
            raw_sustained_l = tanhf(raw_sustained_l * sat_drive);
            raw_sustained_r = tanhf(raw_sustained_r * sat_drive);
        }

        // 7. Anti-Thump Subsonic Butterworth HPF
        float clean_sustained_l, clean_sustained_r;
        apply_subsonic_hpf(raw_sustained_l, raw_sustained_r, clean_sustained_l, clean_sustained_r);

        // 8. String High-Frequency Damping
        float damp_alpha = 1.0f - expf(-2.0f * (float)M_PI * damping_hz / (float)sampleRate);
        string_damp_l += damp_alpha * (clean_sustained_l - string_damp_l);
        string_damp_r += damp_alpha * (clean_sustained_r - string_damp_r);

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
// Main CyberFeedbackRoom Plugin Class (Mono Input / True Stereo Processing)
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
        output_limiter.init();
        string_sim.init();
        string_sim.setup_subsonic_hpf(65.0f, sample_rate);

        reset();
    }

    void reset() {
        distress_l.reset();
        distress_r.reset();
        tail_diffuser_l.reset();
        tail_diffuser_r.reset();
        room_sim.reset();
        output_limiter.reset();
        string_sim.reset();

        trig_lfo_phase = 0.0f;
        guitar_env = 0.0f;
        fast_env = 0.0f;
        env_atk_coeff = 1.0f - expf(-1.0f / ((float)sample_rate * 0.0025f));
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
        float room_size_sec = (p_room_size ? *p_room_size : 298.8f) * 0.001f;
        float room_decay_sec = std::max(0.1f, std::min(3.0f, (p_room_decay ? *p_room_decay : 3.0f)));
        float room_damping_hz = std::max(1000.0f, std::min(16000.0f, (p_room_damping ? *p_room_damping : 16000.0f)));
        float room_mix_amt = (p_room_mix ? *p_room_mix : 28.3f) * 0.01f;
        float room_feed_amt = (p_room_feed ? *p_room_feed : 100.0f) * 0.01f;

        // Hardcoded Tuned Sustainer & Dev Parameters (Permanent Perfection)
        const float gain_knob = 1.0f;              // 100%
        const float distress_knob = 1.0f;          // 100%
        const float tail_sec = 3.91f;              // 3.91 s
        const float mix_knob = 0.238f;             // 23.8%
        const float takeover_threshold = 0.951f;   // 95.1%
        const float transition_sec = 5.204f;       // 5204 ms
        const float loop_sec = 0.331f;             // 331 ms
        const float damping_hz = 13579.0f;         // 13579 Hz
        const float vol_match_ratio = 1.0f;        // 100%
        const float bloom_sec = 3.95f;             // 3.95 s
        const float attack_lockout_sec = 0.669f;   // 669 ms
        const float loop_attack_sec = 0.298f;      // 298 ms
        const float loop_release_sec = 0.199f;     // 199 ms
        const float loop_drift = 0.984f;           // 98.4%
        const float spk_recirc_amt = 0.996f;       // 99.6%
        const float pickup_sat_amt = 1.0f;         // 100%
        const float air_wobble_rate = 2.50f;       // 2.50 Hz
        const float air_wobble_depth = 0.498f;     // 49.8%
        const float trig_lfo_rate = 1.88f;         // 1.88 Hz
        const float trig_lfo_depth = 0.0589f;      // 5.89%
        const bool hold_on_pluck = true;           // Hold Drone active

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
    // No specific deactivation
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

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : NULL;
}
