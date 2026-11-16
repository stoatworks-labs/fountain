"""Render every parameter at both ends of its range and fail if any made no
difference -- for both plugins. (ferrofluid's script; the table is fountain's.)

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. Every render is 180 frames (3 s at
60 fps) at 120 BPM, 320x180 (CI's raster). Most controls act at the defaults
(the Show layout, shooters on the beat). Sensitivity needs music and an onset
to judge (`--kicks`, the harness's kick every half second over a hi-hat with a ghost
note between that only a high Sensitivity hears, with Fire On=Onset). Lead and Sensitivity move or add a jet, which the Over shows
only where it is lit -- against the mist at full Screen it is a sliver -- so
they are swept with the mist bar off. Fire is a
press (`--at`, half a second before the frame read), swept with Fire On=Onset
and no music so the press is the only shot. An option parameter reads back 0..1 whatever its
count (the fleet's trap), so options are set here by name. A control whose ends
differ by less than `--floor` (mean 8-bit difference per channel) is barely
alive: vectrix's lesson.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context (keeping only the base), the
addendum's honesty check: what goes dead there is the list of controls that
only act with another one set, which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

BASE = []

MUSIC = [["--kicks"]]

# name -> (low settings, high settings, context settings)
SWEEP = {
    "Layout": (["Layout=Show"], ["Layout=Screen"], []),
    "Pressure": (["Pressure=0.3"], ["Pressure=0.8"], []),
    "Response": (["Response=0"], ["Response=1"], []),
    "Fire On": (["Fire On=Steady"], ["Fire On=Beat"], []),
    "Fire": ([], [["--at", "150", "Fire=1"]], ["Fire On=Onset"]),
    "Shot": (["Shot=0"], ["Shot=1"], []),
    "Lead": (["Lead=0"], ["Lead=1"], ["Screen=0", "Layout=Row"]),
    "Sensitivity": (["Sensitivity=0"], ["Sensitivity=1"], MUSIC + ["Fire On=Onset", "Screen=0", "Layout=Row", "Response=0"]),
    "Nozzle": (["Nozzle=0.2"], ["Nozzle=0.8"], []),
    "Breakup": (["Breakup=0"], ["Breakup=1"], []),
    "Valve": (["Valve=0"], ["Valve=1"], []),
    "Pump": (["Pump=0"], ["Pump=1"], []),
    "Wind": (["Wind=0.3"], ["Wind=0.7"], []),
    "Gust": (["Gust=0"], ["Gust=1"], []),
    "Screen": (["Screen=0"], ["Screen=1"], []),
    "View": (["View=0.3"], ["View=0.7"], []),
    "Lamps": (["Lamps=White"], ["Lamps=Duo"], []),
    "Exposure": (["Exposure=0.3"], ["Exposure=0.7"], []),
    "Mix": (["Mix=0"], ["Mix=1"], []),
}

# The About block: a text line and browser buttons, with no pixel to sweep.
# Audio is the host's FFT buffer: --kicks writes it, and Sensitivity is swept
# through it.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide", "Audio"}


def read_png(path):
    """Enough of PNG for fttest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(fttest, out, settings, over):
    args = [str(fttest), "--out", str(out), "--size", "320x180", "--frames", "180"] + (["--over"] if over else [])
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"fttest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(fttest, over):
    result = subprocess.run([str(fttest), "--list"] + (["--over"] if over else []), capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"fttest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(fttest, scratch, name, bare, over):
    low, high, context = SWEEP[name]
    context = BASE + ([] if bare else context)
    tag = f"p{abs(hash(name))}{'o' if over else 's'}"
    before = render(fttest, scratch / f"{tag}-a.png", context + low, over)
    after = render(fttest, scratch / f"{tag}-b.png", context + high, over)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    fttest = build / "fttest"
    if not fttest.exists():
        print(f"{fttest} not found", file=sys.stderr)
        return 1

    failed = False
    for over in (False, True):
        plugin = "SW Fountain Over" if over else "SW Fountain"
        declared = parameters(fttest, over)
        unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
        if unknown:
            # A new parameter with no sweep is a hole, not a pass.
            print(f"{plugin}: no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
            return 1
        dead, weak = [], []
        with tempfile.TemporaryDirectory() as scratch:
            scratch = pathlib.Path(scratch)
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                names = [n for n in declared if n in SWEEP]
                results = list(pool.map(lambda n: sweep_one(fttest, scratch, n, args.bare, over), names))
        for name, delta in results:
            if delta == 0.0:
                dead.append(name)
                print(f"  DEAD {plugin}: {name:16s} both ends identical")
            elif delta < args.floor:
                weak.append(name)
                print(f"  WEAK {plugin}: {name:16s} mean delta {delta:.4f}")
            elif args.verbose:
                print(f"  ok   {plugin}: {name:16s} mean delta {delta:.3f}")
        print(f"{plugin}: {len(results)} parameters swept{' (bare: base context only)' if args.bare else ''}, "
              f"{len(dead)} dead, {len(weak)} barely alive")
        failed = failed or bool(dead or weak)
    if failed:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
