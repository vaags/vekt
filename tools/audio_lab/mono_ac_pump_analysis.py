#!/usr/bin/env python3
"""Analyze resolved harmonic-transfer bins from VektMonoAcPumpResponse.

Usage: mono_ac_pump_analysis.py input.csv output-directory
Produces points.csv, summary.csv, and four SVGs per resonance. P_conv uses
ONLY the measured +/-2 pump sidebands; it is not total incremental energy.
"""

import csv
import math
import pathlib
import sys


ORDERS = {-2, -1, 0, 1, 2}
DRIVES = (0, 6, 12, 18, 24)


def load(path):
    bins = {}
    metadata = set()
    with path.open(newline="") as source:
        for row in csv.DictReader(source):
            resonance = float(row["resonance"])
            drive = int(float(row["drive_db"]))
            enabled = int(row["q_comp"])
            frequency = int(float(row["probe_hz"]))
            order = int(row["sideband_order"])
            key = (resonance, drive, enabled, frequency)
            if order not in ORDERS or order in bins.setdefault(key, {}):
                raise ValueError(f"Duplicate or invalid sideband: {key}, {order}")
            if not all(int(row[name]) == 0 for name in (
                "baseline_unconverged", "probe_unconverged", "baseline_nonfinite", "probe_nonfinite"
            )):
                raise ValueError(f"Nonzero solver diagnostics: {key}")
            metadata.add((float(row["pump_hz"]), float(row["pump_amplitude"]),
                          float(row["probe_amplitude"])))
            if not math.isclose(float(row["component_hz"]), frequency + order * float(row["pump_hz"])):
                raise ValueError(f"Component frequency mismatch: {key}")
            gain = float(row["gain_db"])
            if not math.isfinite(gain):
                raise ValueError(f"Nonfinite gain: {key}")
            bins[key][order] = 10 ** (gain / 20)
    if not bins or len(metadata) != 1:
        raise ValueError("Missing bins or inconsistent stimulus")
    if any(set(components) != ORDERS for components in bins.values()):
        raise ValueError("Incomplete five-component measurement")
    groups = {}
    for (resonance, drive, enabled, frequency), components in bins.items():
        if enabled not in (0, 1) or drive not in DRIVES or resonance >= 1:
            raise ValueError("Unexpected settings")
        groups.setdefault((resonance, drive, frequency), {})[enabled] = components
    if any(set(pair) != {0, 1} for pair in groups.values()):
        raise ValueError("Missing Q Comp On/Off pair")
    resonances = {key[0] for key in groups}
    if len(resonances) != 3 or not all(
        any(math.isclose(actual, expected, abs_tol=1e-6) for actual in resonances)
        for expected in (0.5, 0.8, 0.95)
    ):
        raise ValueError("Unexpected resonance grid")
    for resonance in resonances:
        for drive in DRIVES:
            frequencies = {frequency for q, d, frequency in groups if q == resonance and d == drive}
            if frequencies != set(range(400, 1401, 25)):
                raise ValueError(f"Incomplete frequency grid: {resonance}, {drive}")
    return groups, metadata.pop()


def db(value):
    return 20 * math.log10(value) if value > 0 else float("-inf")


def records(groups):
    points = []
    for (resonance, drive, frequency), pair in sorted(groups.items()):
        off, on = pair[0], pair[1]
        def metrics(bins):
            direct = bins[0]
            conversion = math.hypot(bins[-2], bins[2])
            return db(direct), db(conversion), db(conversion / direct)
        a, b = metrics(off), metrics(on)
        points.append((resonance, drive, frequency, *a, *b,
                       b[0] - a[0], b[1] - a[1], b[2] - a[2]))
    return points


def summaries(points):
    summary = []
    for resonance in sorted({row[0] for row in points}):
        for drive in DRIVES:
            subset = [row for row in points if row[:2] == (resonance, drive)]
            if not subset:
                raise ValueError(f"Missing drive: {resonance}, {drive}")
            fixed = next((row for row in subset if row[2] == 900), None)
            if fixed is None:
                raise ValueError("900 Hz reference not measured")
            peak_off = max(subset, key=lambda row: row[3])
            peak_on = max(subset, key=lambda row: row[6])
            summary.append((resonance, drive, peak_off[2], peak_off[3], peak_on[2], peak_on[6],
                            fixed[9], fixed[10], fixed[11], fixed[3], fixed[6], fixed[4], fixed[7]))
    return summary


