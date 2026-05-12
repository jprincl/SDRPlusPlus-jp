# Dependency build: comparison with PothosSDR

This note records findings from comparing the SDR++ iak `deps/` build system
(an in-progress replacement for the PothosSDR-based pre-built dependencies)
against the upstream `pothosware/PothosSDR` super-build at
<https://github.com/pothosware/PothosSDR>.

The current `deps/` system is modeled on PrusaSlicer's `deps/` subsystem
(same `+<Name>/<Name>.cmake` recipe convention, `add_cmake_project()`
wrapper). It was written without seeing PothosSDR. This document captures
what PothosSDR does that we should learn from, and what we already do better.

---

## Inventory snapshot

### SDR++ `deps/` recipes (25 packages)

- **Driver/system**: `libusb` (via libusb-cmake wrapper), `pthreads`
  (pthreads4w, GerHobbelt fork — Windows only)
- **Math/IO**: `fftw3`, `volk`, `zlib`, `zstd`, `libxml2`, `codec2`,
  `glfw3`, `portaudio`, `rtaudio`
- **SDR hardware**: `libairspy`, `libairspyhf`, `libhackrf` (Rouma fork),
  `librtlsdr` (Rouma fork), `libbladeRF`, `LimeSuite`, `libfobos` (Rouma),
  `librfnm` (Rouma), `libhydrasdr`, `libperseus-sdr` (Rouma),
  `libiio`, `libad9361`
- **Closed/binary**: `sdrplay` (zip downloaded from sdrpp.org)

Mechanism:

- PrusaSlicer-style `+<Name>/<Name>.cmake` auto-discovery
- `add_cmake_project()` wrapper around `ExternalProject_Add`
- `DEP_<Name>_DEPENDS` graph wired with `add_dependencies()`
- `sdrpp_emit_imported_config()` synthesizer for libs that don't ship
  `Config.cmake`
- Single-config build trees, one configuration end-to-end per tree (CRT and
  optimization match across app and every dep), driven by per-config presets
  in `CMakePresets.json`
- Global `LIBUSB_*` / `THREADS_PTHREADS_*` injection via `DEP_CMAKE_OPTS`
  to satisfy bundled `FindLIBUSB.cmake` / `FindThreads.cmake` in many SDR libs

### PothosSDR (Windows-only super-package)

- Same hardware drivers, often older / different upstreams:
  `mossmann/hackrf`, `librtlsdr/development`, `analogdevicesinc/libiio v0.19`,
  `Nuand/bladeRF master`, `EttusResearch/uhd v4.0.0.0`
- Common libs: `libusb v1.0.24`, `pthreads-win32`, plus a much wider Pothos
  ecosystem (Qt5, wxWidgets, Boost prebuilt, Poco, ZeroMQ, fdk_aac,
  faac/faad2, libsndfile, Pothos, GNURadio) — almost all unused by SDR++
- A complete **Soapy module zoo**: `SoapyAirspy`, `SoapyHackRF`,
  `SoapyRTLSDR`, `SoapyBladeRF`, `SoapyUHD`, `SoapyPlutoSDR`,
  `SoapySDRPlay3`, `SoapyAirspyHF`, `SoapyOsmo`, `SoapyRedPitaya`,
  `SoapyAudio`, `SoapyRemote`, `SoapyIris`, `SoapyNetSDR`, plus `LimeSuite`
  — relevant only when `OPT_BUILD_SOAPY_SOURCE=ON`
- Prebuilt blobs:
  - **SDRplay API** via Windows registry
    `HKLM\SOFTWARE\SDRplay\Service\API`
  - **Boost** from a local install at `C:/local/boost_1_75_0`
  - **Cypress FX3 SDK** from `C:/Program Files (x86)/Cypress/EZ-USB FX3 SDK/1.3`
- Conventions:
  - `EP_UPDATE_DISCONNECTED` (no auto-update on reconfigure)
  - explicit aggregate `*-update` target for refreshing all repos
  - per-project `licenses/<name>/` segregation
  - `rebuild_all.bat` for full clean+reconfigure
  - post-install DLL relocation for `bladeRF`/`hackRF` (DLL ends up in `lib/`,
    moved to `bin/`)

---

## Findings

### Tier A — correctness / reproducibility (must-fix)

1. **Floating git refs.** Most recipes track `master`/`main`. The Rouma forks
   (`hackrf`, `rtl-sdr`, `libfobos`, `librfnm`, `libperseus-sdr`) and
   `hydrasdr/*`, `airspy/*`, `Nuand/bladeRF`, `volk`, `codec2`, `libusb-cmake`
   will silently break on upstream churn. (`LimeSuite v23.11.0` is already
   pinned — good.) → Pin to commit SHAs or release tags.
2. **`URL_HASH SHA256=<TODO>` placeholders** in `libxml2`, `libiio`,
   `libad9361`, `zlib`, `zstd`, `portaudio`, `sdrplay`. → Fill after first
   verified build.
