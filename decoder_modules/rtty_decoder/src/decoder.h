#pragma once
#include <signal_path/signal_path.h>
#include <signal_path/vfo_manager.h>

// Abstract decoder interface, mirroring the control surface of the
// psk_decoder / mfsk_decoder modules so the module UI (band view, AFC,
// AF slider, squelch, sideband) stays identical across decoders.

class Decoder {
public:
    virtual ~Decoder() {}

    // Mode-specific UI drawn under the shared controls (decoded text, etc.).
    virtual void showMenu() {}

    virtual void setVFO(VFOManager::VFO* vfo) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    // Shared control surface --------------------------------------------------
    virtual void  setVFOMode(int mode) = 0;     // 0 USB, 1 LSB, 2 NFM
    virtual void  setMode(int modeIdx) = 0;      // RTTY preset index
    virtual void  setAFFreq(double f) = 0;
    virtual float getAFFreq() const = 0;
    virtual void  setSquelchEnabled(bool en) = 0;
    virtual void  setSquelchLevel(float dB) = 0;
    virtual void  setAFCEnabled(bool en) = 0;
    virtual double getTrackedAFFreq() const = 0;

    // GUI band view -----------------------------------------------------------
    virtual int    getBandSpectrum(float* out, int n) const = 0;
    virtual double getBandFlo() const = 0;
    virtual double getBandFhi() const = 0;
    virtual double getSignalBandwidth() const = 0;
};
