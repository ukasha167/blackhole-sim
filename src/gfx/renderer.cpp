#include "gfx/renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <cstdlib>
#include <cstring>
#include <string>

#include "physics/luts.hpp"
#include "shaders/common.h"

namespace bhs {

namespace {

MTL::Function* loadFunction(MTL::Library* library, const char* name) {
    NS::String* fnName = NS::String::string(name, NS::UTF8StringEncoding);
    MTL::Function* fn = library->newFunction(fnName);
    if (!fn) {
        SDL_Log("Shader function '%s' not found in the metallib.", name);
    }
    return fn;
}

} // NAMESPACE

namespace {

MTL::ComputePipelineState* makeCompute(MTL::Device* device, MTL::Library* library,
                                       const char* name, MTL::Size& outThreadgroup) {
    MTL::Function* fn = loadFunction(library, name);
    if (!fn) {
        return nullptr;
    }
    NS::Error* error = nullptr;
    MTL::ComputePipelineState* pipeline = device->newComputePipelineState(fn, &error);
    fn->release();
    if (!pipeline) {
        SDL_Log("Compute pipeline '%s' failed: %s", name,
                error ? error->localizedDescription()->utf8String() : "unknown");
        return nullptr;
    }
    // QUERY SIMD WIDTH FROM PIPELINE.
    const NS::UInteger simdWidth = pipeline->threadExecutionWidth();
    const NS::UInteger maxThreads = pipeline->maxTotalThreadsPerThreadgroup();
    outThreadgroup = MTL::Size(simdWidth, std::max<NS::UInteger>(1, maxThreads / simdWidth), 1);
    return pipeline;
}

} // NAMESPACE

bool Renderer::createPipelines() {
    MTL::Library* library = device_->library();
    MTL::Device* device = device_->mtl();

    MTL::Size shape;
    lensBuildPipeline_ = makeCompute(device, library, "lens_build", shape);
    if (!lensBuildPipeline_) return false;
    threadgroupSize_ = shape;

    lensFootprintPipeline_ = makeCompute(device, library, "lens_footprint", shape);
    if (!lensFootprintPipeline_) return false;

    shadePipeline_ = makeCompute(device, library, "shade_main", shape);
    if (!shadePipeline_) return false;

    bloomDownPipeline_ = makeCompute(device, library, "bloom_downsample", shape);
    if (!bloomDownPipeline_) return false;

    bloomUpPipeline_ = makeCompute(device, library, "bloom_upsample", shape);
    if (!bloomUpPipeline_) return false;

    skyBakePipeline_ = makeCompute(device, library, "milkyway_bake", shape);
    if (!skyBakePipeline_) return false;

    MTL::Function* vertexFn   = loadFunction(library, "composite_vertex");
    MTL::Function* fragmentFn = loadFunction(library, "composite_fragment");
    if (!vertexFn || !fragmentFn) {
        if (vertexFn)   vertexFn->release();
        if (fragmentFn) fragmentFn->release();
        return false;
    }

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vertexFn);
    desc->setFragmentFunction(fragmentFn);
    desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);

    NS::Error* error = nullptr;
    compositePipeline_ = device->newRenderPipelineState(desc, &error);

    desc->release();
    vertexFn->release();
    fragmentFn->release();

    if (!compositePipeline_) {
        SDL_Log("Render pipeline failed: %s",
                error ? error->localizedDescription()->utf8String() : "unknown");
        return false;
    }
    return true;
}

// LENS MAP RGBA32F. FULL PRECISION PREVENTS STAR FIELD STEPPING.
bool Renderer::createLensTargets() {
    for (MTL::Texture** slot : {&lensTarget_, &footprintTarget_}) {
        if (*slot) { (*slot)->release(); *slot = nullptr; }
    }

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setPixelFormat(MTL::PixelFormatRGBA32Float);
    desc->setWidth(renderWidth_);
    desc->setHeight(renderHeight_);
    desc->setMipmapLevelCount(1);
    desc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    desc->setStorageMode(MTL::StorageModePrivate);
    lensTarget_ = device_->mtl()->newTexture(desc);

    // JACOBIAN COLUMNS FOR ANISOTROPIC PIXEL FOOTPRINT.
    desc->setTextureType(MTL::TextureType2DArray);
    desc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    desc->setArrayLength(2);
    footprintTarget_ = device_->mtl()->newTexture(desc);

    // CROSSING RECORDS PACKED IN HALF FLOATS.
    if (crossingTarget_) { crossingTarget_->release(); crossingTarget_ = nullptr; }
    desc->setArrayLength(BHS_MAX_CROSSINGS);
    crossingTarget_ = device_->mtl()->newTexture(desc);

    // PER-CROSSING RADIAL AND PHASE PIXEL SWEEP.
    if (sweepTarget_) { sweepTarget_->release(); sweepTarget_ = nullptr; }
    desc->setPixelFormat(MTL::PixelFormatRG16Float);
    sweepTarget_ = device_->mtl()->newTexture(desc);
    desc->release();

    if (!lensTarget_ || !footprintTarget_ || !crossingTarget_ || !sweepTarget_) {
        SDL_Log("Could not allocate the lens map.");
        return false;
    }

    invalidateLens();
    return true;
}

bool Renderer::createHdrTarget() {
    renderWidth_  = std::max(1u, static_cast<std::uint32_t>(drawableWidth_  * renderScale_));
    renderHeight_ = std::max(1u, static_cast<std::uint32_t>(drawableHeight_ * renderScale_));

    if (hdrTarget_) {
        hdrTarget_->release();
        hdrTarget_ = nullptr;
    }

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    // RGBA16F CARRIES UNCLAMPED HDR VALUES.
    desc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    desc->setWidth(renderWidth_);
    desc->setHeight(renderHeight_);
    desc->setMipmapLevelCount(1);
    desc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    desc->setStorageMode(MTL::StorageModePrivate);

    hdrTarget_ = device_->mtl()->newTexture(desc);
    desc->release();

    if (!hdrTarget_) {
        SDL_Log("Could not allocate the HDR render target.");
        return false;
    }
    if (!createBloomChain()) {
        return false;
    }
    return createLensTargets();
}

