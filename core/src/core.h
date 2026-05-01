#pragma once
#include <config.h>
#include <module.h>
#include <module_com.h>
#include <string>
#include "command_args.h"

namespace core {
    SDRPP_EXPORT ConfigManager configManager;
    SDRPP_EXPORT ModuleManager moduleManager;
    SDRPP_EXPORT ModuleComManager modComManager;
    SDRPP_EXPORT CommandArgsParser args;

    void setInputSampleRate(double samplerate);

    // Effective resource paths. Returns the value from configManager.conf,
    // except under AppImage builds (BUILD_APPIMAGE) on Linux when $APPDIR
    // is set — in which case the bundled paths inside the AppImage mount
    // are returned. The accessor pattern keeps the FUSE mount point out
    // of the persisted config file.
    SDRPP_EXPORT std::string getModulesDirectory();
    SDRPP_EXPORT std::string getResourcesDirectory();
};

int sdrpp_main(int argc, char* argv[]);