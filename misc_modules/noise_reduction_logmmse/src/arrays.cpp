#define _USE_MATH_DEFINES

#include "arrays.h"
#include <fftw3.h>

#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utils/flog.h>

namespace dsp {

    namespace math {

        // Exponential integral E1(x) for x > 0, used to build the lookup table
        // below. Rational approximations from Abramowitz & Stegun 5.1.53 / 5.1.56,
        // absolute error < 2e-7, well below the float precision of the table.
        // Not std::expint: libc++ (Apple clang) does not ship the C++17 special
        // math functions.
        static double expintE1(double x) {
            if (x <= 1.0) {
                double p = 0.00107857;
                p = p * x - 0.00976004;
                p = p * x + 0.05519968;
                p = p * x - 0.24991055;
                p = p * x + 0.99999193;
                p = p * x - 0.57721566;
                return p - std::log(x);
            }
            double num = (((x + 8.5733287401) * x + 18.0590169730) * x + 8.6347608925) * x + 0.2677737343;
            double den = (((x + 9.5733223454) * x + 25.6329561486) * x + 21.0996530827) * x + 3.9584969228;
            return num / den * std::exp(-x) / x;
        }

        // E1 lookup table on an x^(1/4)-spaced grid: entry i holds E1((i / EXPN_GRID)^4),
        // covering x in [0, 81]. Built once at load time.
        static constexpr int EXPN_GRID = 1000;
        static constexpr int EXPN_SIZE = 3001;
        static const std::array<float, EXPN_SIZE> expnTable = [] {
            std::array<float, EXPN_SIZE> t;
            // E1 diverges at x = 0; clamp the first entry to E1(1e-16).
            t[0] = (float)expintE1(1e-16);
            for (int i = 1; i < EXPN_SIZE; i++) {
                double r = (double)i / EXPN_GRID;
                t[i] = (float)expintE1(r * r * r * r);
            }
            return t;
        }();

        float expn(float q) {
            // Clamp instead of extrapolating: E1 saturates near 0 and underflows
            // to 0 past x = 81, so the edge entries are the correct limits.
            if (!(q > 0.0f)) return expnTable[0]; // also catches NaN
            double index = std::sqrt(std::sqrt((double)q)) * EXPN_GRID;
            if (index >= EXPN_SIZE - 1) return expnTable[EXPN_SIZE - 1];
            int i = (int)index;
            float f = (float)(index - i);
            return expnTable[i] + (expnTable[i + 1] - expnTable[i]) * f;
        }

        bool linearInterpolateHoles(float *arr, int narr) {
            int firstV = -1;
            int lastV = -1;
            for(int q=0; q < narr; q++) {
                float val = arr[q];
                if(firstV < 0 && val != 0) {
                    firstV = q;
                }
                if (val != 0) {
                    if (lastV != -1) {
                        if (q - lastV > 1) {
                            // fill the gap
                            auto d = (val - arr[lastV]) / (q - lastV);
                            auto running = arr[lastV];
                            for(int w=lastV+1; w<q; w++) {
                                running += d;
                                arr[w] = running;
                            }
                        }
                    }
                    lastV = q;
                }
            }
            if (firstV < 0 || lastV < 0) {
                return false;
            } else {
                auto v = arr[firstV];
                for (int q = firstV - 1; q >= 0; q--) {
                    arr[q] = v;
                }
                v = arr[lastV];
                for (int q = lastV + 1; q < narr; q++) {
                    arr[q] = v;
                }
                return true;
            }
        }
    }

    namespace arrays {

        // Recycles vector backing storage through the shared_ptr deleter: on
        // release the vector is parked in a bucket keyed by its capacity, and
        // a matching-size request pops it back out. In steady state the DSP
        // hot loops cycle through a few fixed sizes, so nearly every request
        // is a pool hit and the vector is returned at the requested size
        // without touching its contents (no realloc, no zero-fill).
        template <class T>
        class VectorPool {
            std::mutex mtx;
            std::unordered_map<size_t, std::vector<std::vector<T>*>> buckets;
            static constexpr size_t MAX_PER_BUCKET = 32;

        public:
            std::shared_ptr<std::vector<T>> get(size_t size) {
                std::vector<T>* buf = nullptr;
                {
                    std::lock_guard<std::mutex> lck(mtx);
                    auto it = buckets.find(size);
                    if (it != buckets.end() && !it->second.empty()) {
                        buf = it->second.back();
                        it->second.pop_back();
                    }
                }
                if (!buf) {
                    buf = new std::vector<T>();
                    buf->reserve(size);
                }
                buf->resize(size);
                return std::shared_ptr<std::vector<T>>(buf, [this](std::vector<T>* p) { put(p); });
            }

