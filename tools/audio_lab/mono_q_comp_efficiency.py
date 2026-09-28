#!/usr/bin/env python3
"""Report Q-comp sine-fundamental response versus the input excitation factor.

Usage: mono_q_comp_efficiency.py matrix.csv output.csv output.svg
This is an output-component diagnostic, NOT a transfer-function measurement.
"""

import csv
import math
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 64
    rows = list(csv.DictReader(pathlib.Path(sys.argv[1]).open(newline="")))
    paired = {}
    for row in rows:
        if row["input"] != "sine" or float(row["cutoff_hz"]) != 500.0 or float(row["resonance"]) != 0.8:
            continue
        paired.setdefault(float(row["drive_db"]), {})[int(row["q_comp"])] = float(row["fundamental_peak"])
    drives = (0.0, 6.0, 12.0, 18.0, 24.0)
    q = 0.8
    k = 4.0 * q  # Below the top-end self-oscillation onset.
    c = 0.20 * q
    predicted = 20.0 * math.log10(1.0 + k * c)
    data = []
    for drive in drives:
        if drive not in paired or set(paired[drive]) != {0, 1} or min(paired[drive].values()) <= 0:
            raise ValueError(f"Missing or invalid sine On/Off pair at {drive:g} dB Drive")
        measured = 20.0 * math.log10(paired[drive][1] / paired[drive][0])
        data.append((drive, predicted, measured, measured / predicted))
    with pathlib.Path(sys.argv[2]).open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(("drive_db", "cutoff_hz", "resonance", "input_excitation_db", "measured_fundamental_change_db", "efficiency"))
        for drive, target, measured, efficiency in data:
            writer.writerow((f"{drive:g}", "500", "0.8", f"{target:.9f}", f"{measured:.9f}", f"{efficiency:.9f}"))

    # Dependency-free, deterministic SVG; zero and unity are visible anchors.
    x = lambda drive: 70 + drive * 20
    y = lambda efficiency: 230 - efficiency * 160
    points = " ".join(f"{x(drive):.1f},{y(efficiency):.1f}" for drive, _, _, efficiency in data)
    labels = "\n".join(
        f'<text x="{x(drive):.1f}" y="255" text-anchor="middle">{drive:g}</text>'
        for drive in drives
    )
    circles = "\n".join(
        f'<circle cx="{x(drive):.1f}" cy="{y(efficiency):.1f}" r="4" fill="#2255aa"/>'
        for drive, _, _, efficiency in data
    )
    pathlib.Path(sys.argv[3]).write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="620" height="310" viewBox="0 0 620 310">\n'
        '<rect width="620" height="310" fill="white"/>\n'
        '<text x="70" y="25">Mono Q Comp: sine fundamental / input excitation</text>\n'
        '<path d="M70 70 V230 H550" fill="none" stroke="black"/>\n'
        '<path d="M70 70 H550" stroke="#aaa" stroke-dasharray="4 4"/>\n'
        '<text x="55" y="75" text-anchor="end">1</text><text x="55" y="235" text-anchor="end">0</text>\n'
        f'<polyline points="{points}" fill="none" stroke="#2255aa" stroke-width="2"/>\n'
        f'{circles}\n{labels}\n'
        '<text x="310" y="282" text-anchor="middle">Drive (dB)</text>\n'
        '<text x="70" y="301">48 kHz; cutoff 500 Hz; Q 0.8; oscillator pitch quantized to MIDI</text>\n'
        '</svg>\n'
    )
    for drive, _, measured, efficiency in data:
        print(f"drive={drive:g} dB, fundamental change={measured:.3f} dB, efficiency={efficiency:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())