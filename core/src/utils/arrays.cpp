#define _USE_MATH_DEFINES

#include <utils/arrays.h>
#include <fftw3.h>

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#include <vector>
#include <memory>
#endif

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utils/flog.h>

static long long currentTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

long long fftCumulativeTime = 0;
bool enableAcceleratedFFT = false;

namespace dsp {

    /**
     * this implements python-style nparray interface
     */

    namespace math {

        std::vector<float> sma(int smawindow, std::vector<float>& src) {
            float running = 0;
            std::vector<float> dest;
            for (int q = 0; q < src.size(); q++) {
                running += src[q];
                float taveraged = 0;
                if (q >= smawindow) {
                    running -= src[q - smawindow];
                    taveraged = running / smawindow;
                }
                else {
                    taveraged = running / q;
                }
                dest.emplace_back(taveraged);
            }
            return dest;
        }

        std::vector<float> maxeach(int maxwindow, std::vector<float>& src) {
            float running = 0;
            std::vector<float> dest;
            for (int q = 0; q < src.size(); q++) {
                running = std::max<float>(src[q], running);
                if (q % maxwindow == maxwindow - 1 || q == src.size() - 1) {
                    dest.emplace_back(running);
                    running = 0;
                }
            }
            return dest;
        }


        double sinc(double omega, double x, double norm) {
            return (x == 0.0f) ? 1.0f : (sin(omega * x) / (norm * x));
        }

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

        FloatArray centeredSma(FloatArray in, int winsize) {
            int limit = in->size();
            auto rv = npzeros(limit);
            float total = 0;
            int win2 = winsize / 2;
            auto indata = in->data();
            auto outdata = rv->data();
            for(int i=0; i<winsize; i++) {
                total += indata[i];
            }
            for(int i=winsize; i<limit; i++) {
                outdata[i - win2] = total / winsize;
                total += indata[i] - indata[i-winsize];
            }
            for(int i=0; i<winsize-win2; i++) {
                outdata[i] = outdata[winsize-win2];
            }
            for(int i=limit - win2; i<limit; i++) {
                outdata[i] = outdata[limit-win2-1];
            }
            return rv;
        }

        FloatArray movingVariance(FloatArray in, int winsize) {
            auto mean = centeredSma(in, winsize);
            auto squaredDistances = npzeros(mean->size());
            auto meanData = mean->data();
            auto squaredDistancesData = squaredDistances->data();
            auto inData = in->data();
            for(int i=0; i<mean->size(); i++) {
                squaredDistancesData[i] = (inData[i] - meanData[i]) * (inData[i] - meanData[i]);
            }
            return centeredSma(squaredDistances, winsize);
        }

        struct fftwPlanImplFFTW : public FFTPlan {
            int nbuckets;
            bool reverse;
            fftwf_plan_s* p;
            ComplexArray input;
            ComplexArray output;

            fftwPlanImplFFTW(bool backward, int buckets) : FFTPlan() {
                auto plan = this;
                plan->nbuckets = buckets;
                plan->reverse = backward;
                plan->input = npzeros_c(buckets);
                plan->output = npzeros_c(buckets);
                auto p = fftwf_plan_dft_1d(buckets, (fftwf_complex*)plan->input->data(), (fftwf_complex*)plan->output->data(), backward ? FFTW_BACKWARD : FFTW_FORWARD, FFTW_ESTIMATE);
                plan->p = p;
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
//                auto out0 = npzeros_c(nbuckets);
                fftwf_execute(p);
//                std::copy(output->begin(), output->end(), out0->begin());
//                this->output->resize(nbuckets);
                if (reverse) {
                    div_(this->output, nbuckets);
                } else {
                    //return out0;
                }
                return this->output;
            }

            virtual ~fftwPlanImplFFTW() {
                fftwf_destroy_plan(p);
            }
        };

#ifdef __APPLE__
        struct vDSPPlanImpl : public FFTPlan {
            int nbuckets;
            bool reverse;
            DSPSplitComplex tempSplitComplex;
            std::shared_ptr<std::vector<dsp::complex_t>> input;
            std::shared_ptr<std::vector<dsp::complex_t>> output;
            FFTSetup fftSetup;

