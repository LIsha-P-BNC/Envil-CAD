#!/usr/bin/env python3
"""Convert a raw company passive-parts CSV into a clean, ASCII, importable list.

Re-runnable as the team's list grows:

    python convert_passive_csv.py "Manufacturer Part Number 2.csv" out.csv

Fixes applied:
  * double-encoded UTF-8 ("mojibake": a UTF-8 file once read as CP1252) is repaired
  * non-ASCII decorations (ohm sign, plus-minus, degree sign) become ASCII
  * comma-only filler rows and rows without an MPN or parseable value are dropped
    (and reported)

Output columns: Value, Package, Tolerance, Manufacturer, MPN, Description, Power,
Composition, Temperature Coefficient, Operating Temperature.  The import dialog's
auto-detection maps all of them without manual adjustment.
"""

import csv
import io
import re
import sys

OUT_HEADER = [
    "Value", "Package", "Tolerance", "Manufacturer", "MPN", "Description",
    "Power", "Composition", "Temperature Coefficient", "Operating Temperature",
]

# Map of output column -> candidate source-header names (normalized: lowercased,
# space/._-# stripped).  First match wins.
SOURCE_HEADERS = {
    "Value": ["resistance", "capacitance", "inductance", "value"],
    "Package": ["package", "case", "casecode"],
    "Tolerance": ["tolerance", "tol"],
    "Manufacturer": ["mfg", "mfr", "manufacturer", "brand"],
    "MPN": ["mfgpn", "mfrpn", "mpn", "manufacturerpartnumber", "mfgpartnumber"],
    "Description": ["description", "desc"],
    "Power": ["power(watts)", "power", "powerwatts"],
    "Composition": ["composition"],
    "Temperature Coefficient": ["temperaturecoefficient", "tempco"],
    "Operating Temperature": ["operatingtemperature", "optemp"],
}

VALUE_RE = re.compile(r"^\s*\d+(\.\d+)?\s*[kKmMuUnNpPgG]?\s*(ohms?|farads?|henr(y|ys|ies)|[FfHh])?\s*$", re.IGNORECASE)


def normalize_header(header: str) -> str:
    return re.sub(r"[ ._\-#]", "", header.lower())


def fix_mojibake(text: str) -> str:
    """Repair UTF-8 that was once decoded as CP1252 and re-encoded."""
    try:
        repaired = text.encode("cp1252").decode("utf-8")
    except (UnicodeEncodeError, UnicodeDecodeError):
        return text

    # Accept the repair only if it strictly reduced the non-ASCII count
    if sum(ord(c) > 127 for c in repaired) < sum(ord(c) > 127 for c in text):
        return repaired

    return text


def asciify(text: str) -> str:
    replacements = {
        "Ω": "ohm",   # Greek omega
        "Ω": "ohm",   # ohm sign
        "±": "",      # plus-minus
        "°": "",      # degree sign
        "µ": "u",     # micro sign
        "μ": "u",     # Greek mu
    }

    for src, dst in replacements.items():
        text = text.replace(src, dst)

    return "".join(c for c in text if ord(c) < 128).strip()


def parses_as_value(cell: str) -> bool:
    return bool(VALUE_RE.match(asciify(cell)))


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    src_path, dst_path = sys.argv[1], sys.argv[2]

    with io.open(src_path, encoding="utf-8", errors="replace", newline="") as f:
        rows = list(csv.reader(f))

    if len(rows) < 2:
        print("ERROR: no data rows")
        return 1

    rows = [[fix_mojibake(cell) for cell in row] for row in rows]

    headers = [normalize_header(h) for h in rows[0]]

    # Resolve each output column to a source index.  For Value, prefer the candidate
    # column whose cells actually parse in the most rows (the team file carries the
    # same resistance twice -- "10k<ohm>" and "10 kOhms").
    col_for = {}

    for out_col, candidates in SOURCE_HEADERS.items():
        matches = [i for i, h in enumerate(headers) for c in candidates if h == c]

        if not matches:
            continue

        if out_col == "Value" and len(matches) > 1:
            # Highest parse rate wins; on a tie prefer the cleanest (least non-ASCII)
            # source column -- a mojibake-damaged twin column loses to its ASCII sibling.
            def value_rank(i):
                parsed = sum(parses_as_value(r[i]) for r in rows[1:] if i < len(r))
                junk = sum(sum(ord(c) > 127 for c in r[i]) for r in rows[1:] if i < len(r))
                return (-parsed, junk)

            matches.sort(key=value_rank)

        col_for[out_col] = matches[0]

    missing = [c for c in ("Value", "MPN") if c not in col_for]

    if missing:
        print(f"ERROR: could not locate source columns for: {', '.join(missing)}")
        return 1

    out_rows = [OUT_HEADER]
    skipped = 0

    for row in rows[1:]:
        def cell(out_col: str) -> str:
            idx = col_for.get(out_col, -1)
            return asciify(row[idx]) if 0 <= idx < len(row) else ""

        value, mpn = cell("Value"), cell("MPN")

        if not value or not mpn or not parses_as_value(value):
            if any(c.strip() for c in row):
                skipped += 1
                print(f"  skipped: value={value!r} mpn={mpn!r}")
            continue

        out_rows.append([cell(c) for c in OUT_HEADER])

    with io.open(dst_path, "w", encoding="ascii", newline="") as f:
        csv.writer(f, lineterminator="\n").writerows(out_rows)

    print(f"{len(out_rows) - 1} parts written to {dst_path}, {skipped} rows skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
