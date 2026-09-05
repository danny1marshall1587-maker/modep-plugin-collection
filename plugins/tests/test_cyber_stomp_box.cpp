#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include "../cyber-stomp-box.lv2/src/CyberStompBoxEngine.hpp"

int main() {
    std::cout << "Testing CyberStompBoxEngine..." << std::endl;

    AudioDSP::CyberStompBoxEngine engine;
    double sampleRate = 48000.0;
    engine.init(sampleRate);

    const int blockSize = 128;
    std::vector<float> inL(blockSize, 0.0f);
    std::vector<float> inR(blockSize, 0.0f);
    std::vector<float> outL(blockSize, 0.0f);
    std::vector<float> outR(blockSize, 0.0f);

    // Test each sound model
    for (int model = 0; model < 6; ++model) {
        engine.reset();
        engine.setModel(model);
        engine.setPitchSemitones(0.0f);
        engine.setDecayTime(0.25f);
        engine.setPunch(0.8f);
        engine.setSubThumpDb(3.0f);
        engine.setTone(0.5f);
        engine.setFixedVelocity(true);
        engine.setGuitarGainDb(0.0f);
        engine.setStompGainDb(0.0f);
        engine.setSplitOutput(false);

        // Process silence before trigger
        engine.process(inL.data(), inR.data(), outL.data(), outR.data(), blockSize);
        for (int i = 0; i < blockSize; ++i) {
            assert(std::abs(outL[i]) < 1e-5f);
        }

        // Trigger kick
        engine.trigger(1.0f);

        float peakL = 0.0f;
        float peakR = 0.0f;
        bool hasSound = false;

        // Process 1 second of audio
        for (int b = 0; b < (48000 / blockSize); ++b) {
            engine.process(inL.data(), inR.data(), outL.data(), outR.data(), blockSize);
            for (int i = 0; i < blockSize; ++i) {
                assert(!std::isnan(outL[i]));
                assert(!std::isinf(outL[i]));
                assert(!std::isnan(outR[i]));
                assert(!std::isinf(outR[i]));

                if (std::abs(outL[i]) > 0.05f) hasSound = true;
                if (std::abs(outL[i]) > peakL) peakL = std::abs(outL[i]);
                if (std::abs(outR[i]) > peakR) peakR = std::abs(outR[i]);
            }
        }

        assert(hasSound);
        assert(peakL > 0.2f && peakL <= 1.0f);
        std::cout << "  [+] Model " << model << " Peak: " << peakL << " - OK" << std::endl;
    }

    // Test Split Output mode (Guitar to L, Stomp to R)
    {
        engine.reset();
        engine.setModel(AudioDSP::CyberStompBoxEngine::MODEL_SUB_STOMP);
        engine.setSplitOutput(true);

        // Feed guitar test signal into inL
        for (int i = 0; i < blockSize; ++i) inL[i] = 0.5f;

        engine.trigger(1.0f);
        engine.process(inL.data(), inR.data(), outL.data(), outR.data(), blockSize);

        // In split mode: outL should be guitar only (0.5f), outR should have kick peak > 0.05
        assert(std::abs(outL[0] - 0.5f) < 1e-4f);
        float peakR = 0.0f;
        for (int i = 0; i < blockSize; ++i) {
            if (std::abs(outR[i]) > peakR) peakR = std::abs(outR[i]);
        }
        assert(peakR > 0.05f);
        std::cout << "  [+] Split Output Routing (Guitar L, PA Kick R) - OK" << std::endl;
    }

    std::cout << "\nAll CyberStompBoxEngine tests PASSED successfully!" << std::endl;
    return 0;
}
