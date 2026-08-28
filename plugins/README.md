# MODEP Guitar Plugins Suite for Patchbox OS & MODEP Cloud

A high-performance suite of native LV2 guitar effect plugins with custom stompbox interfaces tailored for **MODEP** (MOD Duo/Duo X emulator) and **Patchbox OS** on Raspberry Pi.

---

## Plugins Included

### 1. Harmonic Tremolo (`harmonic-tremolo.lv2`)
Vintage Fender Tri-Verb / Brownface style harmonic tremolo:
- **Cytomic SVF Crossover**: Splits signal into low and high frequency bands with pristine phase alignment.
- **Dual Anti-Phase LFO**: 180-degree out-of-phase modulation between bands for rotary/vibe depth.
- **Asymmetric Triode Tube Warmth**: Authentic tube saturation harmonics.
- **Controls**:
  - `Rate`: 0.1 Hz – 20.0 Hz
  - `Depth`: 0.0 – 1.0
  - `Crossover`: 150 Hz – 4000 Hz
  - `Warmth`: Harmonic tube saturation
  - `Waveform`: Sine, Triangle, Tube-Sine, Square
  - `Mix`: Dry / Wet blend
  - `Bypass`: On / Off Stomp Footswitch with Amber LED

---

### 2. Cyber-Denoiser PRO (`cyber-denoiser.lv2`)
Zero-latency 10-band spectral subtraction noise suppressor:
- **One-Touch Noise Learn**: Captures background noise floor (coil buzz, HVAC, traffic) for 2.5 seconds and auto-calibrates surgical suppression thresholds with +3dB headroom.
- **Streamlined Stompbox UI**: No cluttered sliders needed. One big illuminated **LEARN NOISE** button and a master **REDUCTION** knob.
- **Adaptive Release & Phase Cancellation**: Eliminates fluttering and musical noise artifacts.
- **Controls**:
  - `Bypass`: On / Off Footswitch with Neon Violet LED
  - `Learn Noise`: One-touch capture button
  - `Reduction`: 0% – 100% suppression intensity
  - `Low Cut`: 40Hz sub-bass rumble filter
  - `Listen`: Audition removed noise component

---

### 3. Galaxy Strobe Tuner (`galaxy-strobe-tune.lv2`)
Ultra-precision strobe tuner pedal:
- **Autocorrelation + 74dB Smart AGC**: Robust fundamental tracking across all guitar/bass strings.
- **Real-Time Strobe Display**: Animated rotating disc, large note letter display, and ±0.1 cent accuracy.
- **Stage Mute Feature**: When `MUTE` is enabled, activating the tuner completely silences output audio for silent on-stage guitar tuning. When bypassed, audio passes through with zero coloration.
- **Sweetened Tuning Profiles**: Equal Temperament, James Taylor, Buzz Feiten, Peterson GTR, Open D, Open G.
- **Controls**:
  - `Bypass`: Tuner Enable / Bypass Footswitch with Cyan LED
  - `Mute`: Toggle silent tuning mode
  - `Ref A`: 400 Hz – 480 Hz (Default 440 Hz)
  - `Profile`: Sweetened profile selector
  - `Capo`: 0 – 12 fret offset

---

## Installation on Patchbox OS / Raspberry Pi

Run the automated installer on your Raspberry Pi:

```bash
chmod +x install_to_modep.sh
./install_to_modep.sh
```

Or build manually:

```bash
cd plugins
make all
sudo make install
```

---

## Distribution for MODEP Cloud

The pre-packaged archive files are ready in the root directory:
- `modep_guitar_plugins_bundle.zip`
- `modep_guitar_plugins_bundle.tar.gz`
- `dist/harmonic-tremolo.lv2`
- `dist/cyber-denoiser.lv2`
- `dist/galaxy-strobe-tune.lv2`
