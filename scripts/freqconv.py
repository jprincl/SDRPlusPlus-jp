#!/usr/bin/env python3
"""
Converts frequency bookmarks between SDR++ (JSON) and SDR# (frequencies.xml).

Both directions. The direction is detected from the input file unless it is
forced with --to-sdrpp / --to-sdrsharp.

    freqconv.py frequencies.xml -o bookmarks.json
    freqconv.py bookmarks.json  -o frequencies.xml

Standard library only - no pip install needed.

Field mapping
-------------
    SDR++                       SDR#
    ---------------------------------------------
    bookmark name (key)   <->   Name
    list name             <->   GroupName
    frequency (Hz)        <->   Frequency (Hz, integer)
    bandwidth (Hz)        <->   FilterBandwidth (Hz, integer)
    mode (index 0-7)      <->   DetectorType (name)
    notes                 <->   Comment

Field names were taken from a real SDR# frequencies.xml, not from the
published schema - the schema omits IsScanned, CenterFrequency and Comment,
all of which SDR# does write.

Both sides carry fields the other has no place for, so a conversion is
lossy in either direction. Nothing is silently dropped - the summary at the
end says exactly what was lost. Converting A -> B -> A therefore does not
give back the original file.

    lost going to SDR#:   geoinfo, scheduling (start/end time, days)
    lost going to SDR++:  IsScanned, IsFavourite, Shift, CenterFrequency
"""

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from collections import OrderedDict

# Same eight names, same meaning, in both programs - the index is just how
# SDR++ stores it internally (see demodModeList in bookmark.cpp).
MODES = ["NFM", "WFM", "AM", "DSB", "USB", "CW", "LSB", "RAW"]
MODE_TO_INDEX = {m: i for i, m in enumerate(MODES)}

DEFAULT_GROUP = "General"


class ConversionReport:
    """Collects what happened, so the user is told rather than guessing."""

    def __init__(self):
        self.entries = 0
        self.groups = set()
        self.skipped = []      # (name, reason)
        self.renamed = 0       # entries given a frequency suffix
        self.lost_fields = {}  # field name -> count of entries that had it

    def note_loss(self, field):
        self.lost_fields[field] = self.lost_fields.get(field, 0) + 1

    def print_summary(self, out=sys.stderr):
        print(f"Converted {self.entries} bookmark(s) in "
              f"{len(self.groups)} group(s).", file=out)
        if self.groups:
            print("  Groups: " + ", ".join(sorted(self.groups)), file=out)
        if self.renamed:
            print(f"  {self.renamed} entry/entries shared a name within their "
                  "group and got the frequency appended.", file=out)
        if self.lost_fields:
            print("  Dropped (no equivalent in the target format):", file=out)
            for field, count in sorted(self.lost_fields.items()):
                print(f"    {field}: {count} entry/entries", file=out)
        if self.skipped:
            print(f"  Skipped {len(self.skipped)} unusable entry/entries:",
                  file=out)
            for name, reason in self.skipped[:10]:
                print(f"    {name or '<unnamed>'}: {reason}", file=out)
            if len(self.skipped) > 10:
                print(f"    ... and {len(self.skipped) - 10} more", file=out)


# --------------------------------------------------------------------------
# reading
# --------------------------------------------------------------------------

