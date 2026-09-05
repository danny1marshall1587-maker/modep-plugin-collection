# Cyber TONE3000 LV2 Plugin for MODEP & MOD Desktop

An official **TONE3000** NAM (Neural Amp Modeler) & IR (Impulse Response) Cloud Player plugin engineered specifically for **MODEP (Raspberry Pi 5/4)** and **MOD Desktop**.

Featuring real-time zero-latency neural inference, a 4-block interactive signal chain (Noise Gate -> NAM Core -> Cab/Acoustic IR -> 3-Band Tone Stack), and a **full-screen interactive overlay modal** (built like the Cyber Strobe Tuner addon) that allows live searching, auditioning, and downloading tones directly from the TONE3000 cloud platform.

---

## Features

- **Full-Screen TONE3000 Overlay (Tuner-Style Addon)**:
  - Click **[OPEN TONE3000 CLOUD]** on the pedal to expand a dark-mode glassmorphism interface across your entire screen.
  - Interactive signal chain visualizer showing active blocks.
  - Category filters: *Acoustic Guitars & Mics*, *Clean Amps*, *Edge of Breakup*, *High Gain Leads*, *Bass Rigs*, *Cabinet IRs*.
  - Live search across community captures with instant keyword filtering.
  - **In-Browser Auditioning**: Preview guitar chord clips directly before loading.
  - **Single-Click Loading**: Instantly updates the DSP engine and pedal LCD screen with no audio dropout.
  - Local .nam and .wav IR drag-and-drop zone.
- **Built-In High-Fidelity TONE3000 Captures**:
  - **Acoustic Guitar Studio Mics**: Martin D-45 Studio Condenser (Neumann U87), Taylor 814ce Grand Auditorium (Royer R-121 Ribbon), Gibson J-45 Vintage Round Shoulder (Shure SM7B), Guild F-512 12-String Acoustic.
  - **Vintage & Boutique Cleans**: Fender '65 Deluxe Reverb Blackface, Fender '59 Tweed Bassman, Dumble Overdrive Special Clean, Matchless DC-30 Chime.
  - **Edge of Breakup & British Crunch**: Vox AC30 Top Boost 1964, Marshall Bluesbreaker 1962 (JTM45), Orange Rockerverb 50 MkIII.
  - **High Gain & Modern Leads**: Marshall JCM800 2203, Soldano SLO-100 Super Lead, Mesa/Boogie Dual Rectifier Multi-Watt.
  - **Bass Rigs**: Ampeg SVT Classic 8x10 Tube Stack, Darkglass Microtubes B7K Ultra.
- **Zero-Latency Real-Time Audio DSP**:
  - 100% zero-latency processing optimized for Raspberry Pi 5 ARM Cortex-A76 NEON.
  - Integrated fast RMS noise gate with hysteresis (Off / -80 dB to -20 dB).
  - Dynamic tube power sag follower and asymmetric hyperbolic saturation.
  - Paired cabinet / acoustic microphone impulse response filters with IR Blend control.
  - Active 3-band tone stack (Bass @ 100Hz, Mid @ 750Hz, Treble @ 4.5kHz) with +-12 dB range.

---

## Port Specifications

| Index | Symbol | Name | Type | Range | Default | Description |
| :---: | :--- | :--- | :---: | :---: | :---: | :--- |
| **0** | in | Audio In | In | Audio | -- | Mono instrument input |
| **1** | out | Audio Out | Out | Audio | -- | Processed output |
| **2** | ypass | Bypass | In | 0..1 | 1 (On) | Master footswitch bypass |
| **3** | input_gain | Input Gain | In | -24..+24 dB | 0 dB | Input level trim |
| **4** | output_level | Output Level | In | -24..+24 dB | 0 dB | Master output volume |
| **5** | gate | Noise Gate | In | -80..-20 dB | -80 dB | Gate threshold (Off at -80) |
| **6** | ass | Bass | In | -12..+12 dB | 0 dB | 100 Hz low shelf / bell |
| **7** | mid | Middle | In | -12..+12 dB | 0 dB | 750 Hz peaking filter |
| **8** | 	reble | Treble | In | -12..+12 dB | 0 dB | 4.5 kHz high shelf |
| **9** | model | NAM Model | In | 0..15 | 0 | Active neural capture profile |
| **10** | ir | Cab / Mic IR | In | 0..13 | 0 | Paired impulse response |
| **11** | ir_blend | IR Blend | In | 0..100 % | 100 % | Dry NAM vs IR Cab mix |
