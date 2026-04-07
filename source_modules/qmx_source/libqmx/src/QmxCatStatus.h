#pragma once

#include <qmx/QmxDevice.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace qmx::detail {
    enum class PendingMenuValue {
        None,
        CwOffset,
    };

    // Encode mode into a CAT command "MD"
    bool encodeModeCommand(qmx::QmxMode mode, std::string& command);

    class QmxCatStatusParser {
    public:
        void reset();
        void beginBatch(std::size_t expectedResponses = 0);
        void publishBatch();
        bool batchComplete() const;
        void setStatusCallback(StatusCallback callback, void* ctx);
#if QMX_CAT_RAW_LOG
        void setCatLogCallback(CatLogCallback callback, void* ctx);
        void noteError(const std::string& text);
#endif
        // QMX menu responses are not specific, thus only a single menu query could be in flight.
        // Remember which menu query is pending in order to correctly parse the response.
        void armPendingMenuValue(PendingMenuValue value);
        void clearPendingMenuValue();
        bool hasPendingMenuValue() const;
#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        void noteWrite(const std::string& command, std::uint64_t writeStartUs, std::uint64_t writeDoneUs, std::uint64_t flushDoneUs);
#endif

        // Feed raw CAT response bytes into the parser.
        // Whenever a full response is parsed, the internal QmxStatus is updated and the status callback is triggered.
        void feedBytes(const char* data, std::size_t count);
        void clearStatusFlags(QmxStatusFlags flags);
        QmxStatus snapshot() const;

    private:
        bool processResponseLocked(const std::string& response);

        mutable std::mutex mutex;
        // Buffer for accumulating incoming CAT response until a full response is received (terminated by ';')
        // over multiple calls of feedBytes().
        std::string parserBuffer;
        QmxStatus status;
        PendingMenuValue pendingMenuValue = PendingMenuValue::None;
        StatusCallback statusCallback = nullptr;
        void* statusCtx = nullptr;
        bool batchDirty = false;
        bool batchHadIf = false;
        std::size_t batchExpectedResponses = 0;
        std::size_t batchRepliesSeen = 0;
#if QMX_CAT_RAW_LOG
        CatLogCallback catLogCallback = nullptr;
        void* catLogCtx = nullptr;
#endif
#if QMX_CAT_DEBUG_TIMING
        std::uint64_t nextIfSequence = 1;
        std::uint64_t pendingIfSequence = 0;
        std::uint64_t pendingIfWriteStartUs = 0;
        std::uint64_t pendingIfWriteDoneUs = 0;
        std::uint64_t pendingIfSendUs = 0;
#endif
    };
}
