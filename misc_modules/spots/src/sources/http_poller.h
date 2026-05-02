#ifndef __SDRPP_SPOTS_HTTP_POLLER_H
#define __SDRPP_SPOTS_HTTP_POLLER_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <stdexcept>
#include <utils/proto/curl_http.h>
#include "../main.h"

class HTTPPoller : public SpotProvider {
public:
    virtual ~HTTPPoller() {
        stop();
    }

    void start() {
        if (workerThread.joinable()) { workerThread.join(); }
        {
            std::lock_guard lk(mtx);
            if (running) { return; }
            running = true;
        }
        flog::info("spots: starting worker thread");
        workerThread = std::thread(&HTTPPoller::worker, this);
    }

    void stop() {
        {
            std::lock_guard lk(mtx);
            if (!running) { return; }
            running = false;
        }
        cv.notify_all();
        if (workerThread.joinable()) { workerThread.join(); }
    }

protected:
    virtual void processResponse(std::string response) = 0;
    char url[1024];

private:

    bool isRunning() {
        std::lock_guard lk(mtx);
        return running;
    }

    void worker() {
        flog::info("spots: worker starting");
        while (isRunning()) {
            try {
                net::http::CurlRequestOptions options;
                options.timeoutMs = 30000;
                options.maxBody = 4 * 1024 * 1024;

                net::http::CurlResponse response = net::http::curlGet(url, options);
                if (response.status != 200) {
                    flog::error("spots: got HTTP status {0} from {1}", response.status, url);
                    break;
                }

                processResponse(response.body);
            }
            catch (const std::exception& e) {
                flog::error("spots: error polling {0}: {1}", url, e.what());
                break;
            }

            std::unique_lock cv_lk(mtx);
            if (!running) {
                break;
            }
            cv.wait_for(cv_lk, std::chrono::milliseconds(pollPeriod), [&]() { return !running; });
        }
        flog::info("spots: worker stopping");
    }

    // Threading
    int pollPeriod = 15000;
    bool running = false;
    std::thread workerThread;
    std::condition_variable cv;
    std::mutex mtx;
};

#endif //__SDRPP_SPOTS_HTTP_POLLER_H