def read_sdrsharp(path, report):
    """frequencies.xml -> {group: {name: bookmark}}"""
    tree = ET.parse(path)
    root = tree.getroot()
    # Strip any namespace, so files written with xmlns:xsi still match.
    tag = root.tag.split("}")[-1]
    if tag != "ArrayOfMemoryEntry":
        raise ValueError(f"expected <ArrayOfMemoryEntry> as root, got <{tag}>")

    raw = []
    for entry in root.findall("MemoryEntry"):
        def text(name, default=""):
            el = entry.find(name)
            return el.text.strip() if (el is not None and el.text) else default

        name = text("Name")
        raw_freq = text("Frequency")
        if not raw_freq:
            report.skipped.append((name, "no Frequency"))
            continue
        try:
            frequency = float(raw_freq)
        except ValueError:
            report.skipped.append((name, f"unparsable Frequency {raw_freq!r}"))
            continue

        detector = text("DetectorType", "NFM").upper()
        if detector not in MODE_TO_INDEX:
            report.skipped.append((name, f"unknown DetectorType {detector!r}"))
            continue

        try:
            bandwidth = float(text("FilterBandwidth", "0") or 0)
        except ValueError:
            bandwidth = 0.0

        # Fields SDR++ has nowhere to put.
        for field in ("IsScanned", "IsFavourite"):
            if text(field).lower() == "true":
                report.note_loss(field)
        for field in ("Shift", "CenterFrequency"):
            if text(field, "0") not in ("", "0"):
                report.note_loss(field)

        group = text("GroupName") or DEFAULT_GROUP
        # An unnamed entry would become an unusable empty JSON key.
        if not name:
            name = format_freq(frequency)

        raw.append({
            "group": group,
            "name": name,
            "frequency": frequency,
            "bandwidth": bandwidth,
            "mode": MODE_TO_INDEX[detector],
            "notes": text("Comment"),
        })

    # SDR# happily holds the same name many times in one group (a station on
    # several frequencies), but SDR++ keys bookmarks by name, so those must
    # be made distinct. Appending the frequency is far more use than a
    # running number - done for every copy, so they stay consistent with
    # each other rather than one plain name plus numbered leftovers.
    counts = {}
    for r in raw:
        key = (r["group"], r["name"])
        counts[key] = counts.get(key, 0) + 1

    groups = OrderedDict()
    renamed = 0
    for r in raw:
        name = r["name"]
        if counts[(r["group"], name)] > 1:
            name = f"{name} ({format_freq(r['frequency'])})"
            renamed += 1
        bookmarks = groups.setdefault(r["group"], OrderedDict())
        name = unique_name(name, bookmarks)
        bookmarks[name] = {
            "frequency": r["frequency"],
            "bandwidth": r["bandwidth"],
            "mode": r["mode"],
            "startTime": 0,
            "endTime": 0,
            "days": [True] * 7,
            "geoinfo": "",
            "notes": r["notes"],
        }
        report.entries += 1
        report.groups.add(r["group"])
    report.renamed = renamed

    return groups


def read_sdrpp(path, report):
    """SDR++ bookmark JSON -> {group: {name: bookmark}}

    Accepts both shapes: the multi-list export ({"lists": ...}) and the
    plain single-list one ({"bookmarks": ...}).
    """
    with open(path, "r", encoding="utf-8") as fh:
        data = json.load(fh)

    groups = OrderedDict()

    def take(group, name, bm):
        if not isinstance(bm, dict) or "frequency" not in bm:
            report.skipped.append((name, "missing frequency"))
            return
        bookmarks = groups.setdefault(group, OrderedDict())
        bookmarks[name] = bm
        report.entries += 1
        report.groups.add(group)

    if isinstance(data.get("lists"), dict):
        for group, obj in data["lists"].items():
            for name, bm in (obj or {}).get("bookmarks", {}).items():
                take(group, name, bm)
    elif isinstance(data.get("bookmarks"), dict):
        for name, bm in data["bookmarks"].items():
            take(DEFAULT_GROUP, name, bm)
    else:
        raise ValueError('no "lists" or "bookmarks" object in the JSON')

    return groups


def format_freq(hz):
    """Short human-readable frequency, used to tell same-named entries apart."""
    hz = float(hz)
    if hz >= 1e9:
        return f"{hz / 1e9:g} GHz"
    if hz >= 1e6:
        return f"{hz / 1e6:g} MHz"
    if hz >= 1e3:
        return f"{hz / 1e3:g} kHz"
    return f"{hz:g} Hz"


def unique_name(name, existing):
    """Last-resort disambiguation when even the frequency suffix collides."""
    if name not in existing:
        return name
    n = 2
    while f"{name} #{n}" in existing:
        n += 1
    return f"{name} #{n}"


# --------------------------------------------------------------------------
# writing
# --------------------------------------------------------------------------

def write_sdrpp(groups, path, report):
    """Writes the multi-list shape, plus a flattened copy under "bookmarks".

    The flat copy is what stock SDR++ (and other forks) understand; builds
    that know about lists read the structured one and keep the grouping.
    """
    out = OrderedDict()
    out["lists"] = OrderedDict()
    flat = OrderedDict()
    for group, bookmarks in groups.items():
        out["lists"][group] = {"bookmarks": bookmarks}
        for name, bm in bookmarks.items():
            if name not in flat:
                flat[name] = bm
    out["bookmarks"] = flat

    with open(path, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2, ensure_ascii=False)
        fh.write("\n")