3. **No `UPDATE_DISCONNECTED`.** Every reconfigure re-pulls. Add
   `UPDATE_DISCONNECTED 1` in `add_cmake_project()` plus a `deps_update`
   aggregate target that re-fetches on demand (PothosSDR pattern).

### Tier B — coverage gaps surfaced by PothosSDR

4. **Install-layout drift on hackRF/bladeRF.** PothosSDR explicitly post-
   install-moves DLL→`bin/` and `.lib`→`lib/`. Our `libhackrf` (Rouma fork)
   and `libbladeRF` likely produce the same broken layout. Verify on a clean
   build; if broken, add a post-install copy step like
   `+libad9361/install_libad9361.cmake` already does.
5. **Missing recipes for SDR++ build options:**
   - `OPT_BUILD_SOAPY_SOURCE` → no `+soapysdr/soapysdr.cmake`
   - `OPT_BUILD_USRP_SOURCE` → no `+libuhd/libuhd.cmake` (needs Boost story)
   - `OPT_BUILD_HAROGIC_SOURCE` → no `+htra_api/` (proprietary, like sdrplay)
   - `OPT_BUILD_KCSDR_SOURCE` → no `+libkcsdr/`

   All opt-OFF by default, so not blocking — but the deps system should at
   least error clearly when a consumer needs one.
6. **SDRplay registry fallback.** Our `+sdrplay/sdrplay.cmake` hard-fetches
   `https://www.sdrpp.org/SDRplay.zip`. PothosSDR reads
   `HKLM\SOFTWARE\SDRplay\Service\API` from the official installer. Add
   registry path as preferred source when present, fall back to the bundle.
### Tier C — polish

8. **License aggregation.** PothosSDR collects per-package COPYING into
   `licenses/<name>/`. Useful for redistribution. Optional `LICENSE_FILES`
   argument on `add_cmake_project()`.
9. **`deps_clean` / `rebuild_all` aggregate target** mirroring PothosSDR's
   batch script.
10. **Pin `codec2`** to a specific tag (the recipe has a TODO).
11. **Missing Linux/macOS presets** — `CMakePresets.json` has only VS2026 and
    Android stubs.

### Tier D — explicitly out of scope

- Pothos framework, GNURadio, Qt/wxWidgets/qwt, Poco/ZeroMQ, faac/faad2/
  fdk_aac, libsndfile, MPIR, gsl, muparserx, log4cpp — SDR++ uses none of
  these.
- The 17 `Soapy*` per-vendor modules — SDR++ has native source modules for
  Airspy/HackRF/etc.; Soapy is a generic fallback. Only `soapysdr` (the core
  ABI) is needed.

---

## Things SDR++ deps already does *better* than PothosSDR

Don't regress these:

- Cross-platform from day 1 — Android stubs, `if (ANDROID) return()`, WIN32
  conditionals. PothosSDR is Windows-only.
- Auto-discovery via `+<Name>/` folders (PothosSDR has hard-coded `include()`s).
- `sdrpp_emit_imported_config()` synthesizes `Config.cmake` for libs without
  one — cleaner consumer story than PothosSDR's per-project
  `-DLIBX_INCLUDE_DIR=...` pattern.
- Per-config build trees (`Debug`, `RelWithDebInfo`, `Release`) — switching
  configurations switches presets, no CRT-mixing risk. PothosSDR is
  Release-only.
- Per-package `sdrpp_deps_SELECT_*` gating, `PACKAGE_EXCLUDES` regex.
- `OPT_BUILD_DEPS` autobuild integration via `deps/autobuild.cmake`.
- `CMAKE_POLICY_VERSION_MINIMUM=3.5` injection — handles CMake 4.x dropping
  support for projects requesting `cmake_minimum_required(VERSION < 3.5)`.

---

## What SDRPlusPlusBrown does (and what we can learn)

SDRPlusPlusBrown's Windows CI (`.github/workflows/build_all.yml` →
`build_windows` job) takes a fundamentally different approach: it uses the
prebuilt `PothosSDR-2020.01.26-vc14-x64.exe` as a base, surgically patches a
few files, then builds only the things PothosSDR doesn't ship. Worth reading
before designing a "fast-track" CI path for our deps system.

### Their strategy (summary)

1. Download `PothosSDR-2020.01.26-vc14-x64.exe` from
   `downloads.myriadrf.org`.
2. **Extract the NSIS installer with 7-Zip** — no install, no UI, no registry:
   ```
   7z x pothos.exe -o"C:/Program Files/PothosSDR/"
   ```
3. **Patch `libusb` DOWN to v1.0.23** — overwrite `bin/libusb-1.0.dll` and
   `lib/libusb-1.0.lib` from the upstream libusb release archive. PothosSDR's
   bundled libusb is too new for some consumers.
4. **Patch `rtlsdr` UP to osmocom 2024-06-23** — overwrite
   `bin/rtlsdr.dll`. PothosSDR's 2020 rtlsdr is buggy.
