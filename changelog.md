# Changelog

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
