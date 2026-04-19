#pragma once
#include <string>

namespace backend {
    int init(std::string resDir = "");
    float getContentScale();
    void setUserScaleFactor(float factor);
    void beginFrame();
    void render(bool vsync = true);
    void getMouseScreenPos(double& x, double& y);
    void setMouseScreenPos(double x, double y);
    int renderLoop();
    int end();
}