bool Renderer::createBloomChain() {
    for (auto*& tex : bloomChain_) {
        if (tex) { tex->release(); tex = nullptr; }
    }

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    desc->setMipmapLevelCount(1);
    desc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    desc->setStorageMode(MTL::StorageModePrivate);

    std::uint32_t w = renderWidth_ / 2;
    std::uint32_t h = renderHeight_ / 2;
    bloomLevels_ = 0;

    for (std::uint32_t i = 0; i < BHS_BLOOM_LEVELS && w >= 8 && h >= 8; ++i) {
        desc->setWidth(w);
        desc->setHeight(h);
        bloomChain_[i] = device_->mtl()->newTexture(desc);
        if (!bloomChain_[i]) {
            desc->release();
            SDL_Log("Could not allocate bloom level %u.", i);
            return false;
        }
        bloomWidth_[i] = w;
        bloomHeight_[i] = h;
        ++bloomLevels_;
        w /= 2;
        h /= 2;
    }
    desc->release();
    return bloomLevels_ >= 2;
}

// BAKE MILKY WAY CUBEMAP ONCE VIA 2D ARRAY VIEW.
// CUBE SAMPLING GIVES SEAMLESS FILTERING ACROSS EDGES.
bool Renderer::createSkyCube() {
    // 1024 CUBE FACE WITH MIP CHAIN TO PREVENT ALIASING.
    constexpr std::uint32_t kFace = 1024;

    for (MTL::Texture** slot : {&skyCubeArray_, &skyCube_}) {
        if (*slot) { (*slot)->release(); *slot = nullptr; }
    }

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureTypeCube);
    desc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    desc->setWidth(kFace);
    desc->setHeight(kFace);
    std::uint32_t levels = 1;
    while ((kFace >> levels) >= 1u) { ++levels; }
    desc->setMipmapLevelCount(levels);
    desc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite |
                   MTL::TextureUsagePixelFormatView);
    desc->setStorageMode(MTL::StorageModePrivate);
    skyCube_ = device_->mtl()->newTexture(desc);
    desc->release();

    if (!skyCube_) {
        SDL_Log("Could not allocate the sky cube.");
        return false;
    }

    skyCubeArray_ = skyCube_->newTextureView(MTL::PixelFormatRGBA16Float,
                                             MTL::TextureType2DArray,
                                             NS::Range(0, 1), NS::Range(0, 6));
    if (!skyCubeArray_) {
        SDL_Log("Could not create the sky cube write view.");
        return false;
    }

    DiskUniforms uniforms{};
    fillDiskUniforms(&uniforms, 0.0);

    MTL::CommandBuffer* cmd = device_->queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(skyBakePipeline_);
    enc->setTexture(skyCubeArray_, 0);
    enc->setBytes(&uniforms, sizeof(uniforms), 0);
    enc->dispatchThreadgroups(
        MTL::Size((kFace + threadgroupSize_.width  - 1) / threadgroupSize_.width,
                  (kFace + threadgroupSize_.height - 1) / threadgroupSize_.height, 6),
        MTL::Size(threadgroupSize_.width, threadgroupSize_.height, 1));
    enc->endEncoding();

    MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
    blit->generateMipmaps(skyCube_);
    blit->endEncoding();

    cmd->commit();
    cmd->waitUntilCompleted();

    return true;
}

bool Renderer::createDiskTables() {
    for (MTL::Texture** slot : {&profileLut_, &blackbodyLut_}) {
        if (*slot) { (*slot)->release(); *slot = nullptr; }
    }

    const DiskProfile profile = buildDiskProfile(scene_.spin, scene_.mass, scene_.diskOuter);
    diskInnerRadius_ = profile.innerRadius;
    diskPeakRadius_  = profile.peakRadius;

    const std::vector<float> blackbody = buildBlackbodyTable();

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType1D);
    desc->setPixelFormat(MTL::PixelFormatR32Float);
    desc->setWidth(kDiskProfileSamples);
    desc->setHeight(1);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    profileLut_ = device_->mtl()->newTexture(desc);

    desc->setPixelFormat(MTL::PixelFormatRGBA32Float);
    desc->setWidth(kBlackbodySamples);
    blackbodyLut_ = device_->mtl()->newTexture(desc);
    desc->release();

    if (!profileLut_ || !blackbodyLut_) {
        SDL_Log("Could not allocate the disk lookup tables.");
        return false;
    }

    profileLut_->replaceRegion(MTL::Region(0, 0, kDiskProfileSamples, 1), 0,
                               profile.temperature.data(),
                               kDiskProfileSamples * sizeof(float));
    blackbodyLut_->replaceRegion(MTL::Region(0, 0, kBlackbodySamples, 1), 0,
                                 blackbody.data(),
                                 kBlackbodySamples * 4 * sizeof(float));

    SDL_Log("Disk: ISCO %.4f M, peak at %.4f M, outer %.1f M",
            double(profile.innerRadius), double(profile.peakRadius),
            double(scene_.diskOuter));
    return true;
}

