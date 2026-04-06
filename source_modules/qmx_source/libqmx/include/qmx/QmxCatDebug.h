#pragma once

#include <cstdint>
#include <string>

#ifndef QMX_CAT_DEBUG_TIMING
#define QMX_CAT_DEBUG_TIMING 0
#endif

#ifndef QMX_CAT_RAW_LOG
#define QMX_CAT_RAW_LOG 0
#endif

#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
#include <chrono>

namespace qmx::detail {
    inline std::uint64_t qmxCatDebugNowUs() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
}
#endif

#if QMX_CAT_DEBUG_TIMING
namespace qmx {
    struct QmxCatTimingDebug {
        std::uint64_t ifSequence = 0;
        std::uint64_t ifWriteStartUs = 0;
        std::uint64_t ifWriteDoneUs = 0;
        std::uint64_t ifFlushDoneUs = 0;
        std::uint64_t ifSendUs = 0;
        std::uint64_t ifReplyUs = 0;
        std::uint64_t ifParsedUs = 0;
        std::uint64_t statusQueuedUs = 0;
        std::uint64_t frameDrawUs = 0;
        std::uint64_t statusAppliedUs = 0;
        std::uint64_t tuneCalledUs = 0;
    };
}
#endif

#if QMX_CAT_RAW_LOG
namespace qmx {
    enum class QmxCatLogType {
        Tx,
        Rx,
        Error,
    };

    struct QmxCatLogEntry {
        QmxCatLogType type = QmxCatLogType::Rx;
        std::uint64_t timestampUs = 0;
        std::string text;
    };
}
#endif
