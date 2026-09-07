#pragma once

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <cstdint>

namespace bhs {

// OWNS METAL DEVICE, QUEUE, SHADER LIB. HOOKS UP SDL CAMETALLAYER.
class Device {
public:
    bool init(CA::MetalLayer* layer, const char* metallibPath);
    void shutdown();

    void resize(std::uint32_t widthPx, std::uint32_t heightPx);

    MTL::Device*       mtl()     const { return device_; }
    MTL::CommandQueue* queue()   const { return queue_; }
    MTL::Library*      library() const { return library_; }
    CA::MetalLayer*    layer()   const { return layer_; }

    const char* name() const { return name_; }

private:
    MTL::Device*       device_  = nullptr;
    MTL::CommandQueue* queue_   = nullptr;
    MTL::Library*      library_ = nullptr;
    CA::MetalLayer*    layer_   = nullptr;
    char               name_[128]{};
};

} // NAMESPACE BHS

