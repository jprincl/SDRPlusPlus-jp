#include "SerialCat.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#ifndef _WIN32
#include <cerrno>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <filesystem>
#endif

namespace {
#ifndef _WIN32
    speed_t fixedBaudToPosix() {
#ifdef B115200
        return B115200;
#else
        return B38400;
#endif
    }
#endif

    bool encodeModeCommand(qmx::QmxMode mode, std::string& command) {
        switch (mode) {
        case qmx::QmxMode::LSB:
            command = "MD1;";
            return true;
        case qmx::QmxMode::USB:
            command = "MD2;";
            return true;
        case qmx::QmxMode::CW:
            command = "MD3;";
            return true;
        case qmx::QmxMode::CWR:
            command = "MD7;";
            return true;
        case qmx::QmxMode::DIGI:
        case qmx::QmxMode::FM:
        case qmx::QmxMode::AM:
        case qmx::QmxMode::UNKNOWN:
        default:
            return false;
        }
    }
}

namespace qmx::detail {
    SerialCatPort::~SerialCatPort() {
        close();
    }

    bool SerialCatPort::open(const std::string& portName) {
        close();

#ifdef _WIN32
        std::string fullPort = portName;
        if (fullPort.rfind("\\\\.\\", 0) != 0) {
            fullPort = "\\\\.\\" + portName;
        }

        HANDLE serial = CreateFileA(fullPort.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (serial == INVALID_HANDLE_VALUE) {
            return false;
        }

        DCB dcb = {};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(serial, &dcb)) {
            CloseHandle(serial);
            return false;
        }

        dcb.BaudRate = qmx::kSerialBaudRate;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity = NOPARITY;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fAbortOnError = FALSE;

        if (!SetCommState(serial, &dcb)) {
            CloseHandle(serial);
            return false;
        }

        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = 20;
        timeouts.ReadTotalTimeoutConstant = 20;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 20;
        timeouts.WriteTotalTimeoutMultiplier = 1;
        SetCommTimeouts(serial, &timeouts);
        PurgeComm(serial, PURGE_RXCLEAR | PURGE_TXCLEAR);

        handle = serial;
#else
        int fd = ::open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (fd < 0) {
            return false;
        }

        termios tty = {};
        if (tcgetattr(fd, &tty) != 0) {
            ::close(fd);
            return false;
        }

        cfmakeraw(&tty);
        speed_t speed = fixedBaudToPosix();
        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);
        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
        tty.c_cflag |= CLOCAL | CREAD;
        tty.c_cflag &= ~(PARENB | PARODD);
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;

        if (tcsetattr(fd, TCSANOW, &tty) != 0) {
            ::close(fd);
            return false;
        }

        tcflush(fd, TCIOFLUSH);
        handle = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
#endif

        poller.start(this, storedStatusCallback, storedStatusCtx
#if QMX_CAT_RAW_LOG
                     , storedCatLogCallback, storedCatLogCtx
#endif
        );
        return true;
    }

    void SerialCatPort::close() {
        poller.stop();

        if (!handle) {
            return;
        }

#ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(handle));
#else
        ::close(static_cast<int>(reinterpret_cast<intptr_t>(handle)));
#endif
        handle = nullptr;
    }

    bool SerialCatPort::isOpen() const {
        return handle != nullptr;
    }

    void SerialCatPort::setStatusCallback(StatusCallback callback, void* ctx) {
        storedStatusCallback = callback;
        storedStatusCtx = ctx;
    }

#if QMX_CAT_RAW_LOG
    void SerialCatPort::setCatLogCallback(CatLogCallback callback, void* ctx) {
        storedCatLogCallback = callback;
        storedCatLogCtx = ctx;
    }
