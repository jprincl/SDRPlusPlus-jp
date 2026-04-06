#include "FreqModeSync.h"

#include <core.h>
#include <gui/gui.h>
#include <gui/tuner.h>
#include <module_com.h>
#include <radio_interface.h>
#include <signal_path/signal_path.h>
#include <utils/flog.h>

#include <cmath>
#include <string>
#include <utility>

// ── Frequency model ────────────────────────────────────────────────────

double FreqModeSync::qmxRigToIqOffset(const qmx::QmxStatus& status) {
    const double cwOffsetHz = status.hasCwOffset ? static_cast<double>(status.cwOffsetHz) : kQmxDefaultCwOffsetHz;
    double offset = kQmxIfOffsetHz;
    if (!status.hasMode)
        return offset;
    switch (status.mode) {
    case qmx::QmxMode::CW:  return offset + cwOffsetHz;
    case qmx::QmxMode::CWR: return offset - cwOffsetHz;
    default:                return offset;
    }
}

double FreqModeSync::rigFrequencyToCenterFrequency(std::int64_t rigFreq, const qmx::QmxStatus& status) {
    return static_cast<double>(rigFreq) - qmxRigToIqOffset(status);
}

std::int64_t FreqModeSync::centerFrequencyToRigFrequency(double centerFreq, const qmx::QmxStatus& status) {
    return static_cast<std::int64_t>(std::llround(centerFreq + qmxRigToIqOffset(status)));
}

std::int64_t FreqModeSync::effectiveReceiveRigFrequency(const qmx::QmxStatus& status) {
    std::int64_t frequency = status.frequency;
    if (status.hasRit && status.hasRitEnabled && status.ritEnabled)
        frequency += status.ritHz;
    return frequency;
}

int FreqModeSync::qmxModeToRadioIface(qmx::QmxMode mode) {
    switch (mode) {
    case qmx::QmxMode::LSB: return RADIO_IFACE_MODE_LSB;
    case qmx::QmxMode::USB: return RADIO_IFACE_MODE_USB;
    case qmx::QmxMode::CW:  return RADIO_IFACE_MODE_CW;
    case qmx::QmxMode::CWR: return RADIO_IFACE_MODE_CWR;
    default:                 return -1;
    }
}

qmx::QmxMode FreqModeSync::radioIfaceToQmxMode(int radioMode) {
    switch (radioMode) {
    case RADIO_IFACE_MODE_LSB: return qmx::QmxMode::LSB;
    case RADIO_IFACE_MODE_USB: return qmx::QmxMode::USB;
    case RADIO_IFACE_MODE_CW:  return qmx::QmxMode::CW;
    case RADIO_IFACE_MODE_CWR: return qmx::QmxMode::CWR;
    default:                   return qmx::QmxMode::UNKNOWN;
    }
}

// ── Lifecycle ──────────────────────────────────────────────────────────

void FreqModeSync::setDevice(qmx::QmxDevice* device) {
    m_device = device;
}

void FreqModeSync::start(double initialFreq, bool syncVfo) {
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_pendingStatus = {};
        m_hasPendingStatus = false;
    }
    m_status = {};
    m_hasStatus = false;
    m_iqCenterFreq = initialFreq;
    m_syncVfo = syncVfo;
    m_lastModeSentToQmx = -1;
    m_running = true;
}

void FreqModeSync::stop() {
    m_running = false;
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_pendingStatus = {};
        m_hasPendingStatus = false;
    }
    m_status = {};
    m_hasStatus = false;
    m_lastModeSentToQmx = -1;
}

// ── Events ─────────────────────────────────────────────────────────────

