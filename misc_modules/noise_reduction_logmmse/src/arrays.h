#pragma once
#include <memory>
#include <vector>

#ifdef WIN32
#define _USE_MATH_DEFINES
#include <math.h>
#endif

#include "dsp/block.h"

namespace dsp {

    namespace math {

        // Exponential integral E1(x), table-driven (see arrays.cpp).
        float expn(float q);
        // Fill zero-valued gaps in arr by linear interpolation between the
        // surrounding non-zero samples; returns false if arr is all zeros.
        bool linearInterpolateHoles(float *arr, int narr);
    }

    /**
     * NumPy-lookalike vector ops for the LogMMSE port (logmmse.h). The
     * vocabulary deliberately mirrors the Python reference implementation so
     * the port stays line-for-line auditable against it — keep additions
     * limited to what that algorithm needs, and write new in-place/streaming
     * code with raw VOLK instead (see add_noise_history in logmmse.h).
     *
     * Convention: a trailing underscore (npmaximum_, npminimum_, div_) means
     * the function mutates its first argument in place and returns it; all
     * other functions allocate their result.
     */
    namespace arrays {

        template <class X>
        using Arg = std::shared_ptr<X>;
        typedef std::shared_ptr<std::vector<float>> FloatArray;
        typedef std::shared_ptr<std::vector<dsp::complex_t>> ComplexArray;

        // Pooled buffer allocation. The DSP hot paths (LogMMSE etc.) allocate a
        // handful of fixed sizes thousands of times per second; the shared_ptr
        // deleter parks the backing vector in a size-keyed pool for reuse
        // instead of freeing it. alloc* returns a vector of the requested size
        // with UNSPECIFIED contents; the *Zero variants clear it.
        FloatArray allocFloatArray(size_t size);
        FloatArray allocFloatArrayZero(size_t size);
        ComplexArray allocComplexArray(size_t size);
        ComplexArray allocComplexArrayZero(size_t size);

        FloatArray npzeros(int size);
        ComplexArray npzeros_c(int size);

        // hanning window
        FloatArray nphanning(int len);

        // add all items together
        float npsum(const FloatArray& v);
        // true if no element is zero (numpy.all on the != 0 mask)
        bool npall(const FloatArray& v);

        // multiply by scalar
        FloatArray mul(const FloatArray& v, float e);
        // divide by scalar
        FloatArray div(const FloatArray& v, float e);
        // add scalar to all items
        FloatArray add(const FloatArray& v, float e);

        // elementwise binary ops
        FloatArray addeach(const FloatArray& v, const FloatArray& w);
        FloatArray subeach(const FloatArray& v, const FloatArray& w);
        FloatArray muleach(const FloatArray& v, const FloatArray& w);
        FloatArray diveach(const FloatArray& v, const FloatArray& w);
        ComplexArray addeach(const ComplexArray& v, const ComplexArray& w);
        ComplexArray muleach(const FloatArray& v, const ComplexArray& w);

        // in place: clamp v to at most / at least lim, return v
        FloatArray npminimum_(const FloatArray& v, float lim);
        FloatArray npmaximum_(const FloatArray& v, float lim);
        // in place: divide all elements of v by val
        void div_(const ComplexArray& v, float val);

        // copy of v[begin..end)
        ComplexArray nparange(const ComplexArray& v, int begin, int end);
        // copy part into v starting at begin
        void nparangeset(const ComplexArray& v, int begin, const ComplexArray& part);

        // elementwise exp (VOLK expfast: ~1e-4 relative error)
        FloatArray npexp(const FloatArray& v);
        // elementwise magnitude of complex array
        FloatArray npabsolute(const ComplexArray& in);
        // elementwise E1 (scipy.special.expn(1, x))
        FloatArray scipyspecialexpn(const FloatArray& in);

        // centered moving average, only even window sizes
        FloatArray npmavg(const FloatArray& v, int windowSize);

        // zero-padded / truncated copy (returns in unchanged if size matches)
        ComplexArray resize(const ComplexArray& in, int nsize);

        struct FFTPlan {
            virtual ComplexArray getInput() = 0;
            virtual ComplexArray getOutput() = 0;
            virtual ComplexArray npfftfft(const ComplexArray& in) = 0;
            virtual ~FFTPlan() {

            }
        };

        Arg<FFTPlan> allocateFFTWPlan(bool backward, int buckets);

        void npfftfft(const ComplexArray& in, const Arg<FFTPlan>& plan);
    }
}
