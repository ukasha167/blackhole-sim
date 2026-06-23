#pragma once

#include <SDL3/SDL.h>

namespace bhs {

struct CameraState;
struct DiskState;

class Renderer {
public:
    bool init(const char* title, int width, int height);
    void renderFrame(const CameraState& camera, const DiskState& disk, double elapsedTime);
    void shutdown();

private:
    bool createGPUDevice();
    bool createComputePipeline();
    bool createRenderTarget(int width, int height);
    void destroyRenderTarget();

    // Dynamic resolution scaffold (per the earlier architecture flag on
    // "locked 60fps" being a control loop, not a property of fast code).
    // Logic is deliberately crude right now - wall-clock CPU timing around
    // the dispatch, not real GPU timestamp queries, so it includes CPU
    // stalls and will lie to you somewhat. Replace with SDL_GPU timestamp
    // queries once that's the bottleneck worth chasing.
    void updateDynamicResolution(double lastFrameSeconds);

    SDL_Window*             window_         = nullptr;
    SDL_GPUDevice*          device_          = nullptr;
    SDL_GPUComputePipeline* computePipeline_ = nullptr;
    SDL_GPUTexture*         renderTarget_    = nullptr;

    int   windowWidth_  = 0;
    int   windowHeight_ = 0;
    int   renderWidth_  = 0;
    int   renderHeight_ = 0;
    float renderScale_  = 1.0f;
    static constexpr float kMinRenderScale = 0.5f;
    static constexpr float kMaxRenderScale = 1.0f;
};

} // namespace bhs
