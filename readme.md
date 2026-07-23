
# SDR++ jp (fork), is not the original bloat-free SDR software

**Spyserver IQ+FFT module:** 
The fork is to support Spyserver IQ+FFT (VFO+FFT) - new module Spyserver VFO+FFT added to support this mode instead of FULL IQ Spyserver mode. 
Please, keep in mind there are still some bugs in this module!!! 

<img width="886" height="1499" alt="Screenshot_2026-07-22-09-52-33-260_org jp sdrpp debug" src="https://github.com/user-attachments/assets/21bffd34-60e6-4563-9922-69dab5a0c921" />

----------------------------------------------------------------------------------

**Frequency manager module:** 
Support export / import frequencies and groups on Android. It is necessary to allow Manage all files permission in Android settings for this application. Exported json (imported json) path: /storage/emulated/0/Download

----------------------------------------------------------------------------------

**Bookmark converter (SDR++ &harr; SDR#)**

`scripts/freqconv.py` converts frequency bookmarks between this fork's
frequency manager (JSON) and SDR#'s frequency manager (`frequencies.xml`),
in both directions. Handy if you have years of frequencies collected in
SDR# and want them on the tablet — or the other way round.

Python 3 only, no dependencies to install.

Usage:

```
# SDR#  ->  SDR++
python3 scripts/freqconv.py frequencies.xml -o bookmarks.json

# SDR++ ->  SDR#
python3 scripts/freqconv.py bookmarks.json -o frequencies.xml
```

The direction is detected from the input file. Force it with `--to-sdrpp`
or `--to-sdrsharp` if the detection ever gets it wrong, and add `-q` to
silence the summary.

SDR# keeps `frequencies.xml` in its own program folder, next to
`SDRSharp.exe`. Back it up before overwriting it.

#### Getting the result into SDR++

On desktop, use Import in the frequency manager and pick the file.

On Android there is no working file picker, so the frequency manager has a
**File** field instead — put the JSON somewhere reachable, e.g.
`/storage/emulated/0/Download/bookmarks.json`, type that path in and press
Import. This needs *All files access* granted to the app in Android
settings.

#### What is carried over

| SDR++ | SDR# |
| --- | --- |
| bookmark name | `Name` |
| list | `GroupName` |
| frequency | `Frequency` |
| bandwidth | `FilterBandwidth` |
| mode | `DetectorType` |
| notes | `Comment` |

Groups become lists and vice versa, so the structure survives the trip.
Demodulator names are identical in both programs (NFM, WFM, AM, DSB, USB,
CW, LSB, RAW), so modes map one to one.

#### What is not

Each program stores things the other has no field for, so a conversion is
lossy in both directions. Nothing is dropped silently — the summary printed
at the end says exactly what was lost and how many entries it affected.

* to SDR#: geo info, scheduling (start/end time, days of week)
* to SDR++: `IsScanned`, `IsFavourite`, `Shift`, `CenterFrequency`

Converting A &rarr; B &rarr; A therefore does not give back a byte-identical
file.

#### Duplicate names

SDR# is happy to store the same name many times in one group — a station
carried on several frequencies, for instance. SDR++ keys bookmarks by
name, so those have to be told apart: when a name occurs more than once
within a group, the frequency is appended to every copy of it.

```
SRR Radio România Actualități  ->  SRR Radio România Actualități (531 kHz)
                                   SRR Radio România Actualități (558 kHz)
```

Entries that cannot be converted at all — no frequency, or a demodulator
this fork does not have — are skipped and listed by name at the end rather
than being quietly lost.

--------------------------------------------------------------------------
**Please do not report bugs in this fork to original author.**

**Report bugs in this fork on this page, in ISSUES.** 

Please see [upstream project page](https://github.com/AlexandreRouma/SDRPlusPlus) for the basic list of its features.

## Thanks / Credits

Thanks and due respect to:
 
* Original author, Alexandre Rouma, for his great [work](https://github.com/AlexandreRouma/SDRPlusPlus). Due credits go to all contributors in the upstream project. 
* Hans Summers G0UPL [QRP Labs QMX Transceiver](https://qrp-labs.com/qmx).
* Lee Salzman for the [enet](https://github.com/lsalzman/enet) UDP networking library used for QMX streaming.

## Feedback

Found an issue? File an [issue](https://github.com/jprincl/SDRPlusPlus-jp/issues).

