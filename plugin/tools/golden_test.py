#!/usr/bin/env python3
"""
golden_test.py — verify the C++ port matches the verified Python reference exactly.

For every (bank, algo) it renders 4096 samples of the 16 kHz post-effects stream from
the Python engine and from the compiled C++ harness (golden_test_cpp), driving both
identically, and asserts the integer sample streams are bit-for-bit equal.

Usage (run from the project root, with the .venv active or via its interpreter):
    clang++ -std=c++20 -O2 -I plugin/Source plugin/tools/golden_test.cpp -o /tmp/golden_test_cpp
    python plugin/tools/golden_test.py /tmp/golden_test_cpp
"""
import subprocess
import sys

import numpy as np

from engine import Engine

N = 4096
FRAMES = 512
P1, P2, BC, RT, VOL = 0.3, 0.7, 1.0, 1.0, 0.8


def python_reference(bank, algo):
    e = Engine()
    e.pot1, e.pot2 = P1, P2
    e.bitcrush_pot, e.rate_pot, e.volume_pot = BC, RT, VOL
    if (bank, algo) != (0, 0):          # mirror C++ setBankAlgo change-detection
        e._pending = (bank, algo)
    out = np.zeros((FRAMES, 1), dtype=np.float32)
    samples = []
    for _ in range(N // FRAMES):
        e._callback(out, FRAMES, None, None)
        samples.extend(int(round(v * 512.0)) for v in out[:, 0].tolist())
    return samples


def main():
    if len(sys.argv) < 2:
        print("usage: golden_test.py <path-to-golden_test_cpp>")
        return 1
    cpp_bin = sys.argv[1]

    # Run the C++ harness and parse its output into {(bank, algo): [ints]}.
    proc = subprocess.run([cpp_bin], capture_output=True, text=True, check=True)
    cpp = {}
    for line in proc.stdout.strip().splitlines():
        parts = line.split()
        b, a = int(parts[0]), int(parts[1])
        cpp[(b, a)] = [int(x) for x in parts[2:]]

    total = 0
    failures = 0
    for b in range(5):
        for a in range(9):
            ref = python_reference(b, a)
            got = cpp.get((b, a))
            total += 1
            if got is None:
                print(f"  MISSING from C++: bank {b} algo {a}")
                failures += 1
                continue
            if got == ref:
                continue
            failures += 1
            # find first mismatch
            first = next((i for i in range(min(len(ref), len(got))) if ref[i] != got[i]), None)
            name = Engine().banks[b][a].name
            print(f"  MISMATCH bank {b} algo {a} ({name}): "
                  f"len py={len(ref)} cpp={len(got)} first diff @ {first} "
                  f"py={ref[first] if first is not None else '-'} "
                  f"cpp={got[first] if first is not None else '-'}")

    print(f"\n{total - failures}/{total} algorithms match exactly.")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
