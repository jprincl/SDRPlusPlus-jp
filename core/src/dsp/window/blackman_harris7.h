#pragma once
#include "cosine.h"

namespace dsp::window {
    // 7-term Blackman-Harris (P. Albrecht), ~-180 dB sidelobes, NENBW 2.63 bins
    inline double blackmanHarris7(double n, double N) {
        const double coefs[] = {
            0.27105140069342,
            0.43329793923448,
            0.21812299954311,
            0.06592544638803,
            0.01081174209837,
            0.00077658482522,
            0.00001388721735
        };
        return cosine(n, N, coefs, sizeof(coefs) / sizeof(double));
    }
}
