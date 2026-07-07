//
// Created by san on 10/07/22.
//
#pragma once

#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cassert>

class BackgroundNoiseCalculator {

    double lastNoise = ERASED_SAMPLE;
    static constexpr auto NBUCKETS = 1000;
    static constexpr auto SKIP_FRAMES = 10;
    std::vector<int> buckets;
    std::vector<float> logFrame;
    int frameCount = 0;

public:

    static constexpr auto ERASED_SAMPLE = 1e9f;

    void reset() {
        lastNoise = ERASED_SAMPLE;
        frameCount = 0;
    }

    // Any float container with size() and range-for (accepts both
    // std::vector<float> and dsp::arrays::FloatArray, which uses a
    // custom allocator and is therefore a distinct type).
    template <class Vec>
    float addFrame(const Vec &fftFrame) {
        if (frameCount > 0 && frameCount % SKIP_FRAMES != 0) {
            frameCount++;
            return lastNoise;
        }
        frameCount++;
        float minn = ERASED_SAMPLE;
        float maxx = -ERASED_SAMPLE;
        logFrame.clear();
        logFrame.reserve(fftFrame.size());
        for(float q : fftFrame) {
            // Skip non-positive samples: log10(0) = -inf would poison minn and
            // turn the bucket index math below into NaN (UB when cast to int).
            if(q != ERASED_SAMPLE && q > 0.0f) {
                q = log10(q);
                minn = std::min<float>(minn, q);
                maxx = std::max<float>(maxx, q);
                logFrame.push_back(q);
            }
        }
        if (logFrame.empty()) {
            // All samples erased; keep the previous noise estimate.
            return lastNoise;
        }
        auto width = maxx - minn;
        buckets.resize(NBUCKETS);
        memset(buckets.data(), 0, sizeof(int) * NBUCKETS);
        if (width > 0) {
            for(auto f : logFrame) {
                int bucket = std::min((int) (NBUCKETS * ((f - minn) / width)), NBUCKETS - 1);
                buckets[bucket]++;
            }
        } else {
            // All samples equal (width == 0 would divide 0/0 = NaN); the peak is trivially minn.
            buckets[0] = (int) logFrame.size();
        }
        auto ix = std::max_element(buckets.begin(), buckets.end()) - buckets.begin();
        double maxf = pow(10, ((((double)ix)/NBUCKETS) * width + minn));
        if (lastNoise == ERASED_SAMPLE) {
            lastNoise = maxf;
        } else {
            lastNoise = 0.9 * lastNoise + 0.1 * maxf;
        }
        return lastNoise;

    }

};