# Frequency Info (freq_info)

Shows, live on the waterfall and in a side panel, which station(s) should be
on a given frequency right now — based on imported schedule databases, not
a live signal decoder. Inspired by the **ListenInfo** plugin for SDR#.

## Data sources

The plugin imports two independent databases. Neither is downloaded
automatically — you fetch the file yourself and point the plugin at it.

### EiBi

The standard, actively maintained shortwave broadcast/utility schedule
database. Get the current season's file from
[eibispace.de](http://eibispace.de/dx/) — look for `sked-Xzz.csv` (e.g.
`sked-a26.csv` for the A26/summer 2026 season; the letter changes each
season, roughly twice a year).

- Format: semicolon-separated CSV, one broadcast per line.
- Encoding: the file is Windows-1252/Latin-1, **not** UTF-8. The plugin
  converts it automatically — station names with accented characters
  (Rádio, Córdoba, Valparaíso, ...) should display correctly.
- Coverage: EiBi describes itself as "the comprehensive **shortwave**
  broadcasting schedule." Its medium/long wave coverage is real but
  sparse — it's not what the database is built for, and results on those
  bands will reflect that.

### Aoki

A Japan-based shortwave/mediumwave list ("A26 Shortwave Frequency List"
style), fixed-width plain text rather than CSV. Has noticeably better
medium/long wave coverage than EiBi, especially for Asia-Pacific, and
uniquely carries **transmitter coordinates** for most entries — EiBi
generally doesn't.

- Format: fixed-width text columns (not delimited) — frequency, station,
  UTC time, days, language, power, azimuth, location, country, lat/lon,
  remarks. The plugin measures column positions against the file's own
  header, not a hardcoded assumption.
- Day-of-week convention: **1 = Sunday** in this file (EiBi uses 1 =
  Monday) — handled internally, nothing to configure.
- Malformed or unrecognized lines (including the title/header rows) are
  silently skipped and counted rather than aborting the import; a very
  high skip count after an import is worth investigating.

## Importing data

In the panel:

1. **Import source** — pick `EiBi` or `Aoki` from the dropdown.
2. **File path** — the field always shows/edits the path for whichever
   source is currently selected above; switching the dropdown doesn't
   lose what you typed for the other source.
3. **Load / Reimport** — imports the selected source's file.

The two sources are loaded and stored independently — reimporting one
never clears data already loaded from the other. If a load produces zero
entries, an error is shown directly in the panel (there's no reliance on
logcat/console output).

## Panel controls

| Control | What it does |
| --- | --- |
| Show markers on waterfall | Toggles drawing labels on the spectrum entirely. The panel/window keep working either way. |
| Show only tuned marker | When on, only the currently-tuned station(s) are drawn on the waterfall — everything else is hidden there (still listed in the tables). |
| Match tolerance (Hz) | How close a database frequency has to be to your VFO to count as "tuned." Needs to be at least half your tuning step, or you'll tune right past entries without ever matching them. |
| Max rows on waterfall | How many stacked label rows the waterfall will draw before showing "+N more (see panel)" instead of more labels. Lower on a phone, higher on desktop — it's per-install config, so each device can have its own value. |
| Top offset (px) | Shifts all waterfall labels down by this many pixels — use it if Band Plan (when positioned at the top of the spectrum) overlaps the labels. |
| Preferred target area | Free-text match (case-insensitive substring) against EiBi's "Target" field (e.g. `Eu`). Used to rank candidates when several stations share a frequency and no distance ranking applies. |
| My location (lat, lon) | Your own coordinates, for distance-based ranking (see below). Leave at 0, 0 to disable it entirely. |
| EiBi / Aoki checkboxes | Independent visibility toggle per source — applies everywhere (waterfall, both tables). Both off shows nothing. |

## Colors and interaction

- **Tuned/selected entries** are drawn in full color: green for EiBi,
  light blue for Aoki. Everything else is a dim, translucent gray — enough
  to read, not competing for attention.
- **Tap/click** a waterfall label or a table row tunes immediately.
- **Hold/hover for about a second** to see the full detail popup (name,
  frequency, target, country, language, schedule, transmitter site and
  power when known, and which source the entry came from). This is
  deliberately delayed so a quick tap-to-tune doesn't also pop up a window
  every time — the same delay applies whether you're using a mouse or a
  touchscreen, since a sustained touch and a sustained hover are handled
  identically by the underlying UI toolkit.
- Clicking a specific row in a table selects **that one entry**, even if
  several stations share the same frequency — the waterfall's own
  co-channel highlighting is unaffected by this.

## Ranking, when several stations share a frequency

Three tiers, in order:

1. **Distance from your location** (the "My location" field above) —
   only when the entry has known coordinates (mainly Aoki) *and* its
   frequency is below 5 MHz. Above that, real shortwave-skip propagation
   makes raw distance an unreliable predictor of what you'll actually
   hear, so this tier is skipped there on purpose.
2. **Target area match** (mostly EiBi, which doesn't carry coordinates).
3. Frequency/insertion order otherwise.

## Browse window

The "Browse entries..." button opens a separate, non-modal window (same
behavior as the FT8/FT4/WSPR decoder's results window in this fork —
collapsible via the title bar, doesn't darken the rest of the UI or
interfere with tuning while open).

- **Show all records** — queries the entire database instead of just
  what's currently visible on the waterfall. Off by default every time
  the window is opened, on any device, every session.
- **Search** — filters by station name, case-insensitive, works against
  whichever scope (visible/all) is currently selected.

## Known limitations

- **Medium/long wave coverage is inherently limited.** Neither EiBi nor
  Aoki is a comprehensive MW/LW database — no such free, static, bulk-
  downloadable database exists as of writing (MWList/AMLIST were
  investigated and ruled out: no reliable bulk export, and fmscan.org's
  own exports turned out to be location/time-specific signal predictions,
  not a static schedule — a different kind of data than this plugin is
  built around). Shortwave is comprehensive; medium wave is best-effort.
- **No built-in downloader.** You fetch each source file yourself and
  point the plugin at its path — there's no in-app fetching.
- **Distance ranking needs "My location" set.** It's 0, 0 (disabled) by
  default.
