#pragma once

#include <Metal/Metal.hpp>

#include <cstdint>

namespace bhs {

// REAL PER-PASS GPU TIMING. METAL COUNTER BUFFERS, NOT CPU WALL CLOCK.
// ONE BUFFER PER IN-FLIGHT FRAME. RESOLVE ON FRAME RETURN.
class GpuTimer {
public:
    static constexpr std::uint32_t kFramesInFlight = 3;
    static constexpr std::uint32_t kMaxPasses      = 8;

    bool init(MTL::Device* device);
    void shutdown();

    // FALSE IF HARDWARE CANNOT SAMPLE STAGE BOUNDARIES.
    bool supported() const { return supported_; }

    // PUBLISH OLD RESULTS FOR SLOT. RESET FOR REUSE.
    void beginFrame(std::uint32_t slot);

    // ATTACH TO PASS BEFORE ENCODER CREATION. FALSE MEANS NO COUNTER.
    bool attachCompute(MTL::ComputePassDescriptor* desc, const char* label);
    bool attachRender(MTL::RenderPassDescriptor* desc, const char* label);

    std::uint32_t passCount()               const { return resolvedCount_; }
    const char*   passLabel(std::uint32_t i) const { return resolvedLabels_[i]; }

    // PASS DURATION. TILE GPU OVERLAPS PASSES. DO NOT SUM OR OVERCOUNT.
    double passMs(std::uint32_t i) const { return resolvedMs_[i]; }

    // TOTAL SPAN FIRST START TO LAST END. TRUE GPU TIME.
    double totalMs() const { return unionMs_; }

private:
    void resolve(std::uint32_t slot);
    void recalibrate();

    MTL::Device*            device_ = nullptr;
    MTL::CounterSampleBuffer* buffers_[kFramesInFlight]{};

    bool          supported_    = false;
    std::uint32_t currentSlot_  = 0;
    std::uint32_t pendingCount_ = 0;
    const char*   pendingLabels_[kMaxPasses]{};

    // RECORDS LIVE PASS COUNT PER SLOT.
    std::uint32_t slotCount_[kFramesInFlight]{};
    const char*   slotLabels_[kFramesInFlight][kMaxPasses]{};

    std::uint32_t resolvedCount_ = 0;
    const char*   resolvedLabels_[kMaxPasses]{};
    double        resolvedMs_[kMaxPasses]{};
    double        unionMs_ = 0.0;

    // GPU TICKS NOT NS. CORRELATE PERIODICALLY FOR SCALE FACTOR.
    double            tickToNs_       = 1.0;
    MTL::Timestamp    calibCpu_       = 0;
    MTL::Timestamp    calibGpu_       = 0;
    std::uint64_t     framesSinceCal_ = 0;
};

} // NAMESPACE BHS

