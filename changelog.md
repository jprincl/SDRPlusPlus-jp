# Changelog

## v1.2.2-beta3 - 2026-07-14

### Added

- KiwiSDR: "Recent" servers list — the last 8 servers connected to (whether picked on the map, typed by hand or re-picked from the list itself) are remembered across sessions together with their location and served frequency band, for quick reconnection without going through the map.
- Level meter: clicking the meter toggles between a dBFS scale (0 dB at the right edge, negative to the left) and the original positive scale; tick labels and the numeric readout follow the active mode.

### Changed

- SNR readout reworked: SNR is now reported as the held peak in-band level over the out-of-VFO noise floor averaged over the same peak-hold window, so the value stays steady on keyed CW instead of sagging between dits and dashes. The separate "SNR Smoothing" display option was removed — smoothing of the readout is now intrinsic.
- Level meter layout: on narrow windows the meter now switches to sparse (every-other) tick labels and hides the numeric readout instead of overlapping labels, and it can grow wider on large windows.
- Application icons updated with the fork-specific (iak) branding, including the macOS and Windows icon variants.

### Fixed

- Fixed an out-of-bounds read in the FFT signal-level calculation (`calculateVFOSignalInfo`): with the VFO at the upper band edge the peak search read one float past the end of the FFT row and crashed the FFT thread (inherited from upstream).
- Fixed a startup crash-loop on Android caused by the SNR smoothing config keys being read without registered defaults (resolved by the removal of that option).
- USB streaming teardown was made sound across all libusb-based sources, fixing use-after-free crashes when stopping a source — most visibly the SIGSEGV in the Airspy HF+ source after a USB unplug on Android:
  - On Android, unplugging a device no longer closes the USB file descriptor under a live libusb handle; the connection is kept alive until the native side releases it, so cancelled transfers are reaped cleanly and a replug cannot be handed a stale connection.
  - The QMX Android USB backend's transfer teardown was reworked (submission/cancellation serialized, exact in-flight accounting, bounded 500 ms drain, leak-instead-of-free if the drain expires) and serves as the reference implementation.
  - The same defect class was fixed in the bundled drivers via PR-ready patches: libairspy, libairspyhf, libhydrasdr and librtlsdr all gained exact in-flight transfer accounting, resubmission gating during cancel, a verified cancellation drain, and a latch-and-leak fallback instead of freeing memory libusb still references. Also fixes rare stop-time crashes on desktop. An upstream librtlsdr hang (`rtlsdr_close()` spinning forever after an error-path exit from async reading) was fixed along the way.
- KiwiSDR map fixes: clicks can no longer select markers that aren't drawn (hidden full servers in Hide Full mode) or leak through a marker to pan the map, overlapping markers now offer a pick-one popup instead of silently choosing the topmost, the Zoom In/Out buttons zoom around the viewport centre, pinch-to-zoom works on Android, the served band is shown in MHz, and on phone-landscape layouts the Test button stays reachable in a fixed footer.

## v1.2.2-beta2 - 2026-07-11

### Breaking change
- Due to the change in network protocol (authentization, source tuning range synchronization) the SDR++-iak network protocol is no more compatible with the upstream SDR++. SDR++-iak therefore rejects connection to non-iak SDR++ forks.

### Added

- Dragon Labs source module, merged from upstream SDR++: CR8-1725 device discovery/selection through `libdlcr`, 12.5 MS/s input, internal/external clock selection, channel selection, and LNA/mixer/VGA gain controls.
- Radio squelch mode system from upstream SDR++: `Power`, `CTCSS (Mute)` and `CTCSS (Decode Only)` modes, a full CTCSS tone table with `Any`, received-tone diagnostics, and radio-module interface commands for squelch mode/level, CTCSS tone and high-pass control.
- Recorder filename timestamps can now be generated in either local time or UTC.
- SDR++ server password authentication: protocol hello/fork compatibility checks, PBKDF2/HMAC challenge-response ported from SDRPlusPlusBrown by @sannysanoff, per-server saved auth keys, explicit server-busy/auth-failed dialogs, and a "Forget saved password" control.
- Shared RX prebuffer implementation for networked sources. The SDR++ server source now exposes Disabled/50/100/250/500/1000 ms RX prebuffer choices, with Disabled acting as a true live bypass instead of a zero-length buffering path.
- Android release CI now builds a signed universal Play Store `.aab` and splits the universal APK into the published arm and x86 APK artifacts.

