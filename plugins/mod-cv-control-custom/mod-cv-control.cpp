#include "lv2.h"
#include <cmath>
#include <cstring>

#define PLUGIN_URI "http://moddevices.com/plugins/mod-devel/mod-cv-control"

enum PortIndex {
    PORT_CV_OUT     = 0,
    PORT_CONTROL    = 1,
    PORT_SMOOTHING  = 2,
    PORT_CV_OUT_REV = 3,
};

class ModCvControl {
private:
    const float* p_control;
    const float* p_smoothing;
    float* p_cv_out;
    float* p_cv_out_rev;
    
    double sample_rate;
    float current_val;

public:
    ModCvControl(double sr) : sample_rate(sr) {
        p_control = nullptr;
        p_smoothing = nullptr;
        p_cv_out = nullptr;
        p_cv_out_rev = nullptr;
        current_val = 0.0f;
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_CV_OUT:     p_cv_out = (float*)data; break;
            case PORT_CONTROL:    p_control = (const float*)data; break;
            case PORT_SMOOTHING:  p_smoothing = (const float*)data; break;
            case PORT_CV_OUT_REV: p_cv_out_rev = (float*)data; break;
        }
    }

    void activate() {
        if (p_control) current_val = *p_control;
    }

    void run(uint32_t sample_count) {
        float target = p_control ? *p_control : 0.0f;
        bool smooth = p_smoothing && (*p_smoothing > 0.5f);
        
        // Ensure bounds 0-10 based on TTL min/max
        if (target < 0.0f) target = 0.0f;
        if (target > 10.0f) target = 10.0f;

        // If not smoothing, snap immediately
        if (!smooth) {
            current_val = target;
        }

        // Calculate reverse
        float rev_target = 10.0f - target;
        float current_rev = 10.0f - current_val;

        for (uint32_t i = 0; i < sample_count; ++i) {
            if (smooth) {
                // simple 1-pole lowpass for smoothing
                current_val += (target - current_val) * 0.005f;
                current_rev = 10.0f - current_val;
            }
            if (p_cv_out) p_cv_out[i] = current_val;
            if (p_cv_out_rev) p_cv_out_rev[i] = current_rev;
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                              double rate,
                              const char* bundle_path,
                              const LV2_Feature* const* features) {
    return new ModCvControl(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    static_cast<ModCvControl*>(instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    static_cast<ModCvControl*>(instance)->activate();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    static_cast<ModCvControl*>(instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    delete static_cast<ModCvControl*>(instance);
}

static const void* extension_data(const char* uri) {
    return nullptr;
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
    return index == 0 ? &descriptor : nullptr;
}
