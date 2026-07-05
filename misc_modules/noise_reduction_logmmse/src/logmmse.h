#pragma once

#include "arrays.h"
#include "math.h"
#include "bgnoise.h"
#include <utils/flog.h>
#include <array>
#include <deque>

namespace dsp {

    // ported from https://github.com/rajivpoddar/logmmse/  by sannysanoff

    namespace logmmse {

        using namespace ::dsp::arrays;
        using namespace ::dsp::math;

        struct LogMMSE {

            struct SavedParamsC {
                int noise_history_len() {
                    if (nFFT < 1000) {
                        return 2000;
                    } else {
                        return 200;
                    }
                }

                std::deque<FloatArray> noise_history;      // chunks of nFFT*float
                std::deque<FloatArray> dev_history;
                FloatArray sums;      // sliding sum of last N noise_history
                FloatArray devs;      // sliding sum of dev_history
                ComplexArray Xn_prev; // remaining noise

                FloatArray noise_mu2;
                FloatArray Xk_prev;
                ComplexArray x_old;
                bool forceAudio = false;
                bool forceWideband = false;
                int forceSampleRate = 0;


                int Slen;
                int PERC;
                int len1;
                int len2;
                FloatArray win;
                int nFFT;
                Arg<FFTPlan> forwardPlan;
                Arg<FFTPlan> reversePlan;
                float aa = 0.98;
                float mu = 0.98;
                float ksi_min;
                bool hold = false;
                long long generation = 0;
                float mindb = 0;
                float maxdb = 0;
                bool stable = false;

                void reset() {
                    noise_history.clear();
                    dev_history.clear();
                    Xk_prev.reset();
                    Xn_prev.reset();
                    noise_mu2.reset();
                    x_old.reset();
                    generation = 0;
                    stable = false;
                    backgroundNoiseCalculator.reset();
                }

                void add_noise_history(const FloatArray &noise) {
                    if (hold) {
                        return;
                    }
                    if (noise->size() != nFFT) {
                        flog::info("ERROR noise->size() != nFFT: {} {}", (int)noise->size(), nFFT);
                        return;
                    }
                    noise_history.emplace_back(noise);
                    volk_32f_x2_add_32f(sums->data(), sums->data(), noise->data(), nFFT);
                    while (noise_history.size() > noise_history_len()) {
                        volk_32f_x2_subtract_32f(sums->data(), sums->data(), noise_history.front()->data(), nFFT);
                        noise_history.pop_front();
                    }

                    auto noiseAvg = div(sums, (float)noise_history.size());

                    auto diff = subeach(noise, noiseAvg);
                    diff = muleach(diff, diff);
                    dev_history.emplace_back(diff);

                    // devs is private to this struct: accumulate in place
                    volk_32f_x2_add_32f(devs->data(), devs->data(), diff->data(), nFFT);

                    while (dev_history.size() > noise_history_len()) {
                        volk_32f_x2_subtract_32f(devs->data(), devs->data(), dev_history.front()->data(), nFFT);
                        dev_history.pop_front();
                    }

                }

                BackgroundNoiseCalculator backgroundNoiseCalculator;

                void update_noise_mu2(const ComplexArray &x) {
                    auto nframes = noise_history.size();
                    bool audioFrequency = nFFT < 1200;
                    if (forceAudio) audioFrequency = true;
                    if (forceWideband) audioFrequency = false;

                    if (nframes > 100 && !hold) {
                        if (audioFrequency) {

                            // recalculate noise floor
                            if (generation > 0) {
                                auto tnm = npzeros(nFFT);
                                auto lower = tnm->data();
                                const int nlower = 12;
                                int ix = 0;
                                for(auto &it: noise_history) {
                                    if (ix >= nframes - nlower) {
                                        auto nhFrame = it->data();
                                        for (auto w = 0; w < nFFT; w++) {
                                            lower[w] += nhFrame[w];
                                        }
                                    }
                                    ix++;
                                }
                                for (auto w = 0; w < nFFT; w++) {
                                    lower[w] /= nlower;
                                    lower[w] *= lower[w];
                                }
                                auto tnoise_mu2 = npmavg(tnm, 6);
                                auto tmindb = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                auto tmaxdb = *std::max_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                if (tmindb + tmaxdb < mindb + maxdb) {
                                    mindb = tmindb;
                                    maxdb = tmaxdb;
                                    noise_mu2 = tnm;
                                    stable = true;
                                }
                            }

                            if (!stable) {

                                // scale the noise figure
                                if (generation == 0) {
                                    auto tnoise_mu2 = npmavg(noise_mu2, 6);
                                    mindb = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                    maxdb = *std::max_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                    flog::debug("logmmse: inited noise floor: {}", mindb);
                                }

                            }
                            generation++;
                        } else {

                            auto noise_mu2_copy = *noise_mu2;

                            auto noiseAvg = mul(sums, 1 / (float)nframes);


                            auto hi = mul(devs, 1 / (float)nframes);
                            auto devSquare = muleach(hi, hi);
                            auto devSquareD = devSquare->data();
                            for (int z = 0; z < nFFT; z++) {
                                if (abs(z - nFFT/2) < nFFT * 15 / 100) {
                                    // after fft, rightmost and leftmost sides of real frequencies range are at the center of the resulting table.
                                    // We exclude middle of the table from lookup
                                    devSquareD[z] = BackgroundNoiseCalculator::ERASED_SAMPLE;
                                }
                            }
                            memset(noise_mu2->data(), 0, nFFT*sizeof(noise_mu2->at(0)));
                            float detectedNoise = backgroundNoiseCalculator.addFrame(*devSquare);
                            auto acceptible_stdev = detectedNoise;
                            auto nmu2 = noise_mu2->data();
                            auto navg =  noiseAvg->data();
                            for(int q=0; q < nFFT; q++) {
                                if (devSquareD[q] < acceptible_stdev) {
                                    nmu2[q] = navg[q] * navg[q];
                                }
                            }

                            if (!linearInterpolateHoles(nmu2, nFFT)) {
                                *noise_mu2 = noise_mu2_copy;
                            }


                        }  // end if audio frequency
                    }
                }
            };

