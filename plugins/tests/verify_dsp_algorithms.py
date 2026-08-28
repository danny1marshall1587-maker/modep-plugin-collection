import math
import numpy as np

def detect_pitch_normalized(signal, sr=48000.0):
    thresh = 0.25 * np.max(np.abs(signal))
    clipped = np.where(signal > thresh, signal - thresh, np.where(signal < -thresh, signal + thresh, 0.0))
    
    min_lag = int(sr / 1200.0) # 40
    max_lag = int(sr / 40.0)   # 1200
    corr_win = 1024
    
    corrs = np.zeros(max_lag)
    e0 = np.sum(clipped[:corr_win]**2)
    if e0 <= 1e-9:
        return 0.0
        
    for lag in range(min_lag, max_lag):
        if lag + corr_win > len(clipped):
            break
        e_lag = np.sum(clipped[lag:lag + corr_win]**2)
        dot = np.dot(clipped[:corr_win], clipped[lag:lag + corr_win])
        corrs[lag] = dot / math.sqrt(e0 * e_lag + 1e-12)
        
    global_max = np.max(corrs[min_lag:max_lag])
    if global_max < 0.4:
        return 0.0
        
    # Pick first peak reaching at least 85% of max
    best_lag = -1
    for lag in range(min_lag + 1, max_lag - 1):
        if corrs[lag] > corrs[lag - 1] and corrs[lag] >= corrs[lag + 1]:
            if corrs[lag] >= 0.85 * global_max:
                best_lag = lag
                break
                
    if best_lag == -1:
        best_lag = min_lag + np.argmax(corrs[min_lag:max_lag])
        
    # Parabolic interpolation
    alpha = corrs[best_lag - 1]
    beta = corrs[best_lag]
    gamma = corrs[best_lag + 1]
    denom = (alpha - 2.0 * beta + gamma)
    delta = 0.5 * (alpha - gamma) / denom if abs(denom) > 1e-9 else 0.0
    refined_lag = best_lag + delta
    
    return sr / refined_lag

def test():
    sr = 48000.0
    note_names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    test_freqs = [
        ("Low E2", 82.41, 4),
        ("A2", 110.0, 9),
        ("D3", 146.83, 2),
        ("G3", 196.0, 7),
        ("B3", 246.94, 11),
        ("High E4", 329.63, 4),
        ("Concert A4", 440.0, 9)
    ]
    for name, f, exp_idx in test_freqs:
        t = np.linspace(0, 0.25, int(sr * 0.25), endpoint=False)
        sig = 0.7 * np.sin(2 * math.pi * f * t)
        detected_f = detect_pitch_normalized(sig, sr)
        
        midi = 12.0 * math.log2(detected_f / 440.0) + 69.0
        rounded = round(midi)
        note_idx = (rounded % 12 + 12) % 12
        cents = (midi - rounded) * 100.0

        print(f"String {name} ({f:.2f} Hz) -> Detected: {detected_f:.3f} Hz, Note: {note_names[note_idx]}, Cents: {cents:+.2f}c")
        assert note_idx == exp_idx
        assert abs(detected_f - f) < 0.1

    print("ALL STRINGS PASS PERFECTLY (CENTS < 0.1c)!")

if __name__ == '__main__':
    test()