void Renderer::fillDiskUniforms(void* out, double elapsedSeconds) const {
    auto* u = static_cast<DiskUniforms*>(out);
    u->radii    = simd_make_float4(diskInnerRadius_, scene_.diskOuter,
                                   scene_.diskScaleHeight, scene_.diskTimeScale);
    u->emission = simd_make_float4(scene_.peakTemperature, scene_.diskBrightness,
                                   scene_.diskOpacity, static_cast<float>(elapsedSeconds));
    u->noise    = simd_make_float4(scene_.turbulenceScale, scene_.turbulenceAmount,
                                   scene_.turbulenceShear,
                                   showExhausted_ ? 1.0f : scene_.diagnostic);
    u->detail   = simd_make_float4(scene_.turbulenceAzimuth, scene_.turbulenceRidge,
                                   scene_.diskHotspots, scene_.diskInnerGlow);
    u->flow     = simd_make_float4(scene_.turbulenceWarp, scene_.turbulenceVoid,
                                   scene_.diskInflow, scene_.skyGlow);
    u->sky      = simd_make_float4(scene_.skyTilt, scene_.skyYaw,
                                   scene_.skyCore, scene_.skyDust);
    u->galaxy   = simd_make_float4(scene_.skyWidth, scene_.skyBulge, 1.0f, 0.0f);
}

void Renderer::buildTunables() {
    // HUD TUNABLES ORDER. GEOMETRY FIRST AS IT FORCES LENS REBUILD.
    tunables_ = {
        {"spin a/M",     &scene_.spin,             0.005f, 0.0f,   0.9999f, true,  true},
        {"camera r",     &scene_.cameraRadius,     0.5f,   4.0f,   400.0f,  true,  false},
        {"camera theta", &scene_.cameraTheta,      0.005f, 0.05f,  3.09f,   true,  false},
        {"fov y",        &scene_.fovYDegrees,      0.5f,   4.0f,   110.0f,  true,  false},
        {"tolerance",    &scene_.tolerance,        1e-6f,  1e-7f,  1e-3f,   true,  false},
        {"max steps",    &scene_.maxSteps,         500.0f, 200.0f, 40000.0f, true, false},
        {"disk outer",   &scene_.diskOuter,        0.5f,   2.0f,   80.0f,   true,  true},
        {"peak temp K",  &scene_.peakTemperature,  250.0f, 1000.0f, 40000.0f, false, false},
        {"disk bright",  &scene_.diskBrightness,   0.01f,  0.0f,   4.0f,    false, false},
        {"disk opacity", &scene_.diskOpacity,      0.02f,  0.0f,   6.0f,    false, false},
        {"thickness",    &scene_.diskScaleHeight,  0.005f, 0.0f,   0.45f,   true,  false},
        {"turbulence",   &scene_.turbulenceAmount, 0.02f,  0.0f,   1.0f,    false, false},
        {"turb scale",   &scene_.turbulenceScale,  0.02f,  0.05f,  4.0f,    false, false},
        {"turb shear",   &scene_.turbulenceShear,  0.005f, 0.0f,   1.0f,    false, false},
        {"turb azimuth", &scene_.turbulenceAzimuth, 0.05f, 0.2f,   12.0f,   false, false},
        {"turb ridge",   &scene_.turbulenceRidge,  0.02f,  0.0f,   1.0f,    false, false},
        {"hotspots",     &scene_.diskHotspots,     0.02f,  0.0f,   3.0f,    false, false},
        {"inner glow",   &scene_.diskInnerGlow,    0.05f,  0.0f,   12.0f,   false, false},
        {"warp",         &scene_.turbulenceWarp,   0.05f,  0.0f,   5.0f,    false, false},
        {"voids",        &scene_.turbulenceVoid,   0.02f,  0.0f,   0.9f,    false, false},
        {"inflow",       &scene_.diskInflow,       0.005f, -0.5f,  0.5f,    false, false},
        {"sky glow",     &scene_.skyGlow,          0.02f,  0.0f,   3.0f,    false, false},
        {"sky tilt",     &scene_.skyTilt,          2.0f,   0.0f,   180.0f,  false, false, true},
        {"sky yaw",      &scene_.skyYaw,           2.0f,  -360.0f, 360.0f,  false, false, true},
        {"sky core",     &scene_.skyCore,          2.0f,  -360.0f, 360.0f,  false, false, true},
        {"sky dust",     &scene_.skyDust,          0.05f,  0.0f,   6.0f,    false, false, true},
        {"sky width",    &scene_.skyWidth,         0.1f,   0.5f,   20.0f,   false, false, true},
        {"sky bulge",    &scene_.skyBulge,         0.02f,  0.05f,  2.0f,    false, false, true},
        {"fall speed",   &scene_.fallSpeed,        0.5f,   0.5f,   60.0f,   false, false},
        {"fall budget",  &scene_.fallTargetMs,     1.0f,   6.0f,   40.0f,   false, false},
        {"fall fov",     &scene_.fallFov,          2.0f,   20.0f,  170.0f,  false, false},
        {"fall start th",&scene_.fallTheta,        0.005f, 0.05f,  3.09f,   false, false},
        {"fall inward",  &scene_.fallInward,       0.005f, 0.0f,   0.9f,    false, false},
        {"fall dtheta",  &scene_.fallThetaRate,    0.0002f, -0.05f, 0.05f,  false, false},
        {"time scale",   &scene_.diskTimeScale,    0.25f,  0.0f,   60.0f,   false, false},
        {"orbit rate",   &scene_.cameraOrbitRate,  0.005f, -1.0f,  1.0f,    false, false},
        {"exposure",     &scene_.exposure,         0.05f,  0.05f,  20.0f,   false, false},
        {"saturation",   &scene_.saturation,       0.02f,  0.0f,   3.0f,    false, false},
        {"grade power",  &scene_.gradePower,       0.02f,  0.3f,   3.0f,    false, false},
        {"bloom",        &scene_.bloomStrength,    0.01f,  0.0f,   2.0f,    false, false},
        {"bloom radius", &scene_.bloomRadius,      0.05f,  0.2f,   6.0f,    false, false},
        {"vignette",     &scene_.vignette,         0.02f,  0.0f,   2.0f,    false, false},
        {"grain",        &scene_.grain,            0.002f, 0.0f,   0.2f,    false, false},
        {"diag",         &scene_.diagnostic,       1.0f,   0.0f,   12.0f,   false, false},
    };
}

bool Renderer::setTunable(const char* name, float value) {
    for (Tunable& t : tunables_) {
        if (std::strcmp(t.name, name) != 0) {
            continue;
        }
        *t.value = std::min(t.maximum, std::max(t.minimum, value));
        if (t.rebuildDisk) {
            createDiskTables();
        }
        if (t.rebuildSky) {
            createSkyCube();
        }
        if (t.rebuildLens) {
            invalidateLens();
        }
        return true;
    }
    return false;
}

void Renderer::applyTuning(int direction, float scale) {
    if (tunables_.empty() || selected_ >= tunables_.size()) {
        return;
    }
    Tunable& t = tunables_[selected_];
    const float next = std::min(t.maximum,
                                std::max(t.minimum,
                                         *t.value + direction * t.step * scale));
    if (next == *t.value) {
        return;
    }
    *t.value = next;

    if (t.rebuildDisk) {
        createDiskTables();
    }
    if (t.rebuildSky) {
        createSkyCube();
    }
    if (t.rebuildLens) {
        invalidateLens();
    }
    statusText_ = t.rebuildLens ? "rebuilding lens" : "";
}

bool Renderer::savePreset() const {
    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "blackhole.preset";

    std::FILE* file = std::fopen(path.c_str(), "w");
    if (!file) {
        return false;
    }
    std::fprintf(file, "# blackhole preset\n");
    for (const Tunable& t : tunables_) {
        std::fprintf(file, "%s = %.6f\n", t.name, double(*t.value));
    }
    std::fclose(file);
    SDL_Log("Preset saved to %s", path.c_str());
    return true;
}

bool Renderer::loadPreset() {
    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "blackhole.preset";

    std::FILE* file = std::fopen(path.c_str(), "r");
    if (!file) {
        return false;
    }

    char line[256];
    while (std::fgets(line, sizeof(line), file)) {
        if (line[0] == '#') {
            continue;
        }
        char* equals = std::strchr(line, '=');
        if (!equals) {
            continue;
        }
        *equals = '\0';

        // STRIP TRAILING SPACES BEFORE EQUALS SIGN.
        std::size_t len = std::strlen(line);
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
            line[--len] = '\0';
        }

        for (Tunable& t : tunables_) {
            if (std::strcmp(t.name, line) == 0) {
                *t.value = std::min(t.maximum,
                                    std::max(t.minimum,
                                             std::strtof(equals + 1, nullptr)));
                break;
            }
        }
    }
    std::fclose(file);

    createDiskTables();
    invalidateLens();
    SDL_Log("Preset loaded from %s", path.c_str());
    return true;
}

void Renderer::handleKey(int keycode) {
    switch (keycode) {
        case SDLK_UP:    selected_ = (selected_ + tunables_.size() - 1) % tunables_.size(); break;
        case SDLK_DOWN:  selected_ = (selected_ + 1) % tunables_.size(); break;
        case SDLK_LEFT:  applyTuning(-1, 1.0f); break;
        case SDLK_RIGHT: applyTuning( 1, 1.0f); break;
        case SDLK_LEFTBRACKET:  applyTuning(-1, 10.0f); break;
        case SDLK_RIGHTBRACKET: applyTuning( 1, 10.0f); break;
        case SDLK_S: statusText_ = savePreset() ? "preset saved" : "save failed"; break;
        case SDLK_L: statusText_ = loadPreset() ? "preset loaded" : "no preset file"; break;
        case SDLK_F: if (falling_) { stopFall(); } else { startFall(); } break;
        case SDLK_D: showExhausted_ = !showExhausted_;
                     statusText_ = showExhausted_ ? "diagnostics on" : "diagnostics off";
                     invalidateLens();
                     break;
        default: break;
    }
}

void Renderer::startFall() {
    fall_ = freeFallFromVelocity(scene_.spin, scene_.mass,
                                 scene_.cameraRadius, scene_.fallTheta,
                                 -scene_.fallInward, scene_.fallThetaRate, 0.0);
    fallFrame_ = freeFallFrame(fall_, scene_.spin, scene_.mass);
    falling_ = true;
    fallEnded_ = false;
    statusText_ = "falling";
    invalidateLens();
}

void Renderer::advanceFall(double dTau) {
    if (!falling_) {
        return;
    }
    // STEP FREE-FALL GEODESIC CHUNKS.
    constexpr double kChunk = 0.25;
    for (double done = 0.0; done < dTau && !fallEnded_; done += kChunk) {
        const FreeFallState previous = fall_;
        if (!freeFallAdvance(fall_, scene_.spin, scene_.mass,
                             std::min(kChunk, dTau - done))) {
            fall_ = previous;
            fallEnded_ = true;
            statusText_ = "horizon crossed";
            break;
        }
    }
    fallFrame_ = freeFallFrame(fall_, scene_.spin, scene_.mass);
    invalidateLens();
}

void Renderer::stopFall() {
    falling_ = false;
    fallEnded_ = false;
    statusText_ = "";
    setRenderScale(1.0f);
    invalidateLens();
}

void Renderer::setRenderScale(float scale) {
    const float clamped = std::min(1.0f, std::max(0.1f, scale));
    if (std::fabs(clamped - renderScale_) < 1e-4f) {
        return;
    }
    renderScale_ = clamped;
    createHdrTarget();
}

bool Renderer::init(Device& device, std::uint32_t widthPx, std::uint32_t heightPx) {
    device_ = &device;

    drawableWidth_  = widthPx;
    drawableHeight_ = heightPx;

    if (!createPipelines())       return false;
    if (!createSkyCube())         return false;
    if (!createDiskTables())      return false;
    if (!createHdrTarget())       return false;
    if (!hud_.init(device.mtl())) return false;

    // GPU COUNTER SAMPLING OPTIONAL. FAILS GRACEFULLY.
    timer_.init(device.mtl());

    // LOAD SAVED CONFIG PRESET IF PRESENT.
    loadPreset();

    inFlight_ = dispatch_semaphore_create(kFramesInFlight);
    return inFlight_ != nullptr;
}

