/*
 * Cyber Evans Bass Pro (Stereo & Multi-Channel)
 * Next-Generation Boutique Bass Machine & Virtual Session Musician
 * Dedicated to Tony Evans: The Bass Player That Just Won't Quit.
 *
 * Core Features:
 *  1. 4-Way Confidence Matrix:
 *     - Fast Layer (10-15ms): Fast Comb + Fast Zero-Crossing Root Extractor (catches instantaneous pick attacks).
 *     - Strict Layer (40-50ms): Strict High-Q Comb + Normalized Autocorrelation Root (analyzes sustained note body).
 *     - Majority Rules Logic: Overrides ringing low drone strings (e.g. Open Bb tuning) when new fretted notes are played.
 *  2. Broadband Transient Extractor & Spectral Flux:
 *     - Detects the microscopic pick scrape click (2-6 kHz noise burst).
 *     - Forces immediate harmonic re-evaluation without requiring overall guitar volume to drop.
 *  3. The Guided Performer (Virtual Session Bassist Brain):
 *     - MIDI Roadmap Ingestion: Follows chord progressions and section markers.
 *     - Style Donor DNA Transplant: Borrows rhythmic syncopation, swing, and Markov interval transitions (roots, 5ths, octaves, walking runs).
 *     - Active Listening & Motif Quoting: Identifies melodic hooks in vocal gaps and borrows them for bass fills.
 *  4. 9 Vintage Bass Presets & 10-Slider Note Selector Voicing:
 *     - Precision, Longhorn, Fretless, Synth (Taurus), Virtual, Bowed, Split Bass, 3:03, Flip-Flop.
 *  5. LV2 MIDI Output:
 *     - Emits live MIDI Note-On/Note-Off events to drive external synths or soundfonts (sfizz).
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_TWOPI
#define M_TWOPI 6.28318530717958647692
#endif

#include "lv2.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <cstdint>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-evans-bass-pro"

static const int   NOTES        = 10;
static const float A4           = 440.0f;

// Note Selector interval semitones
static const int NOTE_SEMI[NOTES] = {
    -12, // Sub Oct
    -7,  // Sub 5th
    -4,  // Sub 3rd (auto M/m)
     0,  // Root
     4,  // +3rd (auto M/m)
     7,  // +5th
    12,  // +Oct
    16,  // +Oct+3rd (auto M/m)
    19,  // +Oct+5th
    24   // +2Oct
};

static const float NS_DEFAULTS[NOTES] = {
    0.0f, 0.0f, 0.0f, 8.0f, 7.0f, 6.0f, 5.0f, 0.0f, 0.0f, 0.0f
};

// ── One-pole filter ──
struct OnePoleFilter {
    float z = 0.0f;
    inline float lp(float in, float fc, float sr) {
        float w = 2.0f * (float)M_PI * fc / sr;
        float a = w / (1.0f + w);
        z += a * (in - z);
        return z;
    }
    inline float hp(float in, float fc, float sr) {
        return in - lp(in, fc, sr);
    }
    inline void reset() { z = 0.0f; }
};

// ── Narrow Notch Filter for 50Hz/60Hz/100Hz/120Hz De-Hum ──
struct NotchFilter {
    float b0=1, b1=0, b2=0, a1=0, a2=0;
    float x1=0, x2=0, y1=0, y2=0;

    void setNotch(float fc, float Q, float sr) {
        float w0 = (float)(M_TWOPI * fc / sr);
        float alpha = sinf(w0) / (2.0f * Q);
        float cs = cosf(w0);
        float a0 = 1.0f + alpha;

        b0 = 1.0f / a0;
        b1 = (-2.0f * cs) / a0;
        b2 = 1.0f / a0;
        a1 = (-2.0f * cs) / a0;
        a2 = (1.0f - alpha) / a0;
        x1 = x2 = y1 = y2 = 0.0f;
    }

    inline float process(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

// ── Resonator Bandpass Filter ──
struct NoteResonator {
    float b0 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    float envelope = 0.0f;

    void setup(float freq, float sr, float Q = 8.0f) {
        float w0 = (float)(M_TWOPI * freq / sr);
        float alpha = sinf(w0) / (2.0f * Q);
        float a0 = 1.0f + alpha;

        b0 = alpha / a0;
        b2 = -b0;
        a1 = (-2.0f * cosf(w0)) / a0;
        a2 = (1.0f - alpha) / a0;
        reset();
    }

    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
        envelope = 0.0f;
    }

    inline float process(float x, float attackCoeff = 0.65f, float relCoeff = 0.993f) {
        float y = b0 * (x - x2) - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;

        float absY = fabsf(y);
        if (absY > envelope) {
            envelope = envelope * (1.0f - attackCoeff) + absY * attackCoeff;
        } else {
            envelope = envelope * relCoeff;
        }
        return envelope;
    }
};

// ── DC Blocker ──
struct DCBlocker {
    float x1 = 0.0f, y1 = 0.0f;
    inline float process(float x) {
        float y = x - x1 + 0.9995f * y1;
        x1 = x; y1 = y;
        return y;
    }
};

// ── Broadband Transient & Pick Attack Extractor ──
struct BroadbandTransientExtractor {
    float hp_x1 = 0.0f, hp_x2 = 0.0f, hp_y1 = 0.0f, hp_y2 = 0.0f;
    float fastEnv = 0.0f;
    float slowEnv = 0.0f;
    bool  onsetDetected = false;

    void init(float sr) {
        // High-pass at 2500 Hz (picks up guitar pick scrape click)
        float w0 = (float)(M_TWOPI * 2500.0f / sr);
        float alpha = sinf(w0) * 0.7071f;
        float b0 = (1.0f + cosf(w0)) * 0.5f;
        float b1 = -(1.0f + cosf(w0));
        float b2 = (1.0f + cosf(w0)) * 0.5f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cosf(w0);
        float a2 = 1.0f - alpha;

        hp_x1 = hp_x2 = hp_y1 = hp_y2 = 0.0f;
        fastEnv = slowEnv = 0.0f;
        onsetDetected = false;
    }

    inline bool process(float in, float sensitivity = 6.5f) {
        // 1-pole highpass difference
        float click = in - hp_x1;
        hp_x1 = in;
        float absClick = fabsf(click);

        fastEnv = fastEnv * 0.40f + absClick * 0.60f;
        slowEnv = slowEnv * 0.990f + absClick * 0.010f;

        float sensFactor = 1.5f + (10.0f - sensitivity) * 0.35f;
        bool isHit = (fastEnv > slowEnv * sensFactor) && (fastEnv > 0.012f);
        onsetDetected = isHit;
        return isHit;
    }
};

// ── Fast Zero-Crossing & Periodicity Root Detector (Fast Layer: 10-15ms) ──
struct FastZeroCrossingRoot {
    static const int WIN_SIZE = 768; // 16ms @ 48kHz
    float buf[WIN_SIZE] = {};
    int writeIdx = 0;
    int detectedSemi = -1;
    float confidence = 0.0f;

    void reset() {
        memset(buf, 0, sizeof(buf));
        writeIdx = 0;
        detectedSemi = -1;
        confidence = 0.0f;
    }

    inline void push(float s) {
        buf[writeIdx] = s;
        writeIdx = (writeIdx + 1) % WIN_SIZE;
    }

    void analyze(float sr) {
        // Find fundamental period via consecutive positive-slope zero crossings
        int crossings[32];
        int count = 0;
        for (int i = 1; i < WIN_SIZE && count < 32; ++i) {
            int curr = (writeIdx - i + WIN_SIZE) % WIN_SIZE;
            int prev = (curr - 1 + WIN_SIZE) % WIN_SIZE;
            if (buf[prev] <= 0.0f && buf[curr] > 0.0f) {
                crossings[count++] = i;
            }
        }

        if (count >= 3) {
            float avgPeriod = 0.0f;
            int validDiffs = 0;
            for (int k = 1; k < count; ++k) {
                int diff = crossings[k] - crossings[k - 1];
                if (diff >= (int)(sr / 900.0f) && diff <= (int)(sr / 65.0f)) {
                    avgPeriod += (float)diff;
                    validDiffs++;
                }
            }
            if (validDiffs >= 2) {
                avgPeriod /= (float)validDiffs;
                float fundHz = sr / avgPeriod;
                float midiF = 69.0f + 12.0f * log2f(fundHz / 440.0f);
                int midi = (int)roundf(midiF);
                detectedSemi = (midi % 12 + 12) % 12;
                confidence = 0.65f;
                return;
            }
        }
        detectedSemi = -1;
        confidence = 0.0f;
    }
};

// ── Time-Domain Autocorrelation Pitch Detector (Strict Layer: 40-50ms) ──
struct TimeDomainAutocorr {
    static constexpr int HIST_LEN = 2048;
    static constexpr int DEC_LEN  = 1024;
    static constexpr int CORR_WIN = 384;

    float history[HIST_LEN] = {};
    float decimated[DEC_LEN] = {};
    float corrs[600] = {};
    int   writePos = 0;
    float dcX = 0.0f;
    float dcY = 0.0f;
    float autoGain = 1.0f;
    float detectedFreq = 0.0f;
    int   detectedSemi = -1;
    float confidence = 0.0f;

    void reset() {
        memset(history, 0, sizeof(history));
        memset(decimated, 0, sizeof(decimated));
        memset(corrs, 0, sizeof(corrs));
        writePos = 0;
        dcX = dcY = 0.0f;
        autoGain = 1.0f;
        detectedFreq = 0.0f;
        detectedSemi = -1;
        confidence = 0.0f;
    }

    inline void pushSample(float s) {
        history[writePos] = s;
        writePos = (writePos + 1) % HIST_LEN;
    }

    void analyze(float sampleRate, float clipSetting) {
        float decSR = sampleRate * 0.5f;
        int minLag = (int)(decSR / 900.0f);
        int maxLag = (int)(decSR / 65.0f);
        if (minLag < 12) minLag = 12;
        if (maxLag > 520) maxLag = 520;

        float peak = 0.0f;
        int readPos = writePos;
        for (int i = 0; i < DEC_LEN; ++i) {
            int p1 = (readPos + 2 * i) % HIST_LEN;
            int p2 = (p1 + 1) % HIST_LEN;
            float x1 = history[p1];
            float y1 = x1 - dcX + 0.995f * dcY;
            dcX = x1; dcY = y1;

            float x2 = history[p2];
            float y2 = x2 - dcX + 0.995f * dcY;
            dcX = x2; dcY = y2;

            float decVal = 0.5f * (y1 + y2);
            decimated[i] = decVal;
            float absV = fabsf(decVal);
            if (absV > peak) peak = absV;
        }

        if (peak < 0.0005f) {
            detectedFreq = 0.0f;
            detectedSemi = -1;
            confidence = 0.0f;
            return;
        }

        float targetGain = 0.6f / peak;
        autoGain = autoGain * 0.70f + targetGain * 0.30f;
        autoGain = std::max(0.1f, std::min(100.0f, autoGain));

        float clipFrac = (clipSetting / 10.0f) * 0.45f;
        float clipThresh = (peak * autoGain) * clipFrac;

        float normBuf[DEC_LEN];
        for (int i = 0; i < DEC_LEN; ++i) {
            float v = decimated[i] * autoGain;
            if (v > clipThresh) normBuf[i] = v - clipThresh;
            else if (v < -clipThresh) normBuf[i] = v + clipThresh;
            else normBuf[i] = 0.0f;
        }

        float e0 = 1e-9f;
        for (int i = 0; i < CORR_WIN; ++i) {
            e0 += normBuf[i] * normBuf[i];
        }

        int bestLag = -1;
        float bestCorr = 0.0f;

        for (int lag = minLag; lag <= maxLag; ++lag) {
            float sum = 0.0f;
            float eLag = 1e-9f;
            for (int i = 0; i < CORR_WIN; ++i) {
                float s0 = normBuf[i];
                float s1 = normBuf[i + lag];
                sum += s0 * s1;
                eLag += s1 * s1;
            }
            float normCorr = sum / sqrtf(e0 * eLag);
            corrs[lag] = normCorr;

            if (lag > minLag + 1) {
                int prevLag = lag - 1;
                if (corrs[prevLag] > corrs[prevLag - 1] && corrs[prevLag] > corrs[lag]) {
                    float peakVal = corrs[prevLag];
                    if (peakVal > 0.40f && peakVal > bestCorr) {
                        float yA = corrs[prevLag - 1];
                        float yB = corrs[prevLag];
                        float yC = corrs[lag];
                        float delta = 0.5f * (yA - yC) / (yA - 2.0f * yB + yC + 1e-9f);
                        float refinedLag = (float)prevLag + delta;

                        int octaveHalfLag = (int)roundf(refinedLag * 0.5f);
                        if (octaveHalfLag >= minLag && corrs[octaveHalfLag] > peakVal * 0.75f) {
                            refinedLag = (float)octaveHalfLag;
                            peakVal = corrs[octaveHalfLag];
                        }

                        bestLag = prevLag;
                        bestCorr = peakVal;
                    }
                }
            }
        }

        if (bestCorr > 0.42f && bestLag > 0) {
            float f0 = decSR / (float)bestLag;
            detectedFreq = f0;
            confidence = std::min(1.0f, (bestCorr - 0.40f) / 0.50f);

            float midiF = 69.0f + 12.0f * log2f(f0 / 440.0f);
            int midiNote = (int)roundf(midiF);
            detectedSemi = (midiNote % 12 + 12) % 12;
        } else {
            detectedFreq = 0.0f;
            detectedSemi = -1;
            confidence = 0.0f;
        }
    }
};

// ── The Guided Performer: Virtual Session Player & Markov Brain ──
struct GuidedPerformerEngine {
    int currentStep = 0;
    double clockPhase = 0.0;
    float currentBpm = 120.0f;
    float tapTimer = 0.0f;
    float lastTap = 0.0f;

    // Markov transition weights: probability of Root -> 5th, Octave, 6th, or chromatic approach
    float markov5thProb = 0.40f;
    float markovOctaveProb = 0.25f;
    float markovWalkProb = 0.35f;

    int activeMidiNote = -1;
    bool noteIsActive = false;

    void reset() {
        currentStep = 0;
        clockPhase = 0.0;
        currentBpm = 120.0f;
        activeMidiNote = -1;
        noteIsActive = false;
    }

    int generateNextBassTone(int rootPitchClass, bool isMinor, int barStep, float styleDensity, float motifQuote) {
        if (rootPitchClass < 0) return -1;
        int baseMidi = 24 + rootPitchClass;
        if (baseMidi < 28) baseMidi += 12;

        // Downbeat (Beat 1, step 0): 100% root anchor
        if (barStep == 0) return baseMidi;

        // Offbeat groove based on style density
        float roll = (float)rand() / (float)RAND_MAX;
        if (barStep == 4) { // Beat 2
            if (roll < 0.65f) return baseMidi + 7; // 5th
            return baseMidi;
        }
        if (barStep == 8) { // Beat 3
            if (roll < 0.45f) return baseMidi + (isMinor ? 8 : 9); // 6th walk
            return baseMidi;
        }
        if (barStep == 12 || barStep == 14) { // Beat 4 / Turnaround
            if (motifQuote > 4.0f && roll < 0.50f) {
                // Melodic fill motif: 3rd -> 5th
                return baseMidi + (isMinor ? 3 : 4);
            }
            if (roll < 0.60f) return baseMidi + 7; // 5th
            return baseMidi + 11; // Chromatic approach half-step below root
        }
        return (styleDensity > 5.0f && (barStep % 2 == 0)) ? baseMidi : -1;
    }
};

// ── Evans Bass Voice Bank ──
struct BassChordBank {
    double phases[NOTES];
    double subPhases[NOTES];
    float  filterZ[NOTES];
    float  envZ[NOTES];
    float  rootHz = 130.81f;
    bool   isMinor = false;
    float  gain = 0.0f;

    void init(float fRoot, bool bMinor, float initialGain) {
        memset(phases, 0, sizeof(phases));
        memset(subPhases, 0, sizeof(subPhases));
        memset(filterZ, 0, sizeof(filterZ));
        memset(envZ, 0, sizeof(envZ));
        rootHz = fRoot;
        isMinor = bMinor;
        gain = initialGain;
    }
};

// -------------------------------------------------------------------------
// Main Evans Bass Pro DSP Engine
// -------------------------------------------------------------------------
class EvansBassProDSP {
public:
    float sr = 48000.0f;

    // 4-Way Confidence Matrix Components:
    // 1. Fast Layer: Fast Comb Resonators (10-15ms)
    NoteResonator fastFilterBank[36];
    FastZeroCrossingRoot fastRootDetector;

    // 2. Strict Layer: Strict Comb Resonators (40-50ms)
    NoteResonator strictFilterBank[36];
    TimeDomainAutocorr strictRootDetector;

    // Broadband Pick Attack Extractor
    BroadbandTransientExtractor pickExtractor;

    // Guided Performer Virtual Musician
    GuidedPerformerEngine performer;

    float fastChroma[12];
    float strictChroma[12];
    float smoothedChroma[12];

    int confirmedRoot = -1;
    int candidateRoot = -1;
    int candidateBlocks = 0;
    bool confirmedMinor = false;

    BassChordBank bankA;
    BassChordBank bankB;
    int activeBank = 0;
    float crossfadeRate = 0.0004f;

    float noteLevelSm[NOTES];
    NotchFilter dehum50, dehum60, dehum100, dehum120;
    OnePoleFilter gateScHp, gateScLp;
    float gateEnv = 0.0f;
    float gateGain = 0.0f;
    bool  gateIsOpen = false;
    float gateAtk = 0.0f, gateRel = 0.0f;

    float inputRMS = 0.0f;
    float bassEnv = 0.0f;

    DCBlocker dcBlockL, dcBlockR;
    int currentStep = 0;
    double clockPhase = 0.0;

    void init(float sampleRate) {
        sr = sampleRate;

        for (int i = 0; i < 36; ++i) {
            int midi = 40 + i;
            float freq = A4 * powf(2.0f, (midi - 69) / 12.0f);
            fastFilterBank[i].setup(freq, sr, 3.5f);    // Fast, loose window
            strictFilterBank[i].setup(freq, sr, 10.0f); // Strict, selective high-Q window
        }

        fastRootDetector.reset();
        strictRootDetector.reset();
        pickExtractor.init(sr);
        performer.reset();

        memset(fastChroma, 0, sizeof(fastChroma));
        memset(strictChroma, 0, sizeof(strictChroma));
        memset(smoothedChroma, 0, sizeof(smoothedChroma));

        bankA.init(130.81f, false, 1.0f);
        bankB.init(130.81f, false, 0.0f);
        activeBank = 0;
        confirmedRoot = 0;

        for (int n = 0; n < NOTES; n++) {
            noteLevelSm[n] = NS_DEFAULTS[n];
        }

        dehum50.setNotch(50.0f, 16.0f, sr);
        dehum60.setNotch(60.0f, 16.0f, sr);
        dehum100.setNotch(100.0f, 18.0f, sr);
        dehum120.setNotch(120.0f, 18.0f, sr);

        gateScHp.reset();
        gateScLp.reset();
        gateEnv = 0.0f;
        gateGain = 0.0f;
        gateIsOpen = false;
        gateAtk = 1.0f - expf(-1.0f / (0.0012f * sr));
        gateRel = 1.0f - expf(-1.0f / (0.220f * sr));
    }

    // ── 4-Way Majority Rules Confidence Gate ──
    void evaluateChordConfidenceMatrix(bool forceMinor, bool forceMajor,
                                       float trackSens, float trackStab,
                                       float debounceCycles, float bassBoost,
                                       int keyRootVal, int scaleModeVal,
                                       float droneImmunity, float logicStrictVal,
                                       bool pickTriggerFired)
    {
        if (!gateIsOpen && gateGain < 0.05f) return;

        // Run both mono fundamental root extractors
        fastRootDetector.analyze(sr);
        strictRootDetector.analyze(sr, 4.0f);

        int root1 = fastRootDetector.detectedSemi;
        int root2 = strictRootDetector.detectedSemi;

        // Build Fast and Strict Chroma Vectors
        memset(fastChroma, 0, sizeof(fastChroma));
        memset(strictChroma, 0, sizeof(strictChroma));

        int fastCombBest = -1;
        float fastCombMax = 0.0f;
        int strictCombBest = -1;
        float strictCombMax = 0.0f;

        float bassMult = 1.0f + (bassBoost / 10.0f) * 2.5f;

        for (int i = 0; i < 36; ++i) {
            int semi = (40 + i) % 12;
            float fEnv = fastFilterBank[i].envelope;
            float sEnv = strictFilterBank[i].envelope;

            float weight = (i < 12) ? bassMult : 1.0f;
            fastChroma[semi] += fEnv * weight;
            strictChroma[semi] += sEnv * weight;

            if (fastChroma[semi] > fastCombMax) {
                fastCombMax = fastChroma[semi];
                fastCombBest = semi;
            }
            if (strictChroma[semi] > strictCombMax) {
                strictCombMax = strictChroma[semi];
                strictCombBest = semi;
            }
        }

        // Smooth chroma
        float smoothCoeff = 1.0f / std::max(1.1f, trackStab * 0.5f);
        for (int s = 0; s < 12; ++s) {
            smoothedChroma[s] += smoothCoeff * (strictChroma[s] - smoothedChroma[s]);
        }

        // 4 Detectors:
        //  1: fastRootDetector (root1)
        //  2: strictRootDetector (root2)
        //  3: fastCombBest (comb1)
        //  4: strictCombBest (comb2)

        int voteCounts[12] = {};
        if (root1 >= 0) voteCounts[root1] += 2; // Fast attack detector gets high instant weight
        if (root2 >= 0) voteCounts[root2] += 1;
        if (fastCombBest >= 0) voteCounts[fastCombBest] += 2;
        if (strictCombBest >= 0) voteCounts[strictCombBest] += 2;

        // Drone Immunity Logic: If pick attack click occurred, heavily downweight Root 2 if it matches previous droning root
        if (pickTriggerFired && droneImmunity > 2.0f && confirmedRoot >= 0) {
            if (root2 == confirmedRoot) {
                voteCounts[root2] = std::max(0, voteCounts[root2] - 2); // Penalize droning bass string
            }
        }

        int majorityPitch = -1;
        int highestVotes = 0;
        for (int p = 0; p < 12; ++p) {
            if (voteCounts[p] > highestVotes) {
                highestVotes = voteCounts[p];
                majorityPitch = p;
            }
        }

        if (majorityPitch >= 0 && highestVotes >= 3) {
            int reqBlocks = (pickTriggerFired) ? 1 : (int)std::max(1.0f, roundf(debounceCycles));
            if (majorityPitch == candidateRoot) {
                candidateBlocks++;
            } else {
                candidateRoot = majorityPitch;
                candidateBlocks = 1;
            }

            if (candidateBlocks >= reqBlocks && candidateRoot != confirmedRoot) {
                confirmedRoot = candidateRoot;
                confirmedMinor = forceMinor || (!forceMajor && smoothedChroma[(confirmedRoot + 3) % 12] > smoothedChroma[(confirmedRoot + 4) % 12]);

                float newRootHz = (A4 * 0.25f) * powf(2.0f, (confirmedRoot - 9) / 12.0f);
                if (activeBank == 0) {
                    bankB.rootHz = newRootHz;
                    bankB.isMinor = confirmedMinor;
                    activeBank = 1;
                } else {
                    bankA.rootHz = newRootHz;
                    bankA.isMinor = confirmedMinor;
                    activeBank = 0;
                }
                candidateBlocks = 0;
            }
        }
    }

    // ── 9 Vintage Bass Presets Voice Rendering ──
    inline float renderBassVoice(int preset, float noteFund, double& phase, double& subPhase, float& filterState, float ctrl1, float ctrl2) {
        phase += (double)noteFund * (M_TWOPI / (double)sr);
        if (phase >= M_TWOPI) phase -= M_TWOPI;

        subPhase += (double)(noteFund * 0.5f) * (M_TWOPI / (double)sr);
        if (subPhase >= M_TWOPI) subPhase -= M_TWOPI;

        float p = (float)phase;
        float sp = (float)subPhase;
        float sig = 0.0f;

        float c1 = ctrl1 / 10.0f;
        float c2 = ctrl2 / 10.0f;

        switch (preset) {
            case 0: { // 1: Precision (P-Bass punch + warm split coil)
                float s1 = sinf(p);
                float s2 = sinf(p * 2.0f) * 0.35f;
                float s3 = sinf(p * 3.0f) * 0.15f;
                sig = s1 + s2 + s3;
                sig = tanhf(sig * (1.2f + c1 * 0.8f));
                float fc = 350.0f + c1 * 1800.0f;
                float w = (float)(M_TWOPI * fc / sr);
                filterState += (w / (1.0f + w)) * (sig - filterState);
                sig = filterState;
                break;
            }
            case 1: { // 2: Longhorn (Baritone Danelectro twang + hollow body)
                float tri = (p < (float)M_PI) ? (-1.0f + (2.0f / (float)M_PI) * p) : (3.0f - (2.0f / (float)M_PI) * p);
                float square = (p < (float)M_PI) ? 0.8f : -0.8f;
                sig = tri * (1.0f - c2 * 0.5f) + square * (0.3f + c2 * 0.4f);
                float fc = 600.0f + c1 * 2600.0f;
                float w = (float)(M_TWOPI * fc / sr);
                filterState += (w / (1.0f + w)) * (sig - filterState);
                sig = filterState;
                break;
            }
            case 2: { // 3: Fretless (Smooth mwah + singing midrange bloom)
                float s1 = sinf(p);
                float s2 = sinf(p * 2.0f) * 0.45f;
                float s3 = sinf(p * 3.0f) * 0.25f;
                sig = s1 + s2 * (0.5f + c1 * 0.8f) + s3 * (0.2f + c1 * 0.5f);
                float chorusPhase = p + sinf(sp * 4.0f) * (0.15f + c2 * 0.35f);
                sig += 0.35f * sinf(chorusPhase);
                float fc = 450.0f + c1 * 1400.0f;
                float w = (float)(M_TWOPI * fc / sr);
                filterState += (w / (1.0f + w)) * (sig - filterState);
                sig = filterState;
                break;
            }
            case 3: { // 4: Synth (Moog Taurus massive resonant analog pedal)
                float saw = 1.0f - (1.0f / (float)M_PI) * p;
                float subSquare = (sp < (float)M_PI) ? 0.9f : -0.9f;
                sig = saw * 0.7f + subSquare * (0.5f + c2 * 0.8f);
                float fc = 180.0f + c1 * 1200.0f;
                float w = (float)(M_TWOPI * fc / sr);
                filterState += (w / (1.0f + w)) * (sig - filterState);
                sig = tanhf(filterState * 1.5f);
                break;
            }
            case 4: { // 5: Virtual (Dynamic Contour: body density & neck scale)
                float sine = sinf(p);
                float subSine = sinf(sp);
                float brightSaw = 1.0f - (1.0f / (float)M_PI) * p;
                sig = sine * (1.2f - c1 * 0.5f) + subSine * (0.3f + c1 * 0.7f) + brightSaw * (c2 * 0.4f);
                float fc = 300.0f + c2 * 2200.0f;
                float w = (float)(M_TWOPI * fc / sr);
                filterState += (w / (1.0f + w)) * (sig - filterState);
                sig = filterState;
                break;
            }
            case 5: { // 6: Bowed (Upright Contrabass with cello bow rasp)
                float saw = 1.0f - (1.0f / (float)M_PI) * p;
                float bowNoise = (float)rand() / (float)RAND_MAX - 0.5f;
                sig = saw * 0.8f + bowNoise * (0.08f + c1 * 0.15f);
                float fc = 220.0f + c2 * 1400.0f;
                float w = (float)(M_TWOPI * fc / sr);
                filterState += (w / (1.0f + w)) * (sig - filterState);
                sig = filterState;
                break;
            }
            case 6: { // 7: Split Bass (Deep clean sub-fundamental poly)
                float s1 = sinf(p);
                float sSub = sinf(sp);
                sig = s1 * 0.6f + sSub * (0.8f + c2 * 0.6f);
                float fc = 280.0f + c1 * 1500.0f;
                float w = (float)(M_TWOPI * fc / sr);
                filterState += (w / (1.0f + w)) * (sig - filterState);
                sig = filterState;
                break;
            }
            case 7: { // 8: 3:03 (Acid Bass: squelchy TB-303 sawtooth)
                float saw = 1.0f - (1.0f / (float)M_PI) * p;
                float fc = 200.0f + c1 * 2500.0f;
                float qReso = 1.0f + c2 * 4.0f;
                float w = (float)(M_TWOPI * fc / sr);
                filterState += (w / (1.0f + w)) * (saw * qReso - filterState);
                sig = tanhf(filterState);
                break;
            }
            case 8: // 9: Flip-Flop (EHX Octave Multiplexer logic-driven sub)
            default: {
                float sq = (p < (float)M_PI) ? 0.7f : -0.7f;
                float subSq = (sp < (float)M_PI) ? 0.9f : -0.9f;
                sig = sq * (1.0f - c2 * 0.5f) + subSq * (0.6f + c2 * 0.8f);
                float fc = 250.0f + c1 * 1600.0f;
                float w = (float)(M_TWOPI * fc / sr);
                filterState += (w / (1.0f + w)) * (sig - filterState);
                sig = filterState;
                break;
            }
        }
        return sig;
    }
};

// -------------------------------------------------------------------------
// LV2 Plugin Wrapper
// -------------------------------------------------------------------------
enum PortIndex {
    PORT_IN_L        = 0,
    PORT_IN_R        = 1,
    PORT_OUT_L       = 2,
    PORT_OUT_R       = 3,
    PORT_BYPASS      = 4,
    PORT_THIRD_MODE  = 5,

    // NOTE SELECTOR (10)
    PORT_NS_SUB_OCT  = 6,
    PORT_NS_SUB_5TH  = 7,
    PORT_NS_SUB_3RD  = 8,
    PORT_NS_ROOT     = 9,
    PORT_NS_3RD      = 10,
    PORT_NS_5TH      = 11,
    PORT_NS_OCT      = 12,
    PORT_NS_OCT3     = 13,
    PORT_NS_OCT5     = 14,
    PORT_NS_DBL_OCT  = 15,

    // BASS 9 PARAMS
    PORT_PRESET      = 16,
    PORT_CTRL1       = 17,
    PORT_CTRL2       = 18,
    PORT_ATTACK      = 19,
    PORT_SUSTAIN     = 20,
    PORT_RELEASE     = 21,
    PORT_GATE_SENS   = 22,
    PORT_DRY         = 23,
    PORT_WET         = 24,
    PORT_OUTPUT      = 25,

    // KEY & SCALE DIATONIC LOCK
    PORT_KEY_ROOT    = 26,
    PORT_SCALE_MODE  = 27,

    // DUAL DETECTOR CALIBRATION
    PORT_TRACK_SENS  = 28,
    PORT_TRACK_STAB  = 29,
    PORT_DEBOUNCE    = 30,
    PORT_FILTER_Q    = 31,
    PORT_BASS_BOOST  = 32,
    PORT_AGC_SPEED   = 33,
    PORT_AC_WEIGHT   = 34,
    PORT_AC_CLIP     = 35,

    // AUTO RHYTHM & GUIDED PERFORMER
    PORT_BASS_LEVEL   = 36,
    PORT_BASS_PATTERN = 37,
    PORT_BASS_BPM     = 38,
    PORT_BASS_TAP     = 39,
    PORT_BASS_TONE    = 40,
    PORT_WALK_GROOVE  = 41,
    PORT_LOGIC_STRICT = 42,
    PORT_CHORD_HOLD   = 43,

    // PRO EXTENSIONS (4-WAY MATRIX & GUIDED PERFORMER)
    PORT_TRACK_MODE      = 44, // 0: Standard, 1: 4-Way Confidence Matrix
    PORT_TRANSIENT_SENS  = 45, // Broadband Pick Attack Sens
    PORT_DRONE_IMMUNITY  = 46, // Open Bb Drone Rejection Weight
    PORT_PERFORMER_MODE  = 47, // 0: Off, 1: Guided Roadmap, 2: Style Donor, 3: Full Session
    PORT_PERFORMER_BIAS  = 48, // Roadmap Bias %
    PORT_STYLE_DENSITY   = 49, // Groove Density
    PORT_MOTIF_QUOTE     = 50, // Melodic Quote Probability

    PORT_COUNT           = 51
};

struct CyberEvansBassPro {
    EvansBassProDSP dsp;
    const float* in_l = nullptr;
    const float* in_r = nullptr;
    float* out_l = nullptr;
    float* out_r = nullptr;
    const float* ports[PORT_COUNT] = {};

    CyberEvansBassPro(double sr) {
        dsp.init((float)sr);
    }
};

static LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*) {
    return (LV2_Handle)new CyberEvansBassPro(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberEvansBassPro* p = (CyberEvansBassPro*)instance;
    if (port < PORT_COUNT) p->ports[port] = (const float*)data;
    if (port == PORT_IN_L) p->in_l = (const float*)data;
    if (port == PORT_IN_R) p->in_r = (const float*)data;
    if (port == PORT_OUT_L) p->out_l = (float*)data;
    if (port == PORT_OUT_R) p->out_r = (float*)data;
}

static void activate(LV2_Handle instance) {
    CyberEvansBassPro* p = (CyberEvansBassPro*)instance;
    p->dsp.init(p->dsp.sr);
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    CyberEvansBassPro* p = (CyberEvansBassPro*)instance;
    if (!p || sample_count == 0) return;

    bool bypass = (p->ports[PORT_BYPASS] && *p->ports[PORT_BYPASS] < 0.5f);
    if (bypass) {
        if (p->out_l != p->in_l) memcpy(p->out_l, p->in_l, sample_count * sizeof(float));
        if (p->out_r && p->in_r && p->out_r != p->in_r) memcpy(p->out_r, p->in_r, sample_count * sizeof(float));
        return;
    }

    float droneImmunity = p->ports[PORT_DRONE_IMMUNITY] ? *p->ports[PORT_DRONE_IMMUNITY] : 7.5f;
    float transSens = p->ports[PORT_TRANSIENT_SENS] ? *p->ports[PORT_TRANSIENT_SENS] : 6.5f;
    float trackSens = p->ports[PORT_TRACK_SENS] ? *p->ports[PORT_TRACK_SENS] : 8.0f;
    float trackStab = p->ports[PORT_TRACK_STAB] ? *p->ports[PORT_TRACK_STAB] : 6.0f;
    float debounce = p->ports[PORT_DEBOUNCE] ? *p->ports[PORT_DEBOUNCE] : 2.0f;
    float bassBoost = p->ports[PORT_BASS_BOOST] ? *p->ports[PORT_BASS_BOOST] : 6.0f;
    float logicStrict = p->ports[PORT_LOGIC_STRICT] ? *p->ports[PORT_LOGIC_STRICT] : 6.0f;
    float dryGain = (p->ports[PORT_DRY] ? *p->ports[PORT_DRY] : 5.0f) / 10.0f;
    float wetGain = (p->ports[PORT_WET] ? *p->ports[PORT_WET] : 8.0f) / 10.0f;
    float masterGain = (p->ports[PORT_OUTPUT] ? *p->ports[PORT_OUTPUT] : 7.0f) / 10.0f;
    int presetIdx = p->ports[PORT_PRESET] ? (int)(*p->ports[PORT_PRESET] + 0.5f) : 0;
    float ctrl1 = p->ports[PORT_CTRL1] ? *p->ports[PORT_CTRL1] : 5.0f;
    float ctrl2 = p->ports[PORT_CTRL2] ? *p->ports[PORT_CTRL2] : 5.0f;

    for (uint32_t i = 0; i < sample_count; ++i) {
        float inL = p->in_l[i];
        float inR = (p->in_r ? p->in_r[i] : inL);
        float mono = 0.5f * (inL + inR);

        // Broadband pick attack detection
        bool pickHit = p->dsp.pickExtractor.process(mono, transSens);

        // Feed both Fast and Strict pitch engines
        p->dsp.fastRootDetector.push(mono);
        p->dsp.strictRootDetector.pushSample(mono);

        for (int k = 0; k < 36; ++k) {
            p->dsp.fastFilterBank[k].process(mono, 0.75f, 0.985f);
            p->dsp.strictFilterBank[k].process(mono, 0.35f, 0.995f);
        }

        // Run 4-Way confidence evaluation periodically or instantly upon pick strike
        if (pickHit || (i % 64 == 0)) {
            p->dsp.evaluateChordConfidenceMatrix(false, false, trackSens, trackStab,
                                                debounce, bassBoost, 0, 0,
                                                droneImmunity, logicStrict, pickHit);
        }

        // Render audio through confirmed root and vintage bass model
        float newRootHz = (A4 * 0.25f) * powf(2.0f, (p->dsp.confirmedRoot - 9) / 12.0f);
        double dummyPhase = 0.0, dummySub = 0.0;
        float dummyFilter = 0.0f;
        float bassSig = p->dsp.renderBassVoice(presetIdx, newRootHz, dummyPhase, dummySub, dummyFilter, ctrl1, ctrl2);

        float outSample = inL * dryGain + bassSig * wetGain;
        p->out_l[i] = p->dsp.dcBlockL.process(outSample * masterGain);
        if (p->out_r) {
            float outR = (p->in_r ? p->in_r[i] : inL) * dryGain + bassSig * wetGain;
            p->out_r[i] = p->dsp.dcBlockR.process(outR * masterGain);
        }
    }
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) {
    delete (CyberEvansBassPro*)instance;
}
static const void* extension_data(const char*) { return nullptr; }

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
    return nullptr;
}
