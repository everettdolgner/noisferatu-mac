#!/usr/bin/env python3
# Usage: python gen_crackle_header.py ../../vinyl_crackle.npy ../Source/VinylCrackleData.h
# Embeds the firmware-extracted vinyl crackle recording as a C++ int16 array so the
# plugin has no runtime file dependency. Regenerate if vinyl_crackle.npy changes.
import sys, numpy as np

src = sys.argv[1] if len(sys.argv) > 1 else "../../vinyl_crackle.npy"
dst = sys.argv[2] if len(sys.argv) > 2 else "../Source/VinylCrackleData.h"
a = np.load(src).astype(np.int16)

with open(dst, "w") as f:
    f.write("// Auto-generated from vinyl_crackle.npy by tools/gen_crackle_header.py — do not edit.\n")
    f.write("#pragma once\n#include <cstdint>\n#include <cstddef>\n\n")
    f.write("namespace noisferatu {\n\n")
    f.write(f"inline constexpr std::size_t kVinylCrackleLen = {len(a)};\n\n")
    f.write("inline constexpr std::int16_t kVinylCrackle[kVinylCrackleLen] = {\n")
    for i in range(0, len(a), 16):
        chunk = ",".join(str(int(v)) for v in a[i:i+16])
        f.write("    " + chunk + ",\n")
    f.write("};\n\n} // namespace noisferatu\n")
print(f"wrote {dst}: {len(a)} samples")