void Renderer::resize(std::uint32_t widthPx, std::uint32_t heightPx) {
    if (widthPx == 0 || heightPx == 0) {
        return;
    }
    if (widthPx == drawableWidth_ && heightPx == drawableHeight_) {
        return;
    }
    drawableWidth_  = widthPx;
    drawableHeight_ = heightPx;
    createHdrTarget();
    device_->resize(widthPx, heightPx);
}

void Renderer::writeHud(const FrameStats& stats) {
    hud_.clear();

    hud_.print(0, 0,  "BLACKHOLE  M4 grade");
    hud_.print(0, 1,  "%s", device_->name());
    hud_.print(0, 2,  "trace %ux%u  out %ux%u  x%.2f",
               renderWidth_, renderHeight_, drawableWidth_, drawableHeight_,
               static_cast<double>(renderScale_));
    if (lensComplete_) {
        hud_.print(0, 4, "lens     ready");
    } else {
        hud_.print(0, 4, "lens     %3.0f%% built",
                   100.0 * double(lensRow_) / double(renderHeight_ ? renderHeight_ : 1));
    }
    hud_.print(0, 3,  "a/M %.3f   r %.1fM   fov %.0f   steps %.0f",
               static_cast<double>(scene_.spin), static_cast<double>(scene_.cameraRadius),
               static_cast<double>(scene_.fovYDegrees), static_cast<double>(scene_.maxSteps));
    if (falling_) {
        hud_.print(0, 8, "FALL r %6.3fM  th %.3f  v %.4fc  g %.1f",
                   fall_.r, fall_.theta, fallFrame_.speed, fallFrame_.gamma);
    } else {
        hud_.print(0, 8,  "disk %.2f-%.0fM  peak %.2fM  %.0fK",
               static_cast<double>(diskInnerRadius_), static_cast<double>(scene_.diskOuter),
               static_cast<double>(diskPeakRadius_),
               static_cast<double>(scene_.peakTemperature));
    }

    hud_.print(0, 5,  "fps      %6.1f", stats.fps);
    hud_.print(0, 6,  "frame    %6.2f ms", stats.frameMs);
    hud_.print(0, 7,  "cpu      %6.2f ms", stats.cpuMs);

    if (timer_.supported()) {
        hud_.print(0, 9, "gpu      %6.2f ms", timer_.totalMs());
        std::uint32_t row = 10;
        for (std::uint32_t i = 0; i < timer_.passCount() && row < Hud::kRows; ++i, ++row) {
            hud_.print(2, row, "%-10s %6.3f ms", timer_.passLabel(i), timer_.passMs(i));
        }
    } else {
        hud_.print(0, 9, "gpu      counter sampling unavailable");
    }

    // TUNING PANEL ROW SELECTION. HUD SINGLE MONO COLOR.
    const std::size_t visible = 6;
    const std::size_t first = (selected_ >= visible) ? selected_ - visible + 1 : 0;
    std::uint32_t row = 5;
    for (std::size_t i = first; i < tunables_.size() && row < Hud::kRows - 2; ++i, ++row) {
        const Tunable& t = tunables_[i];
        hud_.print(26, row, "%c %-12s %9.3f",
                   i == selected_ ? '>' : ' ', t.name, double(*t.value));
    }

    hud_.print(30, Hud::kRows - 2, "%s", statusText_);
    hud_.print(0, Hud::kRows - 1, "up/dn l/r adjust  [ ] x10  f fall  s save  l load  esc");
}

void Renderer::fillSceneUniforms(void* out, double elapsedSeconds,
                                 std::uint32_t rowOffset) const {
    const float w = static_cast<float>(renderWidth_);
    const float h = static_cast<float>(renderHeight_);

    auto* uniforms = static_cast<SceneUniforms*>(out);
    uniforms->resolution = simd_make_float4(w, h, 1.0f / w, 1.0f / h);
    uniforms->timing     = simd_make_float4(static_cast<float>(elapsedSeconds), 0, 0, 0);
    const float camR     = falling_ ? static_cast<float>(fall_.r) : scene_.cameraRadius;
    const float camTheta = falling_ ? static_cast<float>(fall_.theta) : scene_.cameraTheta;
    const float camPhi   = falling_ ? static_cast<float>(fall_.phi) : cameraAzimuth_;

    // WIDEN FOV DURING INFALL SO HORIZON SHADOW DOES NOT BLIND VIEW.
    float fovY = scene_.fovYDegrees;
    if (falling_) {
        const float horizon = scene_.mass +
            std::sqrt(std::max(scene_.mass * scene_.mass -
                               scene_.spin * scene_.mass * scene_.spin * scene_.mass, 0.0f));
        const float above = (camR - horizon) / std::max(horizon, 1e-3f);
        const float t = std::clamp(1.0f - above / 24.0f, 0.0f, 1.0f);
        fovY = scene_.fovYDegrees + (scene_.fallFov - scene_.fovYDegrees) * t * t;
    }

    uniforms->camera     = simd_make_float4(camR, camTheta, camPhi,
                                            std::tan(0.5f * fovY *
                                                     3.14159265358979f / 180.0f));
    // NEAR HORIZON RAYS RESOLVE FAST. RELAX STEPS AND ESCAPE.
    const float escape = falling_ ? std::min(scene_.escapeRadius, 300.0f)
                                  : scene_.escapeRadius;
    const float tol    = falling_ ? std::max(scene_.tolerance, 6.0e-5f) : scene_.tolerance;
    const float steps  = falling_ ? std::min(scene_.maxSteps, 2500.0f) : scene_.maxSteps;

    uniforms->blackHole  = simd_make_float4(scene_.spin * scene_.mass, scene_.mass,
                                            escape, tol);
    // RELAX DISK STEP BOUNDARY ON REALTIME INFALL TO SAVE FRAME BUDGET.
    const float diskStep = falling_ ? 12.0f : 1.0f;
    uniforms->integrator = simd_make_float4(steps, w / h,
                                            static_cast<float>(rowOffset), diskStep);
    uniforms->boost      = falling_
        ? simd_make_float4(static_cast<float>(fallFrame_.betaR),
                           static_cast<float>(fallFrame_.betaTheta),
                           static_cast<float>(fallFrame_.betaPhi),
                           static_cast<float>(fallFrame_.gamma))
        : simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f);
}

// SOLVE ONE LENS MAP BAND PER FRAME. RUN FOOTPRINT PASS ON FINISH.
void Renderer::encodeLensBuild(MTL::CommandBuffer* cmd, double elapsedSeconds) {
    if (lensComplete_) {
        return;
    }

    const std::uint32_t rows = std::min(lensRowsPerFrame_, renderHeight_ - lensRow_);

    SceneUniforms uniforms{};
    fillSceneUniforms(&uniforms, elapsedSeconds, lensRow_);

    MTL::ComputePassDescriptor* pass = MTL::ComputePassDescriptor::alloc()->init();
    timer_.attachCompute(pass, "lens");
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder(pass);
    pass->release();

    DiskUniforms diskUniforms{};
    fillDiskUniforms(&diskUniforms, elapsedSeconds);

    enc->setComputePipelineState(lensBuildPipeline_);
    enc->setTexture(lensTarget_, 0);
    enc->setTexture(crossingTarget_, 1);
    enc->setBytes(&uniforms, sizeof(uniforms), 0);
    enc->setBytes(&diskUniforms, sizeof(diskUniforms), 1);
    enc->dispatchThreadgroups(
        MTL::Size((renderWidth_ + threadgroupSize_.width - 1) / threadgroupSize_.width,
                  (rows + threadgroupSize_.height - 1) / threadgroupSize_.height, 1),
        threadgroupSize_);
    enc->endEncoding();

    lensRow_ += rows;

    if (lensRow_ >= renderHeight_) {
        SceneUniforms full{};
        fillSceneUniforms(&full, elapsedSeconds, 0);

        MTL::ComputePassDescriptor* fp = MTL::ComputePassDescriptor::alloc()->init();
        timer_.attachCompute(fp, "footprint");
        MTL::ComputeCommandEncoder* fenc = cmd->computeCommandEncoder(fp);
        fp->release();

        DiskUniforms footprintDisk{};
        fillDiskUniforms(&footprintDisk, elapsedSeconds);

        fenc->setComputePipelineState(lensFootprintPipeline_);
        fenc->setTexture(lensTarget_, 0);
        fenc->setTexture(footprintTarget_, 1);
        fenc->setTexture(crossingTarget_, 2);
        fenc->setTexture(sweepTarget_, 3);
        fenc->setBytes(&full, sizeof(full), 0);
        fenc->setBytes(&footprintDisk, sizeof(footprintDisk), 1);
        fenc->dispatchThreadgroups(
            MTL::Size((renderWidth_  + threadgroupSize_.width  - 1) / threadgroupSize_.width,
                      (renderHeight_ + threadgroupSize_.height - 1) / threadgroupSize_.height, 1),
            threadgroupSize_);
        fenc->endEncoding();

        lensComplete_ = true;
    }
}

void Renderer::encodeShade(MTL::CommandBuffer* cmd, double elapsedSeconds) {
    SceneUniforms uniforms{};
    fillSceneUniforms(&uniforms, elapsedSeconds, 0);

    MTL::ComputePassDescriptor* pass = MTL::ComputePassDescriptor::alloc()->init();
    timer_.attachCompute(pass, "shade");
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder(pass);
    pass->release();

    DiskUniforms diskUniforms{};
    fillDiskUniforms(&diskUniforms, elapsedSeconds);

    enc->setComputePipelineState(shadePipeline_);
    enc->setTexture(lensTarget_, 0);
    enc->setTexture(footprintTarget_, 1);
    enc->setTexture(crossingTarget_, 2);
    enc->setTexture(profileLut_, 3);
    enc->setTexture(blackbodyLut_, 4);
    enc->setTexture(hdrTarget_, 5);
    enc->setTexture(skyCube_, 6);
    enc->setTexture(sweepTarget_, 7);
    enc->setBytes(&uniforms, sizeof(uniforms), 0);
    enc->setBytes(&diskUniforms, sizeof(diskUniforms), 1);
    enc->dispatchThreadgroups(
        MTL::Size((renderWidth_  + threadgroupSize_.width  - 1) / threadgroupSize_.width,
                  (renderHeight_ + threadgroupSize_.height - 1) / threadgroupSize_.height, 1),
        threadgroupSize_);
    enc->endEncoding();
}

void Renderer::encodeBloom(MTL::CommandBuffer* cmd) {
    if (bloomLevels_ < 2) {
        return;
    }

    MTL::ComputePassDescriptor* pass = MTL::ComputePassDescriptor::alloc()->init();
    timer_.attachCompute(pass, "bloom");
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder(pass);
    pass->release();

    auto dispatch = [&](std::uint32_t w, std::uint32_t h) {
        enc->dispatchThreadgroups(
            MTL::Size((w + threadgroupSize_.width  - 1) / threadgroupSize_.width,
                      (h + threadgroupSize_.height - 1) / threadgroupSize_.height, 1),
            threadgroupSize_);
    };

    // DOWNSAMPLE CHAIN. FIRST LEVEL USES KARIS AVERAGE TO SUPPRESS FIREFLIES.
    enc->setComputePipelineState(bloomDownPipeline_);
    for (std::uint32_t i = 0; i < bloomLevels_; ++i) {
        BloomUniforms u{};
        u.params = simd_make_float4(static_cast<float>(bloomWidth_[i]),
                                    static_cast<float>(bloomHeight_[i]),
                                    i == 0 ? 1.0f : 0.0f, 0.0f);
        enc->setTexture(i == 0 ? hdrTarget_ : bloomChain_[i - 1], 0);
        enc->setTexture(bloomChain_[i], 1);
        enc->setBytes(&u, sizeof(u), 0);
        dispatch(bloomWidth_[i], bloomHeight_[i]);
        enc->memoryBarrier(MTL::BarrierScopeTextures);
    }

    // UPSAMPLE CHAIN WITH TENT FILTER BLUR ACCUMULATION.
    enc->setComputePipelineState(bloomUpPipeline_);
    for (int i = static_cast<int>(bloomLevels_) - 2; i >= 0; --i) {
        BloomUniforms u{};
        u.params = simd_make_float4(static_cast<float>(bloomWidth_[i]),
                                    static_cast<float>(bloomHeight_[i]),
                                    0.0f, scene_.bloomRadius);
        enc->setTexture(bloomChain_[i + 1], 0);
        enc->setTexture(bloomChain_[i], 1);
        enc->setTexture(bloomChain_[i], 2);
        enc->setBytes(&u, sizeof(u), 0);
        dispatch(bloomWidth_[i], bloomHeight_[i]);
        enc->memoryBarrier(MTL::BarrierScopeTextures);
    }

    enc->endEncoding();
}

