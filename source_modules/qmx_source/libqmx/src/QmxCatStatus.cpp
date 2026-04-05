#include "QmxCatStatus.h"

#include <cctype>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {
    bool parseUnsigned(std::string_view text, std::int64_t& value) {
        if (text.empty()) {
            return false;
        }

        std::int64_t out = 0;
        for (char ch : text) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return false;
            }
            out = (out * 10) + (ch - '0');
        }
        value = out;
        return true;
    }

    bool parseSigned(std::string_view text, int& value) {
        if (text.empty()) {
            return false;
        }

        bool negative = false;
        std::size_t start = 0;
        if (text[0] == '+' || text[0] == '-') {
            negative = (text[0] == '-');
            start = 1;
        }
        if (start >= text.size()) {
            return false;
        }

        int out = 0;
        for (std::size_t i = start; i < text.size(); ++i) {
            char ch = text[i];
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return false;
            }
            out = (out * 10) + (ch - '0');
        }
        value = negative ? -out : out;
        return true;
    }

    bool applyModeChar(char modeChar, qmx::QmxStatus& status) {
        switch (static_cast<char>(std::toupper(static_cast<unsigned char>(modeChar)))) {
        case 'C':
            status.hasMode = true;
            status.mode = qmx::QmxMode::CW;
            return true;
        case 'D':
            status.hasMode = true;
            status.mode = qmx::QmxMode::DIGI;
            return true;
        case 'U':
            status.hasMode = true;
            status.mode = qmx::QmxMode::USB;
            status.hasSideband = true;
            status.sideband = qmx::QmxSideband::USB;
            return true;
        case 'L':
            status.hasMode = true;
            status.mode = qmx::QmxMode::LSB;
            status.hasSideband = true;
            status.sideband = qmx::QmxSideband::LSB;
            return true;
        case '3':
            status.hasMode = true;
            status.mode = qmx::QmxMode::CW;
            return true;
        case '7':
            status.hasMode = true;
            status.mode = qmx::QmxMode::CWR;
            return true;
        case '6':
        case '9':
            status.hasMode = true;
            status.mode = qmx::QmxMode::DIGI;
            return true;
        default:
            return false;
        }
    }
}

namespace qmx::detail {
    void QmxCatStatusParser::reset() {
        std::lock_guard<std::mutex> lock(mutex);
        parserBuffer.clear();
        status = {};
        pendingMenuValue = PendingMenuValue::None;
    }

    void QmxCatStatusParser::setStatusCallback(StatusCallback callback, void* ctx) {
        std::lock_guard<std::mutex> lock(mutex);
        statusCallback = callback;
        statusCtx = ctx;
    }

    void QmxCatStatusParser::armPendingMenuValue(PendingMenuValue value) {
        std::lock_guard<std::mutex> lock(mutex);
        pendingMenuValue = value;
    }

    void QmxCatStatusParser::clearPendingMenuValue() {
        std::lock_guard<std::mutex> lock(mutex);
        pendingMenuValue = PendingMenuValue::None;
    }

    bool QmxCatStatusParser::hasPendingMenuValue() const {
        std::lock_guard<std::mutex> lock(mutex);
        return pendingMenuValue != PendingMenuValue::None;
    }

    void QmxCatStatusParser::feedBytes(const char* data, std::size_t count) {
        if (!data || count == 0) {
            return;
        }

        std::vector<QmxStatus> snapshots;
        StatusCallback callback = nullptr;
        void* ctx = nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex);
            callback = statusCallback;
            ctx = statusCtx;

