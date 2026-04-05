#pragma once

#include <qmx/QmxDevice.h>

#include <cstddef>
#include <mutex>
#include <string>

namespace qmx::detail {
    enum class PendingMenuValue {
        None,
        CwOffset,
    };

    class QmxCatStatusParser {
    public:
        void reset();
        void setStatusCallback(StatusCallback callback, void* ctx);
        void armPendingMenuValue(PendingMenuValue value);
        void clearPendingMenuValue();
        bool hasPendingMenuValue() const;

        void feedBytes(const char* data, std::size_t count);
        QmxStatus snapshot() const;

    private:
        bool processResponseLocked(const std::string& response, QmxStatus& snapshotOut);

        mutable std::mutex mutex;
        std::string parserBuffer;
        QmxStatus status;
        PendingMenuValue pendingMenuValue = PendingMenuValue::None;
        StatusCallback statusCallback = nullptr;
        void* statusCtx = nullptr;
    };
}