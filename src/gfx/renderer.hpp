#pragma once

#include <dispatch/dispatch.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gfx/device.hpp"
#include "gfx/gpu_timer.hpp"
#include "gfx/hud.hpp"
#include "physics/freefall.hpp"

namespace bhs {

struct FrameStats {
    double        cpuMs      = 0.0;  // CPU FRAME TIME.
    double        frameMs    = 0.0;  // WALL CLOCK FRAME TIME.
    double        fps        = 0.0;
    std::uint64_t frameIndex = 0;
};

// PIPELINE: COMPUTE -> HDR RGBA16F -> COMPOSITE (TONEMAP + HUD) -> DRAWABLE.
class Renderer {
public:
    // TABLE MAPS MEMBER POINTERS. BUILT AT CTOR SO CLI WORKS WITHOUT METAL.
    Renderer() { buildTunables(); }

    static constexpr std::uint32_t kFramesInFlight = GpuTimer::kFramesInFlight;

    // GEOMETRIC UNITS. M = 1.
    struct Scene {
        float spin          = 0.998f;  // A/M.
        float mass          = 1.0f;
        float cameraRadius  = 34.0f;
        float cameraTheta   = 1.4208f; // ~8.6 DEG ABOVE EQUATOR.
        float cameraPhi     = 0.0f;
        float fovYDegrees   = 40.0f;
        float escapeRadius  = 1000.0f;
        float tolerance     = 1.0e-5f;
        float maxSteps      = 9000.0f;

        // ACCRETION DISK TUNABLES. INNER PINNED TO ISCO.
        float diskOuter        = 20.0f;
        float diskScaleHeight  = 0.075f;  // H = H*R GAS THICKNESS.
        float peakTemperature  = 11000.0f;  // MOVIE USED WEAK 4500K.
        float diskBrightness   = 0.45f;
        float diskOpacity      = 1.60f;   // NEAR SIDE MUST ECLIPSE FAR SIDE.
        float turbulenceScale  = 0.68f;   // RADIAL SCALE.
        float turbulenceAmount = 0.95f;
        float turbulenceShear  = 0.12f;
        float turbulenceAzimuth = 1.60f;  // AZIMUTHAL COARSE SCALE.
        float turbulenceRidge   = 0.28f;  // 0 = SOFT, 1 = CREASED.
        float diskHotspots      = 0.90f;
        float diskInnerGlow     = 2.40f;  // PLUNGE REGION BRIGHTNESS.
        float turbulenceWarp    = 1.15f;   // DOMAIN WARP.
        float turbulenceVoid    = 0.24f;   // VOID CUTOFF.
        float diskInflow        = 0.055f;  // INFLOW DRIFT.
        float skyGlow           = 0.55f;   // MILKY WAY BAND BRIGHTNESS.

        // GALAXY ORIENTATION AND SHAPE.
        float skyTilt        = 62.0f;   // POLE TILT DEG.
        float skyYaw         = 150.0f;  // POLE YAW DEG.
        float skyCore        = 40.0f;   // CORE LONGITUDE DEG.
        float skyDust        = 2.10f;   // DUST LANE OPACITY.
        float skyWidth       = 3.40f;   // BAND WIDTH DEG.
        float skyBulge       = 0.42f;   // BULGE ANGULAR SIZE.

        // FREE FALL TRAJECTORY TUNABLES.
        float fallTheta      = 1.2000f;   // RELEASE THETA ~21 DEG. RADIUS FROM CAMERA.
        float fallInward     = 0.070f;    // INITIAL U^R INWARD.
        float fallThetaRate  = 0.0010f;   // INITIAL U^THETA TOWARD PLANE.
        float fallSpeed      = 9.0f;      // TAU PER WALL SECOND.
        float fallTargetMs   = 26.0f;     // TARGET MS FRAME BUDGET.
        float fallFadeFrom   = 0.55f;     // HORIZON FADE FRACTION.
        float fallFov        = 115.0f;    // HORIZON FOV DEG.

        // MOTION SCALES. DISK ORBIT TIME AND CAMERA AZIMUTH SLEW.
        float diskTimeScale    = 18.0f;
        float cameraOrbitRate  = 0.025f;   // RAD/S CAMERA AZIMUTH SWEEP.

        // POST-PROCESS COLOR GRADE.
        float exposure    = 2.00f;
        float saturation  = 1.30f;
        float gradePower  = 1.25f;
        float bloomStrength = 0.45f;
        float bloomRadius   = 1.6f;
        float vignette      = 0.68f;
        float grain         = 0.022f;

        // DIAGNOSTIC OVERLAY CHANNEL. 0 IS RENDER.
        float diagnostic    = 0.0f;
    };

    Scene& scene() { return scene_; }

    // PROCESS KEY INPUT. GEOMETRY KEYS INVALIDATE LENS MAP.
    void handleKey(int keycode);

    // SET TUNABLE BY HUD NAME.
    bool setTunable(const char* name, float value);
    void setRenderScale(float scale);
    float renderScale() const { return renderScale_; }

    // REBUILD LENS MAP. CALL ON CAMERA OR SPIN CHANGE.
    void invalidateLens() { lensRow_ = 0; lensComplete_ = false; }
    bool lensComplete() const { return lensComplete_; }

    void startFall();
    void stopFall();

    // ADVANCE FALL TAU OFFLINE FOR REPEATABLE SCREENSHOT.
    void advanceFall(double dTau);
    bool falling() const { return falling_; }