        private:
            void put(std::vector<T>* p) {
                std::unique_lock<std::mutex> lck(mtx);
                auto& bucket = buckets[p->capacity()];
                if (bucket.size() >= MAX_PER_BUCKET) {
                    lck.unlock();
                    delete p;
                    return;
                }
                bucket.push_back(p);
            }
        };

        // Leaked singletons: outstanding shared_ptr deleters may run during
        // static destruction, so the pools must never be destroyed.
        static VectorPool<float>& floatPool() {
            static VectorPool<float>* pool = new VectorPool<float>();
            return *pool;
        }
        static VectorPool<dsp::complex_t>& complexPool() {
            static VectorPool<dsp::complex_t>* pool = new VectorPool<dsp::complex_t>();
            return *pool;
        }

        FloatArray allocFloatArray(size_t size) {
            return floatPool().get(size);
        }

        FloatArray allocFloatArrayZero(size_t size) {
            auto r = floatPool().get(size);
            memset(r->data(), 0, size * sizeof(float));
            return r;
        }

        ComplexArray allocComplexArray(size_t size) {
            return complexPool().get(size);
        }

        ComplexArray allocComplexArrayZero(size_t size) {
            auto r = complexPool().get(size);
            memset(r->data(), 0, size * sizeof(dsp::complex_t));
            return r;
        }

        FloatArray npzeros(int size) {
            return allocFloatArrayZero(size);
        }

        ComplexArray npzeros_c(int size) {
            return allocComplexArrayZero(size);
        }

        // hanning window
        FloatArray nphanning(int len) {
            auto retval = allocFloatArray(len);
            for (int i = 0; i < len; i++) {
                retval->at(i) = (0.5 - 0.5 * cos(2.0 * M_PI * i / (len - 1)));
            }
            return retval;
        }

        // add all items together
        float npsum(const FloatArray& v) {
            float f = 0;
            for (auto d : *v) {
                f += d;
            }
            return f;
        }

        bool npall(const FloatArray& v) {
            for (auto d : *v) {
                if (d == 0) {
                    return false;
                }
            }
            return true;
        }

        // multiply by scalar
        FloatArray mul(const FloatArray& v, float e) {
            auto retval = allocFloatArray(v->size());
            volk_32f_s32f_multiply_32f(retval->data(), v->data(), e, v->size());
            return retval;
        }

        FloatArray div(const FloatArray& v, float e) {
            auto retval = allocFloatArray(v->size());
            volk_32f_s32f_multiply_32f(retval->data(), v->data(), 1.0 / e, v->size());
            return retval;
        }

        // add scalar to all items
        FloatArray add(const FloatArray& v, float e) {
            auto retval = allocFloatArray(v->size());
            int limit = (int)v->size();
            auto* src = v->data();
            auto* dst = retval->data();
            for (int i=0; i<limit; i++) {
                dst[i] = src[i] + e;
            }
            return retval;
        }

        // add two arrays
        FloatArray addeach(const FloatArray& v, const FloatArray& w) {
            auto retval = allocFloatArray(v->size());
            volk_32f_x2_add_32f(retval->data(), v->data(), w->data(), v->size());
            return retval;
        }

        // subtract two arrays
        FloatArray subeach(const FloatArray& v, const FloatArray& w) {
            auto retval = allocFloatArray(v->size());
            volk_32f_x2_subtract_32f(retval->data(), v->data(), w->data(), v->size());
            return retval;
        }

        // add two arrays
        ComplexArray addeach(const ComplexArray& v, const ComplexArray& w) {
            auto retval = allocComplexArray(v->size());
            volk_32fc_x2_add_32fc((lv_32fc_t*)retval->data(), (const lv_32fc_t*)v->data(), (const lv_32fc_t*)w->data(), v->size());
            return retval;
        }

        // multiply two arrays
        FloatArray muleach(const FloatArray& v, const FloatArray& w) {
            auto retval = allocFloatArray(v->size());
            volk_32f_x2_multiply_32f(retval->data(), v->data(), w->data(), v->size());
            return retval;
        }

        // multiply complex array by real array
        ComplexArray muleach(const FloatArray& v, const ComplexArray& w) {
            auto retval = allocComplexArray(v->size());
            volk_32fc_32f_multiply_32fc((lv_32fc_t*)retval->data(), (const lv_32fc_t*)w->data(), v->data(), v->size());
            return retval;
        }

        FloatArray diveach(const FloatArray& v, const FloatArray& w) {
            auto retval = allocFloatArray(v->size());
            volk_32f_x2_divide_32f(retval->data(), v->data(), w->data(), v->size());
            return retval;
        }