            static void logmmse_sample(const ComplexArray &x, int Srate, float eta, SavedParamsC *params, int noise_frames) {
                params->Slen = floor(0.02 * Srate);
                if (params->Slen % 2 == 1) params->Slen++;
                params->PERC = 50;
                params->len1 = floor(params->Slen * params->PERC / 100);
                params->noise_history.clear();
                params->dev_history.clear();
                params->len2 = params->Slen - params->len1;         // len1+len2
                auto audioFrequency = Srate <= 24000;
                if (params->forceAudio) audioFrequency = true;
                if (params->forceWideband) audioFrequency = false;
                if (audioFrequency) {
                    // probably audio frequency
                    params->win = nphanning(params->Slen);
                    params->win = div(mul(params->win, params->len2), npsum(params->win));
                } else {
                    // probably wide band
                    params->win = nphanning(params->Slen);
                    params->win = div(mul(params->win, params->len2), npsum(params->win));
                }
                params->nFFT = 2 * params->Slen;
                params->forwardPlan = allocateFFTWPlan(false, params->nFFT);
                params->reversePlan = allocateFFTWPlan(true, params->nFFT);
                params->sums = npzeros(params->nFFT);
                params->devs = npzeros(params->nFFT);

                flog::debug("logmmse: sampling noise, srate={}, Slen={}, nFFT={}", Srate, params->Slen, params->nFFT);
                auto noise_mean = npzeros(params->nFFT);
                for (int j = 0; j < params->Slen * noise_frames; j += params->Slen) {
                    npfftfft((muleach(params->win, nparange(x, j, j + params->Slen))), params->forwardPlan);
                    auto noise = npabsolute(params->forwardPlan->getOutput());
                    params->add_noise_history(noise);
                    noise_mean = addeach(noise_mean, noise);
                }
                params->noise_mu2 = div(noise_mean, noise_frames);
                if (!audioFrequency) {
                    params->noise_mu2 = npmavg(params->noise_mu2, 120);
                }
                params->noise_mu2 = muleach(params->noise_mu2, params->noise_mu2);
                params->Xk_prev = npzeros(params->len1);
                params->Xn_prev = npzeros_c(0);
                params->x_old = npzeros_c(params->len1);
                params->ksi_min = ::pow(10, -25.0 / 10.0);
            }

            // maxInput caps how much of x is consumed in one pass (-1 = all of it),
            // so a large backlog can be drained in bounded chunks without copying.
            static ComplexArray logmmse_all(const ComplexArray &x, int Srate, float eta, SavedParamsC *params, int maxInput = -1) {
                int inputLen = (int)x->size();
                if (maxInput >= 0 && maxInput < inputLen) {
                    inputLen = maxInput;
                }
                auto Nframes = floor(inputLen / params->len2) - floor(params->Slen / params->len2);
                params->update_noise_mu2(x);
                auto xfinal = npzeros_c(Nframes * params->len2);
                for (int k = 0; k < Nframes * params->len2; k += params->len2) {
                    auto insign = muleach(params->win, nparange(x, k, k + params->Slen));
                    npfftfft(insign, params->forwardPlan);
                    auto spec = params->forwardPlan->getOutput();
                    auto sig = npabsolute(spec);
                    auto sigD = sig->data();
                    for (auto z = 1; z < sig->size(); z++) {
                        if (sigD[z] == 0) {
                            sigD[z] = sigD[z - 1];      // for some reason fft returns 0 instead if small value
                        }
                    }
                    params->add_noise_history(sig);
                    auto sig2 = muleach(sig, sig);

                    auto gammak = npminimum_(diveach(sig2, params->noise_mu2), 40);
                    FloatArray ksi;
                    if (!npall(params->Xk_prev)) {
                        ksi = add(mul(npmaximum_(add(gammak, -1), 0), 1 - params->aa), params->aa);
                    } else {
                        const FloatArray d1 = diveach(mul(params->Xk_prev, params->aa), params->noise_mu2);
                        const FloatArray m1 = mul(npmaximum_(add(gammak, -1), 0), (1 - params->aa));
                        ksi = addeach(d1, m1);
                        ksi = npmaximum_(ksi, params->ksi_min);
                    }
                    auto A = diveach(ksi, add(ksi, 1));
                    auto vk = muleach(A, gammak);
                    auto ei_vk = mul(scipyspecialexpn(vk), 0.5);
                    auto hw = muleach(A, npexp(ei_vk));
                    sig = muleach(sig, hw);
                    params->Xk_prev = muleach(sig, sig);
                    auto hwmulspec = muleach(hw, spec);
                    npfftfft(hwmulspec, params->reversePlan);
                    auto xi_w0 = params->reversePlan->getOutput();
                    auto final = addeach(params->x_old, nparange(xi_w0, 0, params->len1));
                    nparangeset(xfinal, k, final);
                    params->x_old = nparange(xi_w0, params->len1, params->Slen);
                }
                return xfinal;
            }

        };

    }
}