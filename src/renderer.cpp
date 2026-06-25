// renderer.cpp

#include "renderer.hpp"
#include "solver.hpp"
#include "gpu_types.hpp"
#include "defines.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <fstream>
#include <vector>

namespace bhs {

namespace {

std::vector<Uint8> loadFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        SDL_Log("Failed to open file: %s", path);
        return {};
    }
    const auto size = file.tellg();
    std::vector<Uint8> buffer(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}
}

bool Renderer::createGPUDevice() {
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_METALLIB, /*debug_mode=*/true, nullptr);
    if (!device_) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool Renderer::createComputePipeline() {
    const auto shaderBytes = loadFile("assets/geodesic.metallib");
    if (shaderBytes.empty()) {
        SDL_Log("geodesic.metallib missing/empty - did the Shaders CMake target build?");
        return false;
    }

    SDL_GPUComputePipelineCreateInfo info{};
    info.code                            = shaderBytes.data();
    info.code_size                       = shaderBytes.size();
    info.entrypoint                      = "geodesic_main";
    info.format                          = SDL_GPU_SHADERFORMAT_METALLIB;
    info.num_samplers                    = 0;
    info.num_readonly_storage_textures   = 0;
    info.num_readonly_storage_buffers    = 0;
    info.num_readwrite_storage_textures  = 1;
    info.num_readwrite_storage_buffers   = 0;
    info.num_uniform_buffers             = 1;
    info.threadcount_x = kThreadGroupSizeX;
    info.threadcount_y = kThreadGroupSizeY;
    info.threadcount_z = 1;

    computePipeline_ = SDL_CreateGPUComputePipeline(device_, &info);
    if (!computePipeline_) {
        SDL_Log("SDL_CreateGPUComputePipeline failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool Renderer::createRenderTarget(int width, int height) {
    destroyRenderTarget();

    SDL_GPUTextureCreateInfo info{};
    info.type                 = SDL_GPU_TEXTURETYPE_2D;
    info.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage                = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width                = static_cast<Uint32>(width);
    info.height               = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels           = 1;
    info.sample_count         = SDL_GPU_SAMPLECOUNT_1;

    renderTarget_ = SDL_CreateGPUTexture(device_, &info);
    if (!renderTarget_) {
        SDL_Log("SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return false;
    }
    renderWidth_  = width;
    renderHeight_ = height;
    return true;
}

void Renderer::destroyRenderTarget() {
    if (renderTarget_) {
        SDL_ReleaseGPUTexture(device_, renderTarget_);
        renderTarget_ = nullptr;
    }
}

bool Renderer::init(const char* title, int width, int height) {
    windowWidth_  = width;
    windowHeight_ = height;

    window_ = SDL_CreateWindow(title, width, height, 0);
    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    if (!createGPUDevice())                 return false;
    if (!createComputePipeline())           return false;
    if (!createRenderTarget(width, height)) return false;

    return true;
}

void Renderer::updateDynamicResolution(double lastFrameSeconds) {
    constexpr double kBudget = kTargetFrameTime;
    if (lastFrameSeconds > kBudget * 1.1) {
        renderScale_ = std::max(kMinRenderScale, renderScale_ - 0.05f);
    } else if (lastFrameSeconds < kBudget * 0.85) {
        renderScale_ = std::min(kMaxRenderScale, renderScale_ + 0.01f);
    }

    const int targetW = static_cast<int>(windowWidth_  * renderScale_);
    const int targetH = static_cast<int>(windowHeight_ * renderScale_);
    if (targetW != renderWidth_ || targetH != renderHeight_) {
        createRenderTarget(targetW, targetH);
    }
}

void Renderer::renderFrame(const CameraState& camera, const DiskState& disk, double elapsedTime) {
    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmdbuf) {
        SDL_Log("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return;
    }

    SDL_GPUTexture* swapchainTexture = nullptr;
    Uint32 swapW = 0, swapH = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, window_, &swapchainTexture, &swapW, &swapH)) {
        SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmdbuf);
        return;
    }
    if (!swapchainTexture) {
        SDL_SubmitGPUCommandBuffer(cmdbuf);
        return;
    }

    FrameUniforms uniforms{};
    uniforms.invViewProj  = glm::inverse(camera.projMatrix * camera.viewMatrix);
    uniforms.cameraPos    = glm::vec4(camera.position, 1.0f);
    uniforms.diskParams   = glm::vec4(disk.innerRadius, disk.outerRadius, disk.spin, 0.0f);
    uniforms.renderParams = glm::vec4(static_cast<float>(renderWidth_),
                                       static_cast<float>(renderHeight_),
                                       static_cast<float>(elapsedTime),
                                       static_cast<float>(kBlackHoleMass));

    SDL_PushGPUComputeUniformData(cmdbuf,0, &uniforms, sizeof(uniforms));

    SDL_GPUStorageTextureReadWriteBinding writeBinding{};
    writeBinding.texture   = renderTarget_;
    writeBinding.mip_level = 0;
    writeBinding.layer     = 0;
    writeBinding.cycle     = true;

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmdbuf, &writeBinding, 1, nullptr, 0);
    SDL_BindGPUComputePipeline(pass, computePipeline_);

    const Uint32 groupsX = (static_cast<Uint32>(renderWidth_)  + kThreadGroupSizeX - 1) / kThreadGroupSizeX;
    const Uint32 groupsY = (static_cast<Uint32>(renderHeight_) + kThreadGroupSizeY - 1) / kThreadGroupSizeY;
    SDL_DispatchGPUCompute(pass, groupsX, groupsY, 1);
    SDL_EndGPUComputePass(pass);

    SDL_GPUBlitInfo blit{};
    blit.source.texture      = renderTarget_;
    blit.source.w            = static_cast<Uint32>(renderWidth_);
    blit.source.h            = static_cast<Uint32>(renderHeight_);
    blit.destination.texture = swapchainTexture;
    blit.destination.w       = swapW;
    blit.destination.h       = swapH;
    blit.load_op             = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter              = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(cmdbuf, &blit);

    SDL_SubmitGPUCommandBuffer(cmdbuf);
}

void Renderer::shutdown() {
    destroyRenderTarget();
    if (computePipeline_) SDL_ReleaseGPUComputePipeline(device_, computePipeline_);
    if (device_ && window_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
    if (device_) SDL_DestroyGPUDevice(device_);
    if (window_) SDL_DestroyWindow(window_);
}

}