        FloatArray npminimum_(const FloatArray& v, float lim) {
            auto rvD = v->data();
            for (size_t q = 0; q < v->size(); q++) {
                if (rvD[q] > lim) {
                    rvD[q] = lim;
                }
            }
            return v;
        }

        FloatArray npmaximum_(const FloatArray& v, float lim) {
            auto rvD = v->data();
            for (size_t q = 0; q < v->size(); q++) {
                if (rvD[q] < lim) {
                    rvD[q] = lim;
                }
            }
            return v;
        }

        void div_(const ComplexArray& v, float val) {
            volk_32fc_s32fc_multiply_32fc((lv_32fc_t*)v->data(), (lv_32fc_t*)v->data(), lv_32fc_t(1.0f / val, 0.0f), v->size());
        }

        // array range
        ComplexArray nparange(const ComplexArray& v, int begin, int end) {
            auto retval = allocComplexArray(end - begin);
            memcpy(retval->data(), v->data() + begin, (end - begin) * sizeof(dsp::complex_t));
            return retval;
        }

        // update array in-place
        void nparangeset(const ComplexArray& v, int begin, const ComplexArray& part) {
            memmove(v->data() + begin, part->data(), sizeof(part->at(0)) * part->size());
        }

        FloatArray npexp(const FloatArray& v) {
            auto retval = allocFloatArray(v->size());
            volk_32f_expfast_32f(retval->data(), v->data(), v->size());
            return retval;
        }

        FloatArray npabsolute(const ComplexArray& in) {
            auto retval = allocFloatArray(in->size());
            volk_32fc_magnitude_32f(retval->data(), (const lv_32fc_t*)in->data(), in->size());
            return retval;
        }

        FloatArray scipyspecialexpn(const FloatArray& in) {
            auto retval = allocFloatArray(in->size());
            auto rvD = retval->data();
            auto inD = in->data();
            for (size_t q = 0; q < in->size(); q++) {
                rvD[q] = dsp::math::expn(inD[q]);
            }
            return retval;
        }

        // only even window sizes
        FloatArray npmavg(const FloatArray& v, int windowSize) {
            auto retval = allocFloatArray(v->size());
            auto rvD = retval->data();
            size_t written = 0;
            float sum = 0;
            float count = 0;
            auto ws2 = windowSize / 2;
            for (int ix = 0; ix < v->size() + ws2; ix++) {
                if (ix < v->size()) {
                    sum += v->at(ix);
                    count++;
                }
                if (ix > windowSize) {
                    count--;
                    sum -= v->at(ix - count);
                }
                if (ix >= ws2) {
                    rvD[written++] = sum / count;
                }
            }
            if (written != v->size()) {
                flog::info("FloatArray npmavg: written != v->size() {} {}\n", (int)written, (int)v->size());
                abort();
            }
            return retval;
        }

        ComplexArray resize(const ComplexArray& in, int nsize) {
            if (in->size() == nsize) {
                return in;
            }
            auto retval = std::make_shared<std::vector<dsp::complex_t>>(nsize, dsp::complex_t{ 0, 0 });
            auto limit = in->size();
            if (nsize < in->size()) {
                limit = nsize;
            }
            std::copy(in->begin(), in->begin() + limit, retval->begin());
            return retval;
        }

        struct fftwPlanImplFFTW : public FFTPlan {
            int nbuckets;
            bool reverse;
            fftwf_plan_s* p;
            ComplexArray input;
            ComplexArray output;

            fftwPlanImplFFTW(bool backward, int buckets) : FFTPlan() {
                nbuckets = buckets;
                reverse = backward;
                input = npzeros_c(buckets);
                output = npzeros_c(buckets);
                p = fftwf_plan_dft_1d(buckets, (fftwf_complex*)input->data(), (fftwf_complex*)output->data(), backward ? FFTW_BACKWARD : FFTW_FORWARD, FFTW_ESTIMATE);
            }

            ComplexArray getInput() override {
                return input;
            }
            ComplexArray getOutput() override {
                return output;
            }

            ComplexArray npfftfft(const ComplexArray& in) override {
                auto in0 = resize(in, nbuckets);
                std::copy(in0->begin(), in0->end(), input->begin());
                fftwf_execute(p);
                if (reverse) {
                    div_(output, nbuckets);
                }
                return output;
            }

            virtual ~fftwPlanImplFFTW() {
                fftwf_destroy_plan(p);
            }
        };

        Arg<FFTPlan> allocateFFTWPlan(bool backward, int buckets) {
            return std::shared_ptr<FFTPlan>(new fftwPlanImplFFTW(backward, buckets));
        }

        void npfftfft(const ComplexArray& in, const Arg<FFTPlan>& plan) {
            plan->npfftfft(in);
        }
    }
}
