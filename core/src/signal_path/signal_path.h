#pragma once
#include "iq_frontend.h"
#include "vfo_manager.h"
#include "source.h"
#include "sink.h"
#include <module.h>
#include <utils/event.h>

namespace sigpath {
    SDRPP_EXPORT IQFrontEnd iqFrontEnd;
    SDRPP_EXPORT VFOManager vfoManager;
    SDRPP_EXPORT SourceManager sourceManager;
    SDRPP_EXPORT SinkManager sinkManager;

    // Emitted on transmit state changes (true = TX active). Consumed e.g. by
    // noise reduction to freeze its noise profile while transmitting.
    SDRPP_EXPORT Event<bool> txState;
};