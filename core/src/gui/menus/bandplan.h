#pragma once
#include <module.h>

namespace bandplanmenu {
    void init();
    void draw(void* ctx);
    ModuleManager::Instance* getInstance();
};