### Changed

- Merged the current upstream SDR++ master into this fork, preserving the fork-specific modules and dependency/build-system work while bringing in upstream radio, recorder, rigctl, Dragon Labs, RDS and CI fixes.
- Source tuning limits are now managed by `SourceManager`, are aware of the tuning offset used for up/downconverters, and are forwarded through the SDR++ server protocol to remote clients.
- The main frequency selector is now constrained to the valid source range for Airspy (24 MHz-1.75 GHz), Airspy HF+ (0-260 MHz), RTL-SDR (100 kHz-1.75 GHz), QMX/QMX server (100 kHz-60 MHz), KiwiSDR served ranges, and remote SDR++ server sources.
- KiwiSDR tuning limits now follow the selected directory entry's served band and refine to the exact server-reported `freq_offset + bandwidth` range while connected.
- WFM RDS can now disable incremental PS/radio-text updates, showing only complete updated strings when that option is off.
- `SmGui` gained separator and formatted-text helpers with bounded buffers.
- Linux CI now includes Ubuntu Resolute builds.

### Fixed

- SDR++ server client/session handling was hardened: per-session buffers prevent displaced clients from corrupting a new connection, handshakes must complete before a client can take over the live slot, heartbeats reclaim orphaned sessions, failed authentication is rejected immediately, and malformed packets, command payloads and decompressed baseband sizes are validated before use.
- SDR++ server remote state synchronization now applies pushed sample-rate and tuning-limit changes on the GUI thread and re-applies them when reselecting the source.
- SDR++ server and KiwiSDR live prebuffer switching now cleanly swaps between buffered and live paths without leaving stale buffering state behind.
- Rigctl server `get_mode` responses now include the selected VFO bandwidth when available.
- WFM RDS continuation/stale-text handling fixes from upstream were merged.
- HydraSDR source/build compatibility fixes for newer `libhydrasdr` and Windows builds were merged from upstream.

## v1.2.2-beta1 - 2026-07-07

### Added

- Manual/auto AGC switch for the SSB (USB/LSB/DSB), CW/CW-R and AM demodulators, adopted from the [SDRPP](https://github.com/qrp73/SDRPP) fork by @qrp73: an AGC checkbox with a gain slider in dB (for AM an Off/Carrier/Audio AGC mode selector). In auto mode the slider shows the live AGC gain; in manual mode it sets a fixed gain, with the last AGC gain carried over so the audio level doesn't jump. Manual gain is still clipped to the maximum output amplitude to protect ears and speakers.
- PlutoSDR improvements adopted from the [SDRPlusPlus](https://github.com/F5OEO/SDRPlusPlus) fork by @F5OEO (Evariste Courjaud): RX1/RX2 RF input selection for hardware with a second RX input (Pluto+, ANTSDR, LibreSDR RevC), and buffer underflow / ADC overload status indicators in the source menu. A config persistence bug in the original RF input selection was fixed during the port; the CS8 streaming mode (Tezuka firmware only) and the removal of the FIR-based low sample rates were deliberately not adopted.

- FFT windows extended from 3 to 7, adopted from the [SDRPP](https://github.com/qrp73/SDRPP) fork by @qrp73: Rectangular, Hamming, Hann, Blackman, Nuttall, Blackman-Harris 4-term (−92 dB sidelobes) and Blackman-Harris 7-term (−180 dB sidelobes, for spur hunting next to strong carriers).

- Recorder: FLAC container and 24-bit PCM sample type, adopted from the [SDRPP](https://github.com/qrp73/SDRPP) fork by @qrp73. FLAC compresses baseband IQ losslessly to roughly half the size (integer sample types only; sample rates up to 1.048 MHz — a libFLAC format limit — with higher rates failing cleanly at record start). Recording uses libFLAC 1.5.0, statically bundled on Windows/Android/macOS/AppImage and the distro package on Linux .deb builds. The MP3 (LAME) recording option from the same fork was not adopted.
- Recorder: Opus lossy audio recording (`.opus`, Ogg-Opus container), in the spirit of the MP3 option from the [SDRPP](https://github.com/qrp73/SDRPP) fork by @qrp73 but using the royalty-free Opus codec instead of MP3. Available for demodulated audio at 8/12/16/24/48 kHz (Opus's native rates), with a bitrate slider (16–256 kbps, default 128). Uses libopus 1.5.2 + libogg 1.3.6, statically bundled on Windows/Android/macOS/AppImage and distro packages on Linux .deb builds.
- File source: playback of finalized FLAC recordings, the read counterpart to the recorder's FLAC container. `.flac` files are streamed through libFLAC's decoder and presented to the rest of the module exactly as an equivalent PCM WAV would be (8/16/24/32-bit, mono/multi-channel), so all existing handling — frequency-from-filename parsing, looping, the format/duration readout — applies unchanged. The reader is picked from the file's magic bytes, not its extension, and the file dialog now lists `*.wav` and `*.flac`. The FLAC decoder lives in core next to the recorder's encoder (libFLAC stays a private core dependency). Unlike the WAV reader, an unfinalized FLAC left by a crashed recorder (no total sample count in its header) is not recovered — a compressed stream's length can't be derived from the file size — and is reported as unplayable rather than mis-timed.

### Changed

- FFT amplitude calibration (also from @qrp73): the FFT window is now normalized to unity coherent gain, so a sinusoid reads its true dBFS amplitude with any window, any FFT size/rate combination, and any zero padding. Previously the reading was only correct for the rectangular window, and the displayed level also sagged when the FFT rate was raised. **With the default Nuttall window all FFT/waterfall levels now read ~9 dB higher (Blackman: ~7.5 dB), so saved waterfall min/max ranges will need a one-time re-adjustment.** The noise floor still shifts slightly between windows (equivalent noise bandwidth) — that is physically correct and matches bench spectrum analyzers.
- The SNR meter in the top bar was replaced by a signal level meter, adopted from the [SDRPP](https://github.com/qrp73/SDRPP) fork by @qrp73: a level bar in dBFS (peak FFT level within the VFO bandwidth) with a peak hold marker over the last 10 FFT frames, plus a numeric readout of the peak level and the SNR.
- File source WAV support extended, adopted from the [SDRPP](https://github.com/qrp73/SDRPP) fork by @qrp73: plays RF64 (>4 GB recordings), WAVE_FORMAT_EXTENSIBLE, 8/16/24/32-bit PCM and 32/64-bit float, mono and multi-channel files, and recovers unfinalized recordings from crashed recorders. The sample format is auto-detected (the "Float32 Mode" checkbox is gone), the menu shows the detected format and duration, filename frequencies are also parsed with kHz/MHz/GHz units and decimals (HDSDR/SDR Console naming), and selecting a file during playback — previously a crash — is now disabled.
- Squelch improvements adopted from the [SDRPP](https://github.com/qrp73/SDRPP) fork by @qrp73: 1 dB hysteresis when closing and a 100 ms above-threshold hold before unmuting, so the squelch no longer chatters at the threshold or pops open on short noise spikes. The hold is sample-count based in this port, exact at any IF sample rate.
- PlutoSDR (also from @F5OEO): larger IIO buffers (50 ms, 8 kernel buffers) to avoid underflows at high sample rates over USB and network, device scan now covers both the USB and network backends, extended device whitelist (Pluto+, ad9361, FISH), and the full libiio description is shown as the device name.

### Fixed

- Squelch level dB math (also from @qrp73): the threshold now uses `20*log10` of the mean signal amplitude instead of `10*log10`, so the slider value is in real dB. Previously saved squelch levels will trigger differently and need re-adjusting.

## v1.2.2-beta - 2026-07-06

### Added

- Frequency manager: merged the improvements from [`bookmark_manager`](https://github.com/darauble/bookmark_manager) by @darauble (Darau Ble), with contributions by @daviderud (Davide Rovelli), directly into the stock frequency manager module (the config format stays backward compatible): waterfall labels arranged in up to 10 rows with overlap avoidance, per-list colors, text-only and flag-style label options, UTC start/end times and week days per bookmark with off-air greying (UTC helpers from `shortwave-station-list-sdrpp` by Otto Pattemore), notes and geo info fields, a sortable bookmark table, click-to-select of waterfall labels, and moving bookmarks between lists. Several issues found in the original were fixed during the merge (crash on malformed list color, buffer overflows on long imported strings and malformed `days` arrays, stale label hit-testing, new bookmarks ignoring the selected target list, and a crash on malformed import files).
- IF noise reduction (LogMMSE/OMLSA) module, ported from SDRPlusPlusBrown by @sannysanoff.

### Changed

- IF noise reduction hardening and refactoring after the port: crash safety and correct attach/detach to the IF chain, TX and samplerate awareness, removal of debug scaffolding and dead code, value-semantics arrays with a trimmed API and opt-in buffer recycling, a generated `expn()` lookup table replacing the hardcoded one, and UI polish.
- Const correctness fixes of `dsp::complex_t`.
- CI: Android Debug APKs are now opt-in and the Build Android workflow can be triggered manually (`workflow_dispatch`).

### Fixed

- IF noise reduction: fixed a buffer overflow in `logmmse_all()` output with large IF bandwidths and an out-of-bounds read in the `expn()` lookup.

## v1.2.2-alpha4 - 2026-07-04

### Added

- New CMake-based dependency build system, ported from PrusaSlicer's (written by Tamas Meszaros, @tamasmeszaros): all third-party libraries are now built from source instead of pulling prebuilt binary blobs. This enables native Windows on ARM64 builds and removes the need for the custom Android SDR-kit Docker image.
- Native Windows on ARM64 builds.
- Linux AppImage builds, with isolated config root.
- Radiosonde decoder plugin, merged directly into the `iak` fork from `sdrpp_radiosonde` by @dbdexter-dev (Davide Belloli), built on his `sondedump` decoding library.
- Spots module, merged from [`sdrpp-spots`](https://github.com/gerner/sdrpp-spots) by @gerner.
- WebSDR view module, based on the KiwiSDR map and waterfall code from SDRPlusPlusBrown by @sannysanoff.
- libcurl integration for HTTPS and secure WebSockets, statically bundled and exported through `sdrpp_core`.
- KiwiSDR improvements merged from the `qrp73`/`sdr73` forks by @qrp73: server selection by address and port, server band parsing, and map improvements (day/night terminator, country colors, hover tooltips, and a runtime-toggleable marker style).

### Changed

- Reworked the WebSocket client for RFC 6455 correctness and hardening (handshake validation, fragmentation, redirect handling, and concurrency/write safety).
- Networking hardening and cleanup across the KiwiSDR and web modules.

### Fixed

- QMX on Linux: fixed an exception when enumerating ALSA audio devices (#3).
- Fixed input events leaking through overlapping ImGui windows.
- Fixed `flog` string conversion for all numeric types supported by `to_string`.
- Fixed a Debian package build regression where optimization was disabled.

## v1.2.2-alpha3 - 2026-04-21

### Added

- Merged KiwiSDR support from SDRPlusPlusBrown by @sannysanoff.

### Changed

- Further steps to fork from upstream with `iak` branding so that the `iak` fork could live side by side with upstream SDR++ on the same system. Fixed Linux Debian packages broken by previous `iak`ization.
- Fix of default audio sink on Linux with ALSA audio API selected: Don't let the ALSA audio queue dry up if radio is stopped. The ALSA sink would not restart if the radio was paused and restarted.

## v1.2.2-alpha2 - 2026-04-19

### Changed

- Reworked high DPI display handling: The display DPI is newly respected.
- A new user display scaling factor now multiplies with the display native DPI scale.
- General clean-up of the UI scaling code to respect the display and user scaling factors.
- Expanded the touch region around the view splitters on Android so that they could be touched by thick fingers.

## v1.2.2-alpha1 - 2026-04-19

### Added

- Added Android audio rerouting when the preferred audio output changes.
- Added Ctrl+scroll waterfall bandwidth zooming.
- Added initial Android multi-touch support for waterfall zooming.

### Changed

- Updated CI artifact naming to include version information, commit distance, and commit hash from `git describe`.
- Added the `-iak` infix to release artifact names and to Linux Debian package naming to keep this fork isolated from upstream SDR++ packages.
- Reworked CI release handling to read version information from `version.h`.

### Fixed

- Fixed Android audio sink behavior by pausing and restarting audio output to work around devices that lock when the audio queue dries up.