def write_sdrsharp(groups, path, report):
    root = ET.Element("ArrayOfMemoryEntry")

    # SDR# keeps its own file sorted by frequency, with groups interleaved -
    # a real 700-entry file was in ascending frequency order throughout. So
    # emit the same order rather than clustering by group, which keeps a
    # round trip close to the original instead of reshuffling everything.
    flat = []
    for group, bookmarks in groups.items():
        for name, bm in bookmarks.items():
            try:
                freq = float(bm["frequency"])
            except (KeyError, TypeError, ValueError):
                report.skipped.append((name, "missing or unparsable frequency"))
                continue
            flat.append((freq, group, name, bm))
    flat.sort(key=lambda t: t[0])

    for freq, group, name, bm in flat:
        # Warn about what this format cannot carry.
        if bm.get("geoinfo"):
            report.note_loss("geoinfo")
        if bm.get("startTime") or bm.get("endTime"):
            report.note_loss("schedule (start/end time)")
        days = bm.get("days")
        if isinstance(days, list) and not all(days):
            report.note_loss("schedule (days)")

        try:
            detector = MODES[int(bm.get("mode", 0))]
        except (ValueError, TypeError, IndexError):
            detector = "NFM"

        try:
            bandwidth = int(round(float(bm.get("bandwidth", 0) or 0)))
        except (TypeError, ValueError):
            bandwidth = 0

        entry = ET.SubElement(root, "MemoryEntry")
        # Element order copied from a real SDR#-written file.
        ET.SubElement(entry, "IsScanned").text = "false"
        ET.SubElement(entry, "IsFavourite").text = "false"
        ET.SubElement(entry, "Name").text = name
        ET.SubElement(entry, "GroupName").text = group
        ET.SubElement(entry, "Frequency").text = str(int(round(freq)))
        ET.SubElement(entry, "DetectorType").text = detector
        ET.SubElement(entry, "Shift").text = "0"
        ET.SubElement(entry, "FilterBandwidth").text = str(bandwidth)
        ET.SubElement(entry, "CenterFrequency").text = "0"
        # SDR# only writes Comment when there is one.
        if bm.get("notes"):
            ET.SubElement(entry, "Comment").text = bm["notes"]

        report.entries_out = getattr(report, "entries_out", 0) + 1

    # SDR# declares the namespaces on the root element; mirror that so the
    # output is as close to one of its own files as possible.
    root.set("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance")
    root.set("xmlns:xsd", "http://www.w3.org/2001/XMLSchema")

    indent(root)
    xml = ET.tostring(root, encoding="unicode")
    # SDR# writes CRLF; match it so the file is byte-comparable with its own.
    xml = '<?xml version="1.0" encoding="utf-8"?>\n' + xml + "\n"
    xml = xml.replace("\r\n", "\n").replace("\n", "\r\n")
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(xml)


def indent(elem, level=0):
    """Pretty-print, so the file stays hand-editable like SDR#'s own."""
    pad = "\n" + "  " * level
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = pad + "  "
        for child in elem:
            indent(child, level + 1)
        if not child.tail or not child.tail.strip():
            child.tail = pad
    if level and (not elem.tail or not elem.tail.strip()):
        elem.tail = pad


# --------------------------------------------------------------------------

def detect_format(path):
    with open(path, "rb") as fh:
        head = fh.read(4096).lstrip()
    if head.startswith(b"<"):
        return "sdrsharp"
    if head.startswith(b"{"):
        return "sdrpp"
    raise ValueError(f"cannot tell whether {path} is XML or JSON "
                     "- force it with --to-sdrpp or --to-sdrsharp")


def main():
    ap = argparse.ArgumentParser(
        description="Convert frequency bookmarks between SDR++ and SDR#.")
    ap.add_argument("input", help="frequencies.xml or an SDR++ bookmark .json")
    ap.add_argument("-o", "--output", required=True, help="file to write")
    direction = ap.add_mutually_exclusive_group()
    direction.add_argument("--to-sdrpp", action="store_true",
                           help="force SDR# -> SDR++")
    direction.add_argument("--to-sdrsharp", action="store_true",
                           help="force SDR++ -> SDR#")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="suppress the summary")
    args = ap.parse_args()

    if args.to_sdrpp:
        source = "sdrsharp"
    elif args.to_sdrsharp:
        source = "sdrpp"
    else:
        source = detect_format(args.input)

    report = ConversionReport()
    try:
        if source == "sdrsharp":
            groups = read_sdrsharp(args.input, report)
            write_sdrpp(groups, args.output, report)
        else:
            groups = read_sdrpp(args.input, report)
            write_sdrsharp(groups, args.output, report)
    except (ET.ParseError, json.JSONDecodeError, ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    if not args.quiet:
        arrow = "SDR# -> SDR++" if source == "sdrsharp" else "SDR++ -> SDR#"
        print(f"{arrow}: {args.input} -> {args.output}", file=sys.stderr)
        report.print_summary()
    return 0


if __name__ == "__main__":
    sys.exit(main())