def write_csv(path, header, rows):
    with path.open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(header)
        writer.writerows(rows)


def plot(path, resonance, points, field, title):
    # Five vertically separated drive panels; On/Off for levels, one delta
    # line for differences. Each panel has its own labeled dB range.
    is_delta = field >= 9
    width, height = 750, 1000
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="white"/>',
             f'<text x="50" y="26" font-size="18">Q {resonance:g}: {title}</text>',
             '<text x="50" y="45" font-size="11">173 Hz sine pump; 0.1 amplitude; 48 kHz; 1e-4 probe. ±2 only, not total conversion.</text>']
    for panel, drive in enumerate(DRIVES):
        rows = [row for row in points if row[:2] == (resonance, drive)]
        if not rows:
            raise ValueError(f"Missing plot panel {resonance}, {drive}")
        y0 = 70 + panel * 180
        series = [("On − Off", field, "#6842a6")] if is_delta else [
            ("Off", field, "#bd492e"), ("On", field + 3, "#2255aa")]
        values = [row[index] for _, index, _ in series for row in rows]
        low = math.floor(min(values) / 5) * 5 - 1
        high = math.ceil(max(values) / 5) * 5 + 1
        if high <= low:
            high = low + 2
        parts.extend((f'<text x="50" y="{y0 + 12}" font-size="13">Drive +{drive} dB</text>',
                      f'<path d="M80 {y0 + 20} V{y0 + 150} H700" fill="none" stroke="#888"/>',
                      f'<text x="75" y="{y0 + 30}" text-anchor="end" font-size="10">{high:g}</text>',
                      f'<text x="75" y="{y0 + 151}" text-anchor="end" font-size="10">{low:g}</text>'))
        for idx, (label, column, color) in enumerate(series):
            coordinates = " ".join(f"{80 + (row[2] - 400) * .62:.1f},{y0 + 150 - 130 * (row[column] - low) / (high - low):.1f}" for row in rows)
            parts.append(f'<polyline points="{coordinates}" fill="none" stroke="{color}" stroke-width="2"/>')
            parts.append(f'<text x="{530 + idx * 85}" y="{y0 + 12}" font-size="11" fill="{color}">{label}</text>')
    parts.extend(('<text x="80" y="980" font-size="11">400 Hz</text>',
                  '<text x="650" y="980" font-size="11">1400 Hz</text>', '</svg>'))
    path.write_text("\n".join(parts) + "\n")


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 64
    groups, stimulus = load(pathlib.Path(sys.argv[1]))
    if not (math.isclose(stimulus[0], 173, abs_tol=1e-8)
            and math.isclose(stimulus[1], 0.1, rel_tol=1e-6)
            and math.isclose(stimulus[2], 0.0001, rel_tol=1e-6)):
        raise ValueError(f"Unexpected pump/probe: {stimulus}")
    output = pathlib.Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)
    points = records(groups)
    summary = summaries(points)
    write_csv(output / "points.csv", ("resonance", "drive_db", "probe_hz", "h0_off_db", "conversion_off_db",
              "ratio_off_db", "h0_on_db", "conversion_on_db", "ratio_on_db", "h0_delta_db",
              "conversion_delta_db", "ratio_delta_db"), points)
    write_csv(output / "summary.csv", ("resonance", "drive_db", "peak_off_hz", "peak_off_db", "peak_on_hz",
              "peak_on_db", "at900_h0_delta_db", "at900_conversion_delta_db", "at900_ratio_delta_db",
              "at900_h0_off_db", "at900_h0_on_db", "at900_conversion_off_db", "at900_conversion_on_db"), summary)
    for resonance in sorted({row[0] for row in points}):
        for name, column, title in (("direct", 3, "Direct |H0| (dB)"),
                                    ("direct_delta", 9, "Direct On − Off (dB)"),
                                    ("conversion", 4, "Resolved conversion sqrt(Pconv) (dB)"),
                                    ("conversion_ratio", 5, "Resolved conversion / direct (dB)")):
            plot(output / f"q{resonance:g}_{name}.svg", resonance, points, column, title)
    print(f"pairs={len(points)} summaries={len(summary)} output={output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())