// golden_test.cpp — emit the C++ engine's 16 kHz post-effects stream for every
// (bank, algo), so it can be diffed against the Python reference (golden_test.py).
//
// Build:  clang++ -std=c++20 -O2 -I ../Source golden_test.cpp -o golden_test_cpp
// Output: one line per algorithm:  "<bank> <algo> v0 v1 ... vN-1"  (integers, = sample*512)
#include <cmath>
#include <cstdio>
#include <vector>
#include "Engine.h"

using namespace noisferatu;

int main()
{
    constexpr int N = 4096;                 // 8 x 512-sample blocks
    const double P1 = 0.3, P2 = 0.7, BC = 1.0, RT = 1.0, VOL = 0.8;

    std::vector<float> buf (N);
    for (int b = 0; b < 5; ++b)
        for (int a = 0; a < 9; ++a)
        {
            Engine e;                       // fresh engine => fresh shared RNG (default seed)
            e.setControls (P1, P2, BC, RT, VOL);
            e.setBankAlgo (b, a);           // no-op for (0,0), matching engine construction
            e.debugRender16k (buf.data(), N);

            std::printf ("%d %d", b, a);
            for (int i = 0; i < N; ++i)
                std::printf (" %d", static_cast<int> (std::lround (buf[i] * 512.0)));
            std::printf ("\n");
        }
    return 0;
}