#endif

    bool SerialCatPort::setIQMode(bool enabled) {
        std::string cmd = enabled ? "Q91;" : "Q90;";
        if (poller.isRunning()) {
            return poller.enqueueCommand(std::move(cmd)).get();
        }
        return sendCommand(cmd);
    }

    bool SerialCatPort::setFrequency(std::int64_t frequency, int vfo) {
        char buf[32];
        const char* prefix = (vfo == 1) ? "FB" : "FA";
        std::snprintf(buf, sizeof(buf), "%s%011lld;", prefix, static_cast<long long>(frequency));
        std::string cmd = buf;
        if (poller.isRunning()) {
            return poller.enqueueCommand(std::move(cmd)).get();
        }
        return sendCommand(cmd);
    }

    bool SerialCatPort::setMode(QmxMode mode) {
        std::string command;
        if (!encodeModeCommand(mode, command)) {
            return false;
        }
        if (poller.isRunning()) {
            return poller.enqueueCommand(std::move(command)).get();
        }
        return sendCommand(command);
    }

    std::vector<SerialPortInfo> SerialCatPort::listPorts() {
        std::vector<SerialPortInfo> ports;

#ifdef _WIN32
        char target[16];
        char resolved[256];
        for (int i = 1; i <= 256; ++i) {
            std::snprintf(target, sizeof(target), "COM%d", i);
            if (QueryDosDeviceA(target, resolved, sizeof(resolved)) != 0) {
                ports.push_back({ target, target });
            }
        }
#else
        static const std::array<const char*, 6> prefixes = {
            "/dev/ttyACM",
            "/dev/ttyUSB",
            "/dev/cu.usbmodem",
            "/dev/cu.usbserial",
            "/dev/tty.usbmodem",
            "/dev/tty.usbserial"
        };

        for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
            if (!entry.is_character_file()) {
                continue;
            }

            std::string path = entry.path().string();
            for (const char* prefix : prefixes) {
                if (path.rfind(prefix, 0) == 0) {
                    ports.push_back({ path, path });
                    break;
                }
            }
        }

        std::sort(ports.begin(), ports.end(), [](const SerialPortInfo& lhs, const SerialPortInfo& rhs) {
            return lhs.path < rhs.path;
        });
#endif

        return ports;
    }

    bool SerialCatPort::sendCommand(const std::string& command) {
        if (!handle)
            return false;

#ifdef _WIN32
#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        const std::uint64_t writeStartUs = qmxCatDebugNowUs();
#endif
        DWORD written = 0;
        if (!WriteFile(reinterpret_cast<HANDLE>(handle), command.data(), static_cast<DWORD>(command.size()), &written, nullptr)) {
#if QMX_CAT_RAW_LOG
            if (debugParser) {
                debugParser->noteError("CAT write failed: WriteFile error " + std::to_string(static_cast<unsigned long long>(GetLastError())));
            }
#endif
            return false;
        }
#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        const std::uint64_t writeDoneUs = qmxCatDebugNowUs();
#endif
        if (written != command.size()) {
#if QMX_CAT_RAW_LOG
            if (debugParser) {
                debugParser->noteError("CAT write failed: short write");
            }
#endif
            return false;
        }
        FlushFileBuffers(reinterpret_cast<HANDLE>(handle));
#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        const std::uint64_t flushDoneUs = qmxCatDebugNowUs();
        if (debugParser) {
            debugParser->noteWrite(command, writeStartUs, writeDoneUs, flushDoneUs);
        }
#endif
        return true;
#else
#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        const std::uint64_t writeStartUs = qmxCatDebugNowUs();
#endif
        ssize_t written = ::write(static_cast<int>(reinterpret_cast<intptr_t>(handle)), command.data(), command.size());
#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        const std::uint64_t writeDoneUs = qmxCatDebugNowUs();
#endif
        if (written != static_cast<ssize_t>(command.size())) {
#if QMX_CAT_RAW_LOG
            if (debugParser) {
                debugParser->noteError(std::string("CAT write failed: ") + std::strerror(errno));
            }
#endif
            return false;
        }
        tcdrain(static_cast<int>(reinterpret_cast<intptr_t>(handle)));
#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        const std::uint64_t flushDoneUs = qmxCatDebugNowUs();
        if (debugParser) {
            debugParser->noteWrite(command, writeStartUs, writeDoneUs, flushDoneUs);
        }
#endif
        return true;
#endif
    }

    std::size_t SerialCatPort::readBytes(char* buffer, std::size_t size) {
        if (!handle || size == 0)
            return 0;

#ifdef _WIN32
        DWORD bytesRead = 0;
        if (!ReadFile(reinterpret_cast<HANDLE>(handle), buffer, static_cast<DWORD>(size), &bytesRead, nullptr)) {
#if QMX_CAT_RAW_LOG
            if (debugParser) {
                debugParser->noteError("CAT read failed: ReadFile error " + std::to_string(static_cast<unsigned long long>(GetLastError())));
            }
#endif
            return 0;
        }
        return static_cast<std::size_t>(bytesRead);
#else
        ssize_t bytesRead = ::read(static_cast<int>(reinterpret_cast<intptr_t>(handle)), buffer, size);
        if (bytesRead < 0) {
#if QMX_CAT_RAW_LOG
            if (debugParser) {
                debugParser->noteError(std::string("CAT read failed: ") + std::strerror(errno));
            }
#endif
            return 0;
        }
        if (bytesRead == 0) {
            return 0;
        }
        return static_cast<std::size_t>(bytesRead);
#endif
    }

}