            vDSPPlanImpl(bool backward, int buckets) : FFTPlan() {
                nbuckets = buckets;
                reverse = backward;
                input = npzeros_c(buckets);
                output = npzeros_c(buckets);

                // Prepare the FFT Setup object
                fftSetup = vDSP_create_fftsetup((int)log2(nbuckets), FFT_RADIX2);

                // Initialize the DSPSplitComplex structure
                tempSplitComplex.realp = (float *)malloc(nbuckets * sizeof(float));
                tempSplitComplex.imagp = (float *)malloc(nbuckets * sizeof(float));
            }

            ComplexArray getInput() override {
                return input;
            }

            ComplexArray getOutput() override {
                return output;
            }

            ComplexArray npfftfft(const ComplexArray& in) override {
                // Resize and copy input data to tempSplitComplex
                auto in0 = resize(in, nbuckets);
                auto in0P = in0->data();
                for (int i = 0; i < nbuckets; ++i) {
                    tempSplitComplex.realp[i] = in0P[i].re;
                    tempSplitComplex.imagp[i] = in0P[i].im;
                }

                // Perform FFT
                if (reverse) {
                    vDSP_fft_zip(fftSetup, &tempSplitComplex, 1, log2(nbuckets), kFFTDirection_Inverse);
                } else {
                    vDSP_fft_zip(fftSetup, &tempSplitComplex, 1, log2(nbuckets), kFFTDirection_Forward);
                }

                // Copy the results to output
//                auto out0 = npzeros_c(nbuckets);
//                auto out0P = out0->data();
                auto outputP = output->data();
                for (int i = 0; i < nbuckets; ++i) {
                    outputP[i] =
//                        out0P[i] =
                            dsp::complex_t{tempSplitComplex.realp[i], tempSplitComplex.imagp[i]};
                }

                if (reverse) {
                    div_(output, nbuckets);
                } else {
                    //return out0;
                }
                return this->output;
            }

            virtual ~vDSPPlanImpl() {
                // Cleanup
                vDSP_destroy_fftsetup(fftSetup);
                free(tempSplitComplex.realp);
                free(tempSplitComplex.imagp);
            }
        };
#endif

        std::string dumpArr(const FloatArray &x) {
            return dumpArr(x->data(), x->size());
        }

        std::string dumpArr(const float *x, int limit) {
            std::string s;
            auto minn = x[0];
            auto maxx = x[0];
            int lim = 10;
            for (int q = 0; q < limit; q++) {
                auto v = x[q];
                if (false) {
                    if (q < lim) {
                        s.append(std::to_string(v));
                        s.append(" ");
                    }
                    if (v > maxx) {
                        maxx = v;
                    }
                    if (v < minn) {
                        minn = v;
                    }
                } else {
                    s.append(std::to_string(q));
                    s.append(" ");
                    s.append(std::to_string(v));
                    s.append("\n");
                }
            }
            if (false) {
                std::string pre = "min/max=";
                pre.append(std::to_string(minn));
                pre += "/";
                pre.append(std::to_string(maxx));
                pre.append(" ");
                return pre + s;
            } else {
                return s;
            }
        }

        std::string dumpArr(const ComplexArray& x) {
            return dumpArr(x->data(), x->size());
        }

        std::string dumpArr(const dsp::complex_t *x, int limit) {
            std::string s;
            auto minn = x[0].re;
            auto maxx = x[0].re;
            for (int q = 0; q < limit; q++) {
                s.append(std::to_string(q));
                s.append(" ");
                auto v = x[q].amplitude();
                s.append(std::to_string(x[q].re));
                s.append(" ");
                s.append(std::to_string(x[q].im));
                s.append(" ");
                if (v > maxx) {
                    maxx = v;
                }
                if (v < minn) {
                    minn = v;
                }
                s += "\n";
            }
            if (false) {
                std::string pre = "min/max=";
                pre.append(std::to_string(minn));
                pre += "/";
                pre.append(std::to_string(maxx));
                pre.append(" ");
                return pre + s;
            } else {
                return s;
            }
        }

        void dumpArr_(const FloatArray& x) {
            std::cout << dumpArr(x->data(), x->size()) << std::endl;
        }