    bool init(Device& device, std::uint32_t widthPx, std::uint32_t heightPx);
    void shutdown();

    void resize(std::uint32_t widthPx, std::uint32_t heightPx);
    void render(double elapsedSeconds, const FrameStats& stats);

    // CAPTURE FRAME TO PPM.
    bool capture(const char* path, double elapsedSeconds, const FrameStats& stats);

    Hud&            hud()   { return hud_; }
    const GpuTimer& timer() const { return timer_; }

    // TOTAL COMMAND BUFFER TIME FROM METAL API CROSS-CHECK.
    double commandBufferGpuMs() const { return commandBufferGpuMs_.load(std::memory_order_relaxed); }

private:
    bool createPipelines();
    bool createHdrTarget();
    bool createLensTargets();
    bool createDiskTables();
    bool createBloomChain();
    bool createSkyCube();
    void buildTunables();
    void applyTuning(int direction, float scale);
    bool savePreset() const;
    bool loadPreset();
    void encodeBloom(MTL::CommandBuffer* cmd);
    void fillDiskUniforms(void* uniforms, double elapsedSeconds) const;
    void fillSceneUniforms(void* uniforms, double elapsedSeconds, std::uint32_t rowOffset) const;
    void encodeLensBuild(MTL::CommandBuffer* cmd, double elapsedSeconds);
    void writeHud(const FrameStats& stats);

    void encodeShade(MTL::CommandBuffer* cmd, double elapsedSeconds);
    void encodeComposite(MTL::CommandBuffer* cmd, MTL::Texture* target, std::uint32_t slot);

    Device*   device_ = nullptr;
    GpuTimer  timer_;
    Hud       hud_;
    Scene     scene_;

    MTL::ComputePipelineState* lensBuildPipeline_     = nullptr;
    MTL::ComputePipelineState* lensFootprintPipeline_ = nullptr;
    MTL::ComputePipelineState* shadePipeline_         = nullptr;
    MTL::ComputePipelineState* bloomDownPipeline_     = nullptr;
    MTL::ComputePipelineState* bloomUpPipeline_       = nullptr;
    MTL::ComputePipelineState* skyBakePipeline_       = nullptr;
    MTL::RenderPipelineState*  compositePipeline_     = nullptr;

    MTL::Texture* lensTarget_      = nullptr;   // SKY DIR + OUTCOME.
    MTL::Texture* crossingTarget_  = nullptr;   // DISK CROSSINGS ARRAY.
    MTL::Texture* footprintTarget_ = nullptr;   // PIXEL SOLID ANGLE RAD.
    MTL::Texture* sweepTarget_     = nullptr;   // DISK SWEEP ARRAY.
    MTL::Texture* profileLut_      = nullptr;   // PAGE-THORNE TEMP LUT.
    MTL::Texture* blackbodyLut_    = nullptr;   // BLACKBODY SRGB LUT.
    MTL::Texture* hdrTarget_       = nullptr;
    MTL::Texture* skyCube_         = nullptr;   // BAKED MILKY WAY CUBE.
    MTL::Texture* skyCubeArray_    = nullptr;   // 2D-ARRAY VIEW FOR COMPUTE WRITE.

    // EXPLICIT TEXTURE CHAIN PER BLOOM MIP.
    MTL::Texture* bloomChain_[BHS_BLOOM_LEVELS]{};
    std::uint32_t bloomWidth_[BHS_BLOOM_LEVELS]{};
    std::uint32_t bloomHeight_[BHS_BLOOM_LEVELS]{};
    std::uint32_t bloomLevels_ = 0;

    struct Tunable {
        const char* name;
        float*      value;
        float       step;
        float       minimum;
        float       maximum;
        bool        rebuildLens;
        bool        rebuildDisk;
        bool        rebuildSky = false;
    };
    std::vector<Tunable> tunables_;
    std::size_t          selected_ = 0;
    bool                 showExhausted_ = false;
    const char*          statusText_ = "";

    // FALL STATE. LENS MAP REBUILT PER FRAME AT DYNAMIC SCALE.
    bool          falling_     = false;
    bool          fallEnded_   = false;
    FreeFallState fall_{};
    FreeFallFrame fallFrame_{};
    float         fallScale_   = 0.42f;

    float cameraAzimuth_   = 0.0f;   // CAMERA AZIMUTH. CHEAP ROTATION.
    float diskInnerRadius_ = 0.0f;   // ISCO RADIUS.
    float diskPeakRadius_  = 0.0f;

    // PROGRESSIVE LENS BUILD IN HORIZONTAL BANDS.
    std::uint32_t lensRow_        = 0;
    std::uint32_t lensRowsPerFrame_ = 96;
    bool          lensComplete_  = false;

    dispatch_semaphore_t inFlight_ = nullptr;

    // WRITTEN FROM METAL COMPLETION HANDLER.
    std::atomic<double> commandBufferGpuMs_{0.0};

    // RETINA DRAWABLE SIZES. LENS MAP RESOLUTION.
    std::uint32_t drawableWidth_  = 0;
    std::uint32_t drawableHeight_ = 0;
    std::uint32_t renderWidth_    = 0;
    std::uint32_t renderHeight_   = 0;
    float         renderScale_    = 1.0f;
    std::uint64_t frameIndex_     = 0;

    MTL::Size threadgroupSize_{8, 8, 1};
};

} // NAMESPACE BHS