void Renderer::encodeComposite(MTL::CommandBuffer* cmd, MTL::Texture* target,
                               std::uint32_t slot) {
    CompositeUniforms uniforms{};
    uniforms.drawable = simd_make_float4(static_cast<float>(drawableWidth_),
                                         static_cast<float>(drawableHeight_),
                                         1.0f / static_cast<float>(drawableWidth_),
                                         1.0f / static_cast<float>(drawableHeight_));
    uniforms.hudRect  = simd_make_float4(24.0f, 24.0f,
                                         hud_.cellWidth(), hud_.cellHeight());
    uniforms.hudGrid  = simd_make_float4(static_cast<float>(Hud::kColumns),
                                         static_cast<float>(Hud::kRows),
                                         static_cast<float>(BHS_ATLAS_COLS),
                                         static_cast<float>(BHS_ATLAS_ROWS));
    // FADE TO BLACK NEAR HORIZON BEFORE BOYER-LINDQUIST COORDINATES DIVERGE.
    float fallFade = 1.0f;
    if (falling_) {
        const double horizon = scene_.mass +
            std::sqrt(std::max(double(scene_.mass * scene_.mass -
                                      scene_.spin * scene_.mass * scene_.spin * scene_.mass), 0.0));
        const double above = (fall_.r - horizon) / horizon;
        fallFade = static_cast<float>(std::clamp(above / scene_.fallFadeFrom, 0.0, 1.0));
        if (fallEnded_) {
            fallFade = 0.0f;
        }
    }

    uniforms.tonemap  = simd_make_float4(scene_.exposure * fallFade, 0.9f,
                                         scene_.saturation, scene_.gradePower);
    uniforms.grade    = simd_make_float4(scene_.bloomStrength, scene_.vignette,
                                         scene_.grain, static_cast<float>(frameIndex_));

    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::alloc()->init();
    auto* attachment = pass->colorAttachments()->object(0);
    attachment->setTexture(target);
    attachment->setLoadAction(MTL::LoadActionDontCare);
    attachment->setStoreAction(MTL::StoreActionStore);
    timer_.attachRender(pass, "composite");

    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
    pass->release();

    enc->setRenderPipelineState(compositePipeline_);
    enc->setFragmentTexture(hdrTarget_, 0);
    enc->setFragmentTexture(hud_.atlas(), 1);
    enc->setFragmentTexture(bloomChain_[0], 2);
    enc->setFragmentBytes(&uniforms, sizeof(uniforms), 0);
    enc->setFragmentBuffer(hud_.cells(slot), 0, 1);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    enc->endEncoding();
}

void Renderer::render(double elapsedSeconds, const FrameStats& stats) {
    cameraAzimuth_ = scene_.cameraPhi +
                     scene_.cameraOrbitRate * static_cast<float>(elapsedSeconds);

    if (falling_ && !fallEnded_) {
        const double dTau = std::min(stats.frameMs * 1.0e-3, 0.05) * scene_.fallSpeed;
        // PRESERVE LAST VALID HORIZON STATE.
        const FreeFallState previous = fall_;
        if (!freeFallAdvance(fall_, scene_.spin, scene_.mass, dTau)) {
            fall_ = previous;
            fallEnded_ = true;
            statusText_ = "horizon crossed";
        }
        fallFrame_ = freeFallFrame(fall_, scene_.spin, scene_.mass);

        // DYNAMIC RESOLUTION SCALING TO MEET TARGET FRAME TIME ON INFALL.
        const double gpuMs = timer_.supported() ? timer_.totalMs() : stats.frameMs;
        if (gpuMs > scene_.fallTargetMs * 1.08 && fallScale_ > 0.16f) {
            fallScale_ *= 0.94f;
        } else if (gpuMs < scene_.fallTargetMs * 0.82f && fallScale_ < 0.85f) {
            fallScale_ *= 1.03f;
        }
        setRenderScale(fallScale_);

        lensRowsPerFrame_ = renderHeight_;
        invalidateLens();
    }

    dispatch_semaphore_wait(inFlight_, DISPATCH_TIME_FOREVER);

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    const auto slot = static_cast<std::uint32_t>(frameIndex_ % kFramesInFlight);

    timer_.beginFrame(slot);
    writeHud(stats);
    hud_.upload(slot);

    MTL::CommandBuffer* cmd = device_->queue()->commandBuffer();
    encodeLensBuild(cmd, elapsedSeconds);
    encodeShade(cmd, elapsedSeconds);
    encodeBloom(cmd);

    CA::MetalDrawable* drawable = device_->layer()->nextDrawable();
    if (drawable) {
        encodeComposite(cmd, drawable->texture(), slot);
        cmd->presentDrawable(drawable);
    }

    dispatch_semaphore_t semaphore = inFlight_;
    auto* gpuMs = &commandBufferGpuMs_;
    cmd->addCompletedHandler([semaphore, gpuMs](MTL::CommandBuffer* buffer) {
        const double seconds = buffer->GPUEndTime() - buffer->GPUStartTime();
        gpuMs->store(seconds * 1000.0, std::memory_order_relaxed);
        dispatch_semaphore_signal(semaphore);
    });

    cmd->commit();

    pool->release();
    ++frameIndex_;
}