        void dumpArr_(const std::vector<float> &x) {
            std::cout << dumpArr(x.data(), x.size()) << std::endl;
        }

        void dumpArr_(const ComplexArray& x) {
            std::cout << dumpArr(x->data(), x->size()) << std::endl;
        }

        void dumpArr_(const std::vector<dsp::complex_t> &x) {
            std::cout << dumpArr(x.data(), x.size()) << std::endl;
        }

        void dumpArr_(dsp::complex_t *ptr, int len) {
            std::cout << dumpArr(ptr, len) << std::endl;
        }

        void dumpArr_(float *ptr, int len) {
            std::cout << dumpArr(ptr, len) << std::endl;
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

        // multiply by scalar
        FloatArray mul(const FloatArray& v, float e) {
            auto retval = allocFloatArray(v->size());
            volk_32f_s32f_multiply_32f(retval->data(), v->data(), e, v->size());
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
#ifndef VOLK_VERSION
            auto rD = retval->data();
            for (int q = 0; q < v->size(); q++) {
                rD[q] = v->at(q) + w->at(q);
            }
#else
            volk_32fc_x2_add_32fc((lv_32fc_t*)retval->data(), (lv_32fc_t*)v->data(), (lv_32fc_t*)w->data(), v->size());
#endif
            return retval;
        }

        // multiply two arrays
        FloatArray muleach(const FloatArray& v, const FloatArray& w) {
            auto retval = allocFloatArray(v->size());
            volk_32f_x2_multiply_32f(retval->data(), v->data(), w->data(), v->size());
            return retval;
        }

        // multiply two arrays
        ComplexArray muleach(const FloatArray& v, const ComplexArray& w) {
            auto retval = allocComplexArray(v->size());
            auto rD = retval->data();
            auto wD = w->data();
            auto vD = v->data();
            for (int q = 0; q < v->size(); q++) {
                rD[q] = dsp::complex_t{ vD[q], 0 } * wD[q];
            }
            return retval;
        }

        FloatArray diveach(const FloatArray& v, const FloatArray& w) {
            auto retval = allocFloatArray(v->size());
            volk_32f_x2_divide_32f(retval->data(), v->data(), w->data(), v->size());
            return retval;
        }

        bool npall(const FloatArray& v) {
            int countZeros = 0;
            //            int firstZero = -1;
            for (auto d : *v) {
                if (d == 0) {
                    return false;
                }
                /*
                                if (countZeros == 0) {
                                    firstZero++;
                                }
                */
            }
            //            if (countZeros) {
            ////                std::cout << "npall: " << countZeros << "/" << v->size() << " first at " << firstZero << std::endl;
            //                return false;
            //            }
            return true;
        }

        FloatArray div(const FloatArray& v, float e) {
            auto retval = allocFloatArray(v->size());
            volk_32f_s32f_multiply_32f(retval->data(), v->data(), 1.0 / e, v->size());
            return retval;
        }

        FloatArray npminimum(const FloatArray& v, const FloatArray& w) {
            auto retval = std::make_shared<std::vector<float>>();
            retval->reserve(v->size());
            //            int ix = 0;
            for (int q = 0; q < retval->size(); q++) {
                if (v->at(q) < w->at(q)) {
                    retval->emplace_back(v->at(q));
                }
                else {
                    retval->emplace_back(w->at(q));
                }
            }
            return retval;
        }

        FloatArray npminimum(const FloatArray& v, float lim) {
            auto retval = std::make_shared<std::vector<float>>();
            retval->reserve(v->size());
            //            int ix = 0;
            for (auto d : *v) {
                if (d < lim) {
                    retval->emplace_back(d);
                }
                else {
                    retval->emplace_back(lim);
                }
                //                ix++;
                //                if (ix == 1000000) {
                //                    std::cout << "XX";
                //                }
            }
            return retval;
        }

        FloatArray npminimum_(const FloatArray& v, float lim) {
            auto retval = allocFloatArray(v->size());
            memcpy(retval->data(), v->data(), v->size() * sizeof(float));
            auto rvD = retval->data();
            for (int q = 0; q < retval->size(); q++) {
                if (rvD[q] > lim) {
                    rvD[q] = lim;
                }
            }
            return retval;
        }


        ComplexArray div(const ComplexArray& v, float val) {
            auto retval = allocComplexArray(v->size());
            volk_32fc_s32fc_multiply_32fc((lv_32fc_t*)retval->data(), (lv_32fc_t*)v->data(), lv_32fc_t(1.0f / val, 0.0f), v->size());
            return retval;
        }

        void div_(ComplexArray& v, float val) {
            volk_32fc_s32fc_multiply_32fc((lv_32fc_t*)v->data(), (lv_32fc_t*)v->data(), lv_32fc_t(1.0f / val, 0.0f), v->size());
        }

        float npmax(const FloatArray& v) {
            float m = v->front();
            auto rvD = v->data();
            for (int q = 1; q < v->size(); q++) {
                if (rvD[q] > m) {
                    m = rvD[q];
                }
            }
            return m;
        }

        float npmin(const FloatArray& v) {
            float m = v->front();
            auto rvD = v->data();
            for (int q = 1; q < v->size(); q++) {
                if (rvD[q] < m) {
                    m = rvD[q];
                }
            }
            return m;
        }

        FloatArray npmaximum(const FloatArray& v, float lim) {
            auto retval = allocFloatArray(v->size());
            memcpy(retval->data(), v->data(), v->size() * sizeof(float));
            auto rvD = retval->data();
            for (int q = 0; q < retval->size(); q++) {
                if (rvD[q] < lim) {
                    rvD[q] = lim;
                }
            }
            return retval;
        }

        FloatArray npmaximum_(const FloatArray& v, float lim) {
            auto rvD = v->data();
            for (int q = 0; q < v->size(); q++) {
                if (rvD[q] < lim) {
                    rvD[q] = lim;
                }
            }
            return v;
        }

        // array range
        FloatArray nparange(const FloatArray& v, int begin, int end) {
            if (end == -1) {
                end = v->size();
            }
            auto retval = allocFloatArray(end - begin);
            memcpy(retval->data(), v->data() + begin, (end - begin) * sizeof(float));
            return retval;
        }

        ComplexArray nparange(const Arg<std::vector<dsp::complex_t>>& v, int begin, int end) {
            auto retval = allocComplexArray(end - begin);
            memcpy(retval->data(), v->data() + begin, (end - begin) * sizeof(dsp::complex_t));
            return retval;
        }

        // update array in-place
        void nparangeset(const FloatArray& v, int begin, const FloatArray& part) {
            for (int i = 0; i < part->size(); i++) {
                (*v)[begin + i] = part->at(i);
            }
        }

        void nparangeset(const ComplexArray& v, int begin, const ComplexArray& part) {
            memmove(v->data() + begin, part->data(), sizeof(part->at(0)) * part->size());
            //            for (int i = 0; i < part->size(); i++) {
            //                (*v)[begin + i] = part->at(i);
            //            }
        }

        FloatArray neg(const FloatArray& v) {
            auto retval = std::make_shared<std::vector<float>>();
            retval->reserve(v->size());
            for (auto d : *v) {
                retval->emplace_back(-d);
            }
            return retval;
        }

        FloatArray npexp(const FloatArray& v) {
            auto retval = allocFloatArray(v->size());
            volk_32f_expfast_32f(retval->data(), v->data(), v->size());
            return retval;
        }

        FloatArray npsqrt(const FloatArray& v) {
            auto retval = std::make_shared<std::vector<float>>();
            retval->reserve(v->size());
            for (auto d : *v) {
                retval->emplace_back(sqrt(d));
            }
            return retval;
        }

        FloatArray nplog(const FloatArray& v) {
            auto retval = std::make_shared<std::vector<float>>();
            for (auto d : *v) {
                retval->emplace_back(log(d));
            }
            return retval;
        }

        ComplexArray tocomplex(const FloatArray& v) {
            auto retval = std::make_shared<std::vector<dsp::complex_t>>();
            retval->reserve(v->size());
            for (auto d : *v) {
                retval->emplace_back(dsp::complex_t{ d, 0.0f });
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

        FloatArray npreal(const ComplexArray& v) {
            auto retval = std::make_shared<std::vector<float>>();
            retval->reserve(v->size());
            for (auto d : *v) {
                retval->emplace_back(d.re);
            }
            return retval;
        }

        FloatArray npzeros(int size) {
            return allocFloatArrayZero(size);
        }

        FloatArray hamming(int N) {
            FloatArray window = npzeros(N);
            const double PI = 3.14159265358979323846;

            for (int i = 0; i < N; ++i) {
                window->at(i) = 0.54 - 0.46 * cos(2 * PI * i / (N - 1));
            }

            return window;
        }

        FloatArray linspace(float start, float stop, int num) {
            auto array = std::make_shared<std::vector<float>>(num);
            float step = (stop - start) / (num - 1);
            for (int i = 0; i < num; i++) {
                (*array)[i] = start + i * step;
            }
            return array;
        }

        void swapfft(const ComplexArray &arr) {
            int s2 = arr->size() / 2;
            auto data = arr->data();
            for(int i=0; i<s2; i++) {
                std::swap(data[i], data[i+s2]);
            }
        }

        ComplexArray npzeros_c(int size) {
            return allocComplexArrayZero(size);
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

        FloatArray scipyspecialexpn(const FloatArray& in) {
            auto retval = allocFloatArray(in->size());
            auto rvD = retval->data();
            auto inD = in->data();
            for (auto q = 0; q < in->size(); q++) {
                rvD[q] = dsp::math::expn(inD[q]);
            }
            return retval;
        }

        FloatArray maximum(const FloatArray& in, float value) {
            auto retval = allocFloatArray(in->size());
            auto rvD = retval->data();
            auto inD = in->data();
            for (auto q = 0; q < in->size(); q++) {
                rvD[q] = std::max<float>(inD[q], value);
            }
            return retval;
        }

        FloatArray clone(const FloatArray& in) {
            auto retval = allocFloatArray(in->size());
            memcpy(retval->data(), in->data(), in->size() * sizeof(float));
            return retval;
        }

        ComplexArray clone(const ComplexArray& in) {
            auto retval = allocComplexArray(in->size());
            memcpy(retval->data(), in->data(), in->size() * sizeof(dsp::complex_t));
            return retval;
        }

        FloatArray npabsolute(const ComplexArray& in) {
            auto retval = allocFloatArray(in->size());
            volk_32fc_magnitude_32f(retval->data(), (const lv_32fc_t*)in->data(), in->size());
            return retval;
        }


        Arg<FFTPlan> allocateFFTWPlan(bool backward, int buckets) {
            FFTPlan *plan;
#ifdef __APPLE__
            if (enableAcceleratedFFT) {
                plan = new vDSPPlanImpl(backward, buckets);
            } else {
                plan = new fftwPlanImplFFTW(backward, buckets);
            }
#else
            plan = new fftwPlanImplFFTW(backward, buckets);
#endif
            return std::shared_ptr<FFTPlan>(plan);
        }

        void npfftfft(const ComplexArray& in, const Arg<FFTPlan>& plan) {
            auto ctm = currentTimeMillis();
            plan->npfftfft(in);
            fftCumulativeTime += currentTimeMillis() - ctm;
        }

        std::string ftos(float x) {
            char buf[100];
            snprintf(buf, sizeof(buf), "%1.5f", x);
            return std::string(buf);
        }

        std::string sampleArr(const FloatArray& x) {
            return std::string("[") + ftos(x->at(0)) + "," + ftos(x->at(1)) + ",..," + ftos(x->at(40)) + ",..," + ftos(x->at(140)) + ",...]";
        }

        std::string sampleArr(const ComplexArray& x) {
            return std::string("[") + ftos(x->at(0).re) + "," + ftos(x->at(1).re) + ",..," + ftos(x->at(40).re) + ",...]";
        }

        // For FloatArray
        FloatArray tile(const FloatArray& arr, size_t repeat) {
            size_t arr_size = arr->size();
            FloatArray result = std::make_shared<std::vector<float>>(arr_size * repeat);

            for (size_t i = 0; i < repeat; i++) {
                std::copy(arr->begin(), arr->end(), result->begin() + i * arr_size);
            }

            return result;
        }

        // For ComplexArray
        ComplexArray tile(const ComplexArray& arr, size_t repeat) {
            size_t arr_size = arr->size();
            ComplexArray result = std::make_shared<std::vector<complex_t>>(arr_size * repeat);

            for (size_t i = 0; i < repeat; i++) {
                std::copy(arr->begin(), arr->end(), result->begin() + i * arr_size);
            }

            return result;
        }

        // For FloatArray
        float amin(const FloatArray& arr) {
            return *std::min_element(arr->begin(), arr->end());
        }

        // For ComplexArray
        complex_t amin(const ComplexArray& arr) {
            return *std::min_element(arr->begin(), arr->end(),
                                     [](const complex_t& a, const complex_t& b) {
                                         return a.amplitude() < b.amplitude();
                                     });
        }


        // For float values
        float power(float base, float exponent) {
            return std::pow(base, exponent);
        }

        // For complex_t values
        complex_t power(const complex_t& base, float exponent) {
            float r = std::pow(base.amplitude(), exponent);
            float theta = base.phase() * exponent;
            return complex_t{ r * std::cos(theta), r * std::sin(theta) };
        }

        // For FloatArray
        FloatArray concatenate(const FloatArray& arr1, const FloatArray& arr2) {
            FloatArray result = std::make_shared<std::vector<float>>(arr1->size() + arr2->size());
            std::copy(arr1->begin(), arr1->end(), result->begin());
            std::copy(arr2->begin(), arr2->end(), result->begin() + arr1->size());
            return result;
        }

        // For ComplexArray
        ComplexArray concatenate(const ComplexArray& arr1, const ComplexArray& arr2) {
            ComplexArray result = std::make_shared<std::vector<complex_t>>(arr1->size() + arr2->size());
            std::copy(arr1->begin(), arr1->end(), result->begin());
            std::copy(arr2->begin(), arr2->end(), result->begin() + arr1->size());
            return result;
        }

        ComplexArray conj(const ComplexArray& arr) {
            ComplexArray result = std::make_shared<std::vector<complex_t>>(arr->size());
            std::transform(arr->begin(), arr->end(), result->begin(), [](const complex_t& a) { return a.conj(); });
            return result;
        }

        FloatArray exp(const FloatArray& arr) {
            FloatArray result = std::make_shared<std::vector<float>>(arr->size());
            std::transform(arr->begin(), arr->end(), result->begin(), [](float a) { return std::exp(a); });
            return result;
        }

        FloatArray real(const ComplexArray& arr) {
            FloatArray result = std::make_shared<std::vector<float>>(arr->size());
            std::transform(arr->begin(), arr->end(), result->begin(), [](const complex_t& a) { return a.re; });
            return result;
        }

        FloatArray convolve(const FloatArray& arr1, const FloatArray& arr2) {
            size_t size1 = arr1->size();
            size_t size2 = arr2->size();
            size_t result_size = size1 + size2 - 1;
            FloatArray result = std::make_shared<std::vector<float>>(result_size, 0);

            for (size_t i = 0; i < size1; i++) {
                for (size_t j = 0; j < size2; j++) {
                    (*result)[i + j] += (*arr1)[i] * (*arr2)[j];
                }
            }

            return result;
        }

        ComplexArray operator*(const ComplexArray& a, const FloatArray& b) {
            return muleach(b, a);
        }

        FloatArray operator*(const FloatArray& a, const FloatArray& b) {
            return muleach(b, a);
        }

        FloatArray operator*(const float a, const FloatArray& b) {
            return mul(b, a);
        }

        FloatArray operator/(const FloatArray& a, const FloatArray& b) {
            return diveach(b, a);
        }

        FloatArray operator/(const FloatArray& a, float b) {
            return div(a, b);
        }

        FloatArray operator-(const FloatArray& a, float b) {
            return add(a, -b);
        }

        FloatArray operator+(float a, const FloatArray& b) {
            return add(b, a);
        }

        FloatArray operator+(const FloatArray& a, const FloatArray& b) {
            return addeach(b, a);
        }

    }
}
