# qrp73/SDRPP — features worth porting

Review date: 2026-07-06. Reviewed https://github.com/qrp73/SDRPP against our fork
(local clone used for the diff: `scratchpad\qrp73-sdrpp`).

## What the fork is

qrp73/SDRPP is a **receiver-quality and file-handling focused fork based on SDR++ 1.1.0**
(~2 years behind our 1.2.2-alpha base), still active (last commit May 22, 2026,
~1,655 commits). It adds no hardware we lack — we have far more source modules.
Since it forked at 1.1.0, nothing merges cleanly; everything is an adapt-and-port job.
GPL-3.0, so license-compatible with our fork.

## Worth porting (in priority order)

### 1. Squelch hysteresis + hold — tiny patch, immediate win — **PORTED 2026-07-07**
Their `core/src/dsp/noise_reduction/squelch.h` adds 1 dB hysteresis and a 100 ms hold
before unmuting, and fixes the dB math (upstream/ours uses `10*log10` on an amplitude
mean where `20*log10` is correct). Ours is verbatim upstream with none of that.

Caveat: their counter is a `static int` inside `process()`, which is a bug with
multiple VFOs — make it a member when porting.

Port notes: counter made a per-instance member; the hold is sample-count based
(`Squelch::setSamplerate`, wired from `selectDemodByID`) instead of their "10 blocks
≈ 100 ms" assumption. Behavior change for users: with the `20*log10` fix the same
signal reads twice the dB value, so previously saved squelch levels trigger
differently and need re-adjusting (slider range -100..0 dB still covers it).

### 2. Manual/auto AGC switch for CW/SSB/AM/DSB — small, very relevant to QMX/ham use — **PORTED 2026-07-07**
Each demodulator gets an AGC on/off checkbox plus a gain slider in dB; in auto mode the
slider displays the live AGC gain, in manual mode it sets fixed gain. Clean
implementation: `dsp::demod::SSB/AM/CW` gain `setEnabled`/`setAGCGain`/`getAGCGain`,
plus ~40 lines of UI per demod header (see their
`decoder_modules/radio/src/demodulators/usb.h`). Ours only has attack/decay sliders.

Port notes: covers USB/LSB/DSB/CW/CW-R (checkbox + gain slider) and AM
(Off/Carrier/Audio AGC mode combo, replacing the "Carrier AGC" checkbox; the legacy
`carrierAgc` config key is migrated to `agcMode`). Unlike qrp73, the
`dsp::demod::SSB/AM/CW::init()` signatures were kept unchanged (AGC state is applied
via setters after init) so third-party callers and `vor_receiver` don't break. Attack/
decay sliders are greyed out in manual mode, the gain slider shows live AGC gain in
auto mode, and the last AGC gain carries over when switching to manual.

### 3. file_source format support — self-contained, high interop value
Their `source_modules/file_source/src/wavreader.h` (252 lines, drop-in replacement
shape) handles:
- RF64 (>4 GB recordings)
- WAVE_FORMAT_EXTENSIBLE (incl. subformat GUID parsing)
- IEEE float 32/64
- 8/16/24/32-bit PCM
- recovery of broken WAVs with wrong sample counts in the header

Ours is the bare upstream reader (effectively 16-bit PCM only). Matters for playing
back recordings from SDR#, SDR Console, HDSDR, etc.

### 4. Recorder: FLAC, MP3 (VBR), and 24-bit PCM containers — medium effort
Contained in `core/src/utils/wav.{h,cpp}` + recorder `main.cpp` +
`cmake/FindFLAC.cmake` / `cmake/Findmp3lame.cmake`. Code is tidy, but it adds
**libFLAC and LAME to our static-deps CI** across Windows/Android/macOS/Linux — real
but manageable work given our deps infrastructure. FLAC for baseband IQ recording is a
genuinely useful storage saver.

### 5. Level meter replacing SNR meter — small-medium
`ImGui::LevelMeter(level, levelMax, snr, …)` (their
`core/src/gui/widgets/snr_meter.{h,cpp}`) gives audio level + peak-hold + numeric
dB/SNR readout instead of the bare SNR bar. Nice operator-facing improvement; needs
main_window wiring.

### 6. FFT window functions + unity gain — small
Adds Blackman-Harris-4/7, Blackman-Nuttall, Nuttall, Hamming, Hann, cosine as separate
headers under `core/src/dsp/window/`, with unity-gain normalization so FFT amplitude
readings are calibrated, plus a 1M FFT size option. Easy port into `iq_frontend` +
display menu.

## Not worth it / already covered

- **KiwiSDR map browser** (~1,000 lines: geomap + earcut) — we already have an
  equivalent from the SDRPPBrown port (`gui/brown/kiwisdr_map.h`, used by our
  `websdr_view` module). Skip.
- **SDL2 / OpenGL ES headless backend** (RPi KMS-DRM/Wayland) — significant platform
  work, irrelevant to our Windows/Android focus.
- **hpsdr_source** — we have hermes_source and our fork targets QMX; niche unless
  someone asks.
- **RTL-SDR "sample conversion fix"** — it's `-127.5` vs upstream's `-127.4` offset;
  negligible.
- **7-segment frequency font, SDR#/SpectraVue waterfall palettes, antialiasing
  setting** — cosmetics; the palettes are trivial config files if ever wanted.
- **Waterfall zoom / mouse-click-leak fixes** — real fixes, but entangled with their
  1.1.0-era waterfall which has diverged from our 1.2.x one; extracting them needs
  commit archaeology for uncertain payoff. Check per-fix whether upstream 1.2.x already
  fixed the same issues.
- **frequency_manager LO save** — we've modified frequency_manager ourselves; their
  change would conflict and the feature is minor.

## Suggested first step

Port the squelch + manual AGC pair — best value-to-effort in the list.