            for (std::size_t i = 0; i < count; ++i) {
                unsigned char ch = static_cast<unsigned char>(data[i]);
                if (ch == ';') {
                    if (parserBuffer.empty()) {
                        continue;
                    }

                    QmxStatus snapshot;
                    std::string response;
                    response.swap(parserBuffer);
                    if (processResponseLocked(response, snapshot)) {
                        snapshots.push_back(snapshot);
                    }
                    continue;
                }

                if (ch < 32) {
                    continue;
                }

                parserBuffer.push_back(static_cast<char>(ch));
                if (parserBuffer.size() > 128) {
                    parserBuffer.erase(0, parserBuffer.size() - 64);
                }
            }
        }

        if (callback) {
            for (const auto& snapshot : snapshots) {
                callback(snapshot, ctx);
            }
        }
    }

    QmxStatus QmxCatStatusParser::snapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        return status;
    }

    bool QmxCatStatusParser::processResponseLocked(const std::string& response, QmxStatus& snapshotOut) {
        if (response == "?") {
            pendingMenuValue = PendingMenuValue::None;
            return false;
        }
        if (response.size() < 2) {
            return false;
        }

        const std::string code = response.substr(0, 2);
        const std::string payload = response.substr(2);
        bool changed = false;

        if (code == "IF") {
            std::int64_t frequency = 0;
            if (payload.size() >= 11 && parseUnsigned(std::string_view(payload).substr(0, 11), frequency)) {
                status.hasFrequency = true;
                status.frequency = frequency;
                changed = true;
            }

            int ritHz = 0;
            if (payload.size() >= 21 && parseSigned(std::string_view(payload).substr(16, 5), ritHz)) {
                status.hasRit = true;
                status.ritHz = ritHz;
                changed = true;
            }
            if (payload.size() > 21 && (payload[21] == '0' || payload[21] == '1')) {
                status.hasRitEnabled = true;
                status.ritEnabled = (payload[21] == '1');
                changed = true;
            }
            if (payload.size() > 26 && (payload[26] == '0' || payload[26] == '1')) {
                status.hasTransmit = true;
                status.transmit = (payload[26] == '1');
                changed = true;
            }
            if (payload.size() > 27 && applyModeChar(payload[27], status)) {
                changed = true;
            }
            if (payload.size() > 28 && (payload[28] == '0' || payload[28] == '1')) {
                status.hasRxVfo = true;
                status.rxVfo = payload[28] - '0';
                changed = true;
            }
            if (payload.size() > 30 && (payload[30] == '0' || payload[30] == '1')) {
                status.hasSplit = true;
                status.split = (payload[30] == '1');
                changed = true;
            }
        }
        else if (code == "FA") {
            std::int64_t frequency = 0;
            if (parseUnsigned(payload, frequency)) {
                status.hasVfoAFrequency = true;
                status.vfoAFrequency = frequency;
                changed = true;
            }
        }
        else if (code == "FB") {
            std::int64_t frequency = 0;
            if (parseUnsigned(payload, frequency)) {
                status.hasVfoBFrequency = true;
                status.vfoBFrequency = frequency;
                changed = true;
            }
        }
        else if (code == "FR") {
            if (!payload.empty() && (payload[0] == '0' || payload[0] == '1')) {
                status.hasRxVfo = true;
                status.rxVfo = payload[0] - '0';
                changed = true;
            }
        }
        else if (code == "FT") {
            if (!payload.empty() && (payload[0] == '0' || payload[0] == '1')) {
                status.hasTxVfo = true;
                status.txVfo = payload[0] - '0';
                changed = true;
            }
        }
        else if (code == "SP") {
            if (!payload.empty() && (payload[0] == '0' || payload[0] == '1')) {
                status.hasSplit = true;
                status.split = (payload[0] == '1');
                changed = true;
            }
        }
        else if (code == "TQ") {
            if (!payload.empty() && (payload[0] == '0' || payload[0] == '1')) {
                status.hasTransmit = true;
                status.transmit = (payload[0] == '1');
                changed = true;
            }
        }
        else if (code == "Q1") {
            if (!payload.empty() && (payload[0] == '0' || payload[0] == '1')) {
                status.hasSideband = true;
                status.sideband = (payload[0] == '1') ? QmxSideband::LSB : QmxSideband::USB;
                changed = true;
            }
        }
        else if (code == "MD") {
            if (!payload.empty() && applyModeChar(payload[0], status)) {
                changed = true;
            }
        }
        else if (code == "MM") {
            if (pendingMenuValue == PendingMenuValue::CwOffset) {
                int value = 0;
                std::int64_t unsignedValue = 0;
                if (parseSigned(payload, value)) {
                    status.hasCwOffset = true;
                    status.cwOffsetHz = value;
                    changed = true;
                }
                else if (parseUnsigned(payload, unsignedValue)) {
                    status.hasCwOffset = true;
                    status.cwOffsetHz = static_cast<int>(unsignedValue);
                    changed = true;
                }
                pendingMenuValue = PendingMenuValue::None;
            }
        }
        else if (code == "SM") {
            int value = 0;
            if (parseSigned(payload, value)) {
                status.hasSMeter = true;
                status.sMeterDb = value;
                changed = true;
            }
        }
        else if (code == "PC") {
            std::int64_t value = 0;
            if (parseUnsigned(payload, value)) {
                status.hasPower = true;
                status.powerTenthsW = static_cast<int>(value);
                changed = true;
            }
        }
        else if (code == "SW") {
            if (payload.empty()) {
                if (status.hasSWR) {
                    status.hasSWR = false;
                    changed = true;
                }
            }
            else {
                std::int64_t value = 0;
                if (parseUnsigned(payload, value)) {
                    status.hasSWR = true;
                    status.swrHundredths = static_cast<int>(value);
                    changed = true;
                }
            }
        }

        if (!changed) {
            return false;
        }

        status.sequence++;
        snapshotOut = status;
        return true;
    }
}
