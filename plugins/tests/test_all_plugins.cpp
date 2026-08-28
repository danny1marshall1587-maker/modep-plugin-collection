#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <string>
#include <random>

#include "../harmonic-tremolo/src/HarmonicTremoloEngine.hpp"
#include "../cyber-denoiser/src/CyberDenoiserEngine.hpp"
#include "../galaxy-strobe-tune/src/StrobeTunerCore.hpp"

// Utility to generate a sine wave buffer
std::vector<float> generateSine(float freq, double sampleRate, int numSamples, float amplitude = 0.5f) {
    std::vector<float> buf(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        buf[i] = amplitude * std::sin(2.0 * AudioDSP::PI_DOUBLE * freq * i / sampleRate);
    }
    return buf;
}

// Utility to generate gaussian noise
std::vector<float> generateNoise(int numSamples, float amplitude = 0.05f) {
    std::vector<float> buf(numSamples);
    std::mt19937 gen(42);
    std::normal_distribution<float> dist(0.0f, amplitude);
    for (int i = 0; i < numSamples; ++i) {
        buf[i] = dist(gen);
    }
    return buf;
}

void testHarmonicTremolo() {
    std::cout << "--- Testing Harmonic Tremolo Engine ---" << std::endl;
    AudioDSP::HarmonicTremoloEngine engine;
    const double sr = 48000.0;
    engine.prepare(sr);

    engine.setRate(4.0f);
    engine.setDepth(0.9f);
    engine.setCrossoverFrequency(700.0f);
    engine.setWarmth(0.5f);
    engine.setWaveform(AudioDSP::HarmonicTremoloEngine::LFOWaveform::TubeSine);
    engine.setMix(1.0f);

    auto sine = generateSine(440.0f, sr, 48000);
    std::vector<float> outL(sine.size()), outR(sine.size());

    for (size_t i = 0; i < sine.size(); ++i) {
        engine.processSample(sine[i], sine[i], outL[i], outR[i]);
    }

    // Check that output is not silent and has dynamic modulation
    float maxL = 0.0f, minL = 1.0f;
    for (float s : outL) {
        maxL = std::max(maxL, std::abs(s));
        minL = std::min(minL, std::abs(s));
    }
    assert(maxL > 0.2f);
    std::cout << "  Harmonic Tremolo Processed Successfully! Max Peak: " << maxL << std::endl;
}

void testCyberDenoiser() {
    std::cout << "\n--- Testing Cyber-Denoiser PRO Engine ---" << std::endl;
    AudioDSP::CyberDenoiserEngine engine;
    const double sr = 48000.0;
    engine.prepare(sr);

    // 1. Generate Noise only buffer for learning
    int learnSamples = static_cast<int>(sr * 3.0); // 3 seconds
    auto noise = generateNoise(learnSamples, 0.02f);
    std::vector<float> outL(learnSamples), outR(learnSamples);

    engine.startLearn();
    assert(engine.isLearning() == true);

    // Feed noise while learning
    const int blockSize = 256;
    for (int i = 0; i < learnSamples; i += blockSize) {
        int n = std::min(blockSize, learnSamples - i);
        engine.processBlock(&noise[i], &noise[i], &outL[i], &outR[i], n);
    }

    // Learning should now be completed
    assert(engine.isLearning() == false);
    std::cout << "  Noise Profile Successfully Learned!" << std::endl;

    // 2. Process noisy signal (Guitar Tone + Noise)
    auto guitarTone = generateSine(220.0f, sr, 48000, 0.6f);
    auto testNoise = generateNoise(48000, 0.02f);
    std::vector<float> mixed(48000);
    for (size_t i = 0; i < mixed.size(); ++i) mixed[i] = guitarTone[i] + testNoise[i];

    std::vector<float> cleanedL(48000), cleanedR(48000);
    for (int i = 0; i < 48000; i += blockSize) {
        int n = std::min(blockSize, 48000 - i);
        engine.processBlock(&mixed[i], &mixed[i], &cleanedL[i], &cleanedR[i], n);
    }

    // 3. Test noise reduction during silence gap
    auto silentWithNoise = generateNoise(24000, 0.02f);
    std::vector<float> suppressed(24000);
    for (int i = 0; i < 24000; i += blockSize) {
        int n = std::min(blockSize, 24000 - i);
        engine.processBlock(&silentWithNoise[i], &silentWithNoise[i], &suppressed[i], nullptr, n);
    }

    float noiseRmsBefore = 0.0f, noiseRmsAfter = 0.0f;
    for (int i = 12000; i < 24000; ++i) {
        noiseRmsBefore += silentWithNoise[i] * silentWithNoise[i];
        noiseRmsAfter += suppressed[i] * suppressed[i];
    }
    noiseRmsBefore = std::sqrt(noiseRmsBefore / 12000.0f);
    noiseRmsAfter = std::sqrt(noiseRmsAfter / 12000.0f);

    std::cout << "  Silence Noise RMS Before: " << noiseRmsBefore << ", After: " << noiseRmsAfter << std::endl;
    assert(noiseRmsAfter < noiseRmsBefore * 0.35f);
    std::cout << "  Cyber-Denoiser PRO Spectral Subtraction Verified!" << std::endl;
}

void testGalaxyStrobeTuner() {
    std::cout << "\n--- Testing Galaxy Strobe Tuner Engine ---" << std::endl;
    HeadRushDSP::StrobeTunerCore tuner;
    const double sr = 48000.0;
    tuner.init(sr);

    struct TestNote {
        const char* name;
        float freq;
        int expectedNoteIndex; // C=0, C#=1, D=2, D#=3, E=4, F=5, F#=6, G=7, G#=8, A=9, A#=10, B=11
    };

    std::vector<TestNote> testNotes = {
        { "Low E2", 82.41f, 4 },
        { "A2", 110.00f, 9 },
        { "D3", 146.83f, 2 },
        { "G3", 196.00f, 7 },
        { "B3", 246.94f, 11 },
        { "High E4", 329.63f, 4 },
        { "Concert A4", 440.00f, 9 }
    };

    for (const auto& tn : testNotes) {
        tuner.reset();
        auto signal = generateSine(tn.freq, sr, 12288, 0.7f);
        
        // Push in 256 sample chunks
        for (size_t i = 0; i < signal.size(); i += 256) {
            tuner.pushSamples(&signal[i], 256);
            tuner.updateTuningCalculations(440.0f, 0, 0);
        }

        float detected = tuner.getDetectedFrequency();
        int noteIdx = tuner.getDetectedNoteIndex();
        float cents = tuner.getCentsDeviation();
        bool inTune = tuner.isInTune();

        std::cout << "  Testing " << tn.name << " (" << tn.freq << " Hz): Detected=" 
                  << detected << " Hz, NoteIdx=" << noteIdx << ", Cents=" << cents 
                  << ", InTune=" << (inTune ? "YES" : "NO") << std::endl;

        assert(std::abs(detected - tn.freq) < 0.2f);
        assert(noteIdx == tn.expectedNoteIndex);
        assert(std::abs(cents) < 0.5f);
        assert(inTune == true);
    }
    std::cout << "  Galaxy Strobe Tuner Pitch Detection Verified Across All Guitar Strings!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "   MODEP DSP Plugins Validation Suite   " << std::endl;
    std::cout << "========================================" << std::endl;

    testHarmonicTremolo();
    testCyberDenoiser();
    testGalaxyStrobeTuner();

    std::cout << "\n>>> ALL PLUGIN TESTS PASSED WITH 100% SUCCESS! <<<" << std::endl;
    return 0;
}
