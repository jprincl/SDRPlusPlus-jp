#pragma once
#ifdef __ANDROID__

namespace androidmenu {
    void init();
    void draw(void* ctx);

    // Back with nothing left to dismiss: true (default) opens the exit
    // confirmation dialog, false moves the app to the background.
    extern bool confirmExitOnBack;
}

#endif