5. SDRplay via the same `sdrpp.org/SDRplay.zip` we use, extracted into
   `C:/Program Files/`.
6. `codec2` built via **MinGW64 + `-DCMAKE_GNUtoMS=ON`** (msys2 mingw64
   shell), because `cl.exe` + codec2 was unhappy. (We solved the same
   problem differently: route codec2 through VS-bundled `clang-cl`, which
   accepts the C99 VLAs and emits a native MSVC-ABI DLL. No MinGW, no
   GNUtoMS, no cross-architecture x64 island in our ARM64 bundles.)
7. `vcpkg install fftw3 glfw3 portaudio zstd libusb itpp spdlog` for
   math/IO.
8. From-source builds for what PothosSDR doesn't ship: `rtaudio`
   (commit-pinned), Rouma forks of `libperseus-sdr` / `librfnm` / `libfobos`,
   `hydrasdr/hydrasdr-host`. Most of these install **into the PothosSDR prefix**
   so the consumer sees one merged kit.

### Useful learnings

1. **NSIS-installer-via-7z trick.** `7z x installer.exe -o<dir>` extracts
   NSIS installers without running them — no interactive UI, no registry
   side-effects, no admin rights. Useful for any future vendor SDK that
   ships only as an NSIS exe (`htra_api`, SDRplay's official installer if
   we move off the sdrpp.org bundle, Cypress FX3 SDK).
2. **PothosSDR binary as a fast-track CI base.** A second deps preset
   (`pothossdr-prebuilt`) could `URL`-pull
   `PothosSDR-2020.01.26-vc14-x64.exe`, extract, and surgically replace
   files — skipping ~25 from-source builds. Useful for quick CI smoke
   tests; **not** suitable for production (vc14 ABI mismatch with modern
   MSVC, frozen 2020 versions, no Debug, no ARM64). Worth a dedicated doc
   section / preset, not a primary build path.
3. **Confirms our pinning instincts.** PothosSDR's 2020 `libusb` is "too
   new" for some consumers and 2020 `rtlsdr` is "too old" — both confirm
   Tier A1: silently floating versions break things and must be pinned.
   The fact that Brown has to ship *two* patches against PothosSDR is
   exactly why a from-source deps system is the right call.
4. **codec2 still uses MinGW + GNUtoMS in the wild** (PothosSDR / Brown).
   We bypass this entirely by building codec2 with `clang-cl` from the VS
   installer — same source, same target ABI as the rest of the MSVC build,
   no MinGW dependency on the host. The trick is the `TOOLSET ClangCL`
   override threaded through `add_cmake_project` in
   `deps/+codec2/codec2.cmake`.
5. **librtlsdr osmocom 2024-06-23 binary** is a known-good prebuilt
   source. If our Rouma-fork-from-source build of `librtlsdr` ever has
   issues, we can swap to that prebuilt with one URL change.
6. **Single unified prefix.** Brown intentionally cross-installs
   everything into `C:/Program Files/PothosSDR/` to make consumers see one
   merged kit — confirming the value of our `destdir/usr/local`
   single-prefix model. They're reverse-engineering it on top of
   PothosSDR.

### Don't copy

- **vcpkg dependency.** Introduces a parallel package manager and
  `vcpkg.cmake` toolchain plumbing. We explicitly avoid this.
- **Mixed prefix layout** (rfnm under default vcpkg prefix, hydrasdr
  under `C:/Program Files/hydrasdr-host`). Fragile — requires
  `make_windows_package.ps1` to chase DLLs across many trees. Our
  single-prefix model is better.
- **Patch by file overwrite.** Fine for one-off DLL swaps, brittle for
  source patches. Our CMake-driven `PATCH_COMMAND` (e.g.,
  `+libbladeRF/patch_libbladerf.cmake`) is more rigorous.
- **`make_windows_package.ps1` post-build copy.** Hard-codes per-package
  DLL paths. Our `CMAKE_PREFIX_PATH/bin → $<TARGET_FILE_DIR:sdrpp>`
  blanket copy in the root `CMakeLists.txt` is much cleaner.

---

## Suggested execution order

Smallest blast radius first:

1. **Tier A1** — pin all floating git refs to SHAs or release tags.
2. **Tier A3** — add `UPDATE_DISCONNECTED` and a `deps_update` target.
3. **Tier B6** — SDRplay registry fallback (Windows-specific block in
   `+sdrplay/sdrplay.cmake`).
4. **Tier B4** — post-install relocation for `hackRF`/`bladeRF`, but only
   after a first verified build confirms the layout is broken. Don't add
   speculatively.
5. **Tier B5** — `+soapysdr/soapysdr.cmake` if `OPT_BUILD_SOAPY_SOURCE` is
   actually used.
6. **Tier A2** — fill `SHA256` hashes opportunistically after each first
   clean build.
7. **Tier C** — license aggregation, additional presets, `codec2` pin.

Each tier is independent.