// Called by SDR++ when the IQ center frequency changes (tune callback, GUI thread).
// This is the SDR++ → QMX direction.  We:
//   1. Compute the new rig frequency from the cached status.
//   2. Send that rig frequency to QMX.
//   3. Update the cached status so it reflects the new rig frequency immediately
//      (as if QMX had already confirmed it).
//   4. If syncVfo: place the SDR++ VFO at the new rig frequency.
// This way the cached status is always the source of truth and tick() won't
// produce a feedback bounce.
void FreqModeSync::onIqCenterChanged(double newFreq) {
    if (m_iqCenterFreq == newFreq)
        return;

    m_iqCenterFreq = newFreq;

    if (!m_running || !m_hasStatus || !m_status.hasFrequency)
        return;
    if (m_status.hasTransmit && m_status.transmit)
        return;

    // 1. Compute desired rig frequency.
    std::int64_t newRigFreq = centerFrequencyToRigFrequency(newFreq, m_status);
    std::int64_t curRigFreq = effectiveReceiveRigFrequency(m_status);
    if (newRigFreq == curRigFreq)
        return;

    // 2. Send to QMX (enqueued, non-blocking).
    //    Use the active receive VFO (A/B) from cached status to send FA or FB.
    int rxVfo = m_status.hasRxVfo ? m_status.rxVfo : 0;
    std::string error;
    if (!m_device->setFrequency(newRigFreq, rxVfo, &error)) {
        flog::warn("FreqModeSync: {}", error);
        return;
    }

    // 3. Update cached status immediately.
    m_status.frequency = newRigFreq;
    m_status.hasFrequency = true;
    // Also suppress a stale pending status from overwriting this.
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        if (m_hasPendingStatus) {
            m_pendingStatus.frequency = newRigFreq;
            m_pendingStatus.hasFrequency = true;
        }
    }

    // 4. If syncVfo, move the SDR++ VFO to the rig frequency.
    if (m_syncVfo) {
        std::string vfoName = gui::waterfall.selectedVFO;
        if (!vfoName.empty() && sigpath::vfoManager.vfoExists(vfoName)) {
            double vfoAbsFreq = gui::waterfall.getCenterFrequency()
                                + sigpath::vfoManager.getOffset(vfoName);
            if (std::llround(vfoAbsFreq) != newRigFreq)
                tuner::tune(tuner::TUNER_MODE_NORMAL, vfoName, static_cast<double>(newRigFreq));
        }
    }
}

void FreqModeSync::onStatusReceived(const qmx::QmxStatus& status) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_pendingStatus = status;
    m_hasPendingStatus = true;
}

// ── Configuration ──────────────────────────────────────────────────────

void FreqModeSync::setSyncVfo(bool enabled) {
    m_syncVfo = enabled;
}

bool FreqModeSync::getSyncVfo() const {
    return m_syncVfo;
}

// ── Per-frame tick ─────────────────────────────────────────────────────

void FreqModeSync::tick() {
    qmx::QmxStatus  newQMXStatus;
    bool            hasNewQMXStatus = false;

    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        if (m_hasPendingStatus) {
            hasNewQMXStatus    = true;
            newQMXStatus       = m_pendingStatus;
            m_hasPendingStatus = false;
        }
    }

    if (hasNewQMXStatus) {
        m_status    = newQMXStatus;
        m_hasStatus = true;
        if (m_running && newQMXStatus.hasFrequency && !(newQMXStatus.hasTransmit && newQMXStatus.transmit)) {
            const std::int64_t rigFreq = effectiveReceiveRigFrequency(newQMXStatus);
            const double centerFrequency = rigFrequencyToCenterFrequency(rigFreq, newQMXStatus);
            // Update SDR++ IQ center if it doesn't match the cached rig frequency.
            // tuner::tune(IQ_ONLY) calls our onIqCenterChanged, which will recompute
            // the same rig frequency from the just-updated cache → no-op, no feedback.
            if (std::llround(m_iqCenterFreq) != std::llround(centerFrequency)) {
                tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", centerFrequency);
                if (m_syncVfo) {
                    std::string vfoName = gui::waterfall.selectedVFO;
                    if (!vfoName.empty() && sigpath::vfoManager.vfoExists(vfoName)) {
                        double vfoAbsFreq = gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(vfoName);
                        if (std::llround(vfoAbsFreq) != rigFreq)
                            tuner::tune(tuner::TUNER_MODE_NORMAL, vfoName, static_cast<double>(rigFreq));
                    }
                }
            }
        }
    }

    if (m_syncVfo) {
        // VFO sync: move SDR++ VFO to rig frequency and sync mode.
        std::string vfoName = gui::waterfall.selectedVFO;
        if (!vfoName.empty() && sigpath::vfoManager.vfoExists(vfoName) && m_status.hasFrequency) {
            double vfoAbsFreq = gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(vfoName);
            const std::int64_t rigFreq = effectiveReceiveRigFrequency(m_status);
            if (std::llround(vfoAbsFreq) != rigFreq)
                tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", rigFrequencyToCenterFrequency(vfoAbsFreq, m_status));
        }
        // Sync mode: QMX → SDR++ radio.
        if (m_status.hasMode && !vfoName.empty() && core::modComManager.getModuleName(vfoName) == "radio") {
            int targetMode = qmxModeToRadioIface(m_status.mode);
            if (targetMode >= 0) {
                int currentRadioMode = -1;
                core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_GET_MODE, NULL, &currentRadioMode);
                if (currentRadioMode != targetMode) {
                    core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_MODE, &targetMode, NULL);
                    m_lastModeSentToQmx = targetMode;
                }
            }
        }
    }
}