bool Renderer::capture(const char* path, double elapsedSeconds, const FrameStats& stats) {
    cameraAzimuth_ = scene_.cameraPhi +
                     scene_.cameraOrbitRate * static_cast<float>(elapsedSeconds);

    if (falling_ && !fallEnded_) {
        const double dTau = std::min(stats.frameMs * 1.0e-3, 0.05) * scene_.fallSpeed;
        // PRESERVE LAST VALID HORIZON STATE.
        const FreeFallState previous = fall_;
        if (!freeFallAdvance(fall_, scene_.spin, scene_.mass, dTau)) {
            fall_ = previous;
            fallEnded_ = true;
            statusText_ = "horizon crossed";
        }
        fallFrame_ = freeFallFrame(fall_, scene_.spin, scene_.mass);

        // DYNAMIC RESOLUTION SCALING TO MEET TARGET FRAME TIME ON INFALL.
        const double gpuMs = timer_.supported() ? timer_.totalMs() : stats.frameMs;
        if (gpuMs > scene_.fallTargetMs * 1.08 && fallScale_ > 0.16f) {
            fallScale_ *= 0.94f;
        } else if (gpuMs < scene_.fallTargetMs * 0.82f && fallScale_ < 0.85f) {
            fallScale_ *= 1.03f;
        }
        setRenderScale(fallScale_);

        lensRowsPerFrame_ = renderHeight_;
        invalidateLens();
    }

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    // SHARED SRGB TEXTURE READABLE BY CPU FOR PPM EXPORT.
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm_sRGB);
    desc->setWidth(drawableWidth_);
    desc->setHeight(drawableHeight_);
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);

    MTL::Texture* target = device_->mtl()->newTexture(desc);
    desc->release();
    if (!target) {
        SDL_Log("capture: could not allocate the readback target.");
        pool->release();
        return false;
    }

    MTL::CommandBuffer* cmd = device_->queue()->commandBuffer();
    // COMPLETE FULL LENS MAP BEFORE SCREENSHOT CAPTURE.
    while (!lensComplete_) {
        encodeLensBuild(cmd, elapsedSeconds);
    }

    writeHud(stats);
    hud_.upload(0);
    encodeShade(cmd, elapsedSeconds);
    encodeBloom(cmd);
    encodeComposite(cmd, target, 0);
    cmd->commit();
    cmd->waitUntilCompleted();

    const std::size_t rowBytes = static_cast<std::size_t>(drawableWidth_) * 4;
    std::vector<std::uint8_t> rgba(rowBytes * drawableHeight_);
    target->getBytes(rgba.data(), rowBytes,
                     MTL::Region(0, 0, drawableWidth_, drawableHeight_), 0);
    target->release();

    std::FILE* file = std::fopen(path, "wb");
    if (!file) {
        SDL_Log("capture: could not open %s for writing.", path);
        pool->release();
        return false;
    }

    std::fprintf(file, "P6\n%u %u\n255\n", drawableWidth_, drawableHeight_);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(drawableWidth_) * drawableHeight_ * 3);
    for (std::size_t i = 0, n = static_cast<std::size_t>(drawableWidth_) * drawableHeight_;
         i < n; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    std::fwrite(rgb.data(), 1, rgb.size(), file);
    std::fclose(file);

    pool->release();
    return true;
}

void Renderer::shutdown() {
    // DRAIN IN-FLIGHT FRAMES BEFORE RESOURCE TEARDOWN.
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        dispatch_semaphore_wait(inFlight_, DISPATCH_TIME_FOREVER);
    }

    hud_.shutdown();
    timer_.shutdown();

    if (hdrTarget_)             { hdrTarget_->release();             hdrTarget_ = nullptr; }
    for (auto*& tex : bloomChain_) { if (tex) { tex->release(); tex = nullptr; } }
    if (bloomUpPipeline_)       { bloomUpPipeline_->release();       bloomUpPipeline_ = nullptr; }
    if (bloomDownPipeline_)     { bloomDownPipeline_->release();     bloomDownPipeline_ = nullptr; }
    if (skyCubeArray_)          { skyCubeArray_->release();          skyCubeArray_ = nullptr; }
    if (skyCube_)               { skyCube_->release();               skyCube_ = nullptr; }
    if (skyBakePipeline_)       { skyBakePipeline_->release();       skyBakePipeline_ = nullptr; }
    if (blackbodyLut_)          { blackbodyLut_->release();          blackbodyLut_ = nullptr; }
    if (profileLut_)            { profileLut_->release();            profileLut_ = nullptr; }
    if (sweepTarget_)           { sweepTarget_->release();           sweepTarget_ = nullptr; }
    if (crossingTarget_)        { crossingTarget_->release();        crossingTarget_ = nullptr; }
    if (footprintTarget_)       { footprintTarget_->release();       footprintTarget_ = nullptr; }
    if (lensTarget_)            { lensTarget_->release();            lensTarget_ = nullptr; }
    if (compositePipeline_)     { compositePipeline_->release();     compositePipeline_ = nullptr; }
    if (shadePipeline_)         { shadePipeline_->release();         shadePipeline_ = nullptr; }
    if (lensFootprintPipeline_) { lensFootprintPipeline_->release(); lensFootprintPipeline_ = nullptr; }
    if (lensBuildPipeline_)     { lensBuildPipeline_->release();     lensBuildPipeline_ = nullptr; }
}

} // NAMESPACE BHS
