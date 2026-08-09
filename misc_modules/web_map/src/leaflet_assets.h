#pragma once

// Vendored Leaflet 1.9.4 (BSD-2-Clause), embedded as raw string literals so
// the module has zero runtime filesystem dependency -- works identically on
// desktop and inside the Android APK, no "where do static assets live on
// this platform" problem to solve. Regenerate by re-running the fetch
// script if Leaflet needs an update; don't hand-edit.

extern const char* const kLeafletJs;
extern const char* const kLeafletCss;
