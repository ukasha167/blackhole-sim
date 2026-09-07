#include "gfx/device.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

namespace bhs {

bool Device::init(CA::MetalLayer* layer, const char* metallibPath) {
    layer_ = layer;

    device_ = MTL::CreateSystemDefaultDevice();
    if (!device_) {
        SDL_Log("No Metal device available.");
        return false;
    }

    if (NS::String* deviceName = device_->name()) {
        std::snprintf(name_, sizeof(name_), "%s", deviceName->utf8String());
    }

    queue_ = device_->newCommandQueue();
    if (!queue_) {
        SDL_Log("Failed to create Metal command queue.");
        return false;
    }

    // LAYER WRITES SRGB AUTOMATIC. SHADER STAYS LINEAR TO END.
    layer_->setDevice(device_);
    layer_->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
    layer_->setFramebufferOnly(true);

    NS::String* path = NS::String::string(metallibPath, NS::UTF8StringEncoding);
    NS::URL*    url  = NS::URL::fileURLWithPath(path);

    NS::Error* error = nullptr;
    library_ = device_->newLibrary(url, &error);
    if (!library_) {
        SDL_Log("Failed to load %s: %s", metallibPath,
                error ? error->localizedDescription()->utf8String() : "unknown error");
        return false;
    }

    return true;
}

void Device::resize(std::uint32_t widthPx, std::uint32_t heightPx) {
    if (layer_ && widthPx > 0 && heightPx > 0) {
        layer_->setDrawableSize(CGSizeMake(static_cast<CGFloat>(widthPx),
                                           static_cast<CGFloat>(heightPx)));
    }
}

void Device::shutdown() {
    if (library_) { library_->release(); library_ = nullptr; }
    if (queue_)   { queue_->release();   queue_   = nullptr; }
    if (device_)  { device_->release();  device_  = nullptr; }
    layer_ = nullptr;
}

} // NAMESPACE BHS
