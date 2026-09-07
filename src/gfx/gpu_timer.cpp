#include "gfx/gpu_timer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace bhs {

namespace {

// RECALIBRATE CLOCK SCALE TWICE PER SECOND.
constexpr std::uint64_t kRecalibrateInterval = 32;

MTL::CounterSet* findTimestampCounterSet(MTL::Device* device) {
    NS::Array* sets = device->counterSets();
    if (!sets) {
        return nullptr;
    }
    for (NS::UInteger i = 0; i < sets->count(); ++i) {
        auto* set = static_cast<MTL::CounterSet*>(sets->object(i));
        NS::String* name = set ? set->name() : nullptr;
        if (name && std::strcmp(name->utf8String(), "timestamp") == 0) {
            return set;
        }
    }
    return nullptr;
}

} // NAMESPACE

bool GpuTimer::init(MTL::Device* device) {
    device_ = device;

    // STAGE BOUNDARY SAMPLING. APPLE SILICON SUPPORTS THIS. FALL BACK CLEANLY IF ABSENT.
    if (!device->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary)) {
        SDL_Log("GPU counter sampling at stage boundary unsupported; "
                "per-pass timing disabled.");
        return false;
    }

    MTL::CounterSet* timestamps = findTimestampCounterSet(device);
    if (!timestamps) {
        SDL_Log("No timestamp counter set; per-pass timing disabled.");
        return false;
    }

    MTL::CounterSampleBufferDescriptor* desc =
        MTL::CounterSampleBufferDescriptor::alloc()->init();
    desc->setCounterSet(timestamps);
    desc->setStorageMode(MTL::StorageModeShared);
    desc->setSampleCount(kMaxPasses * 2);

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        NS::Error* error = nullptr;
        buffers_[i] = device->newCounterSampleBuffer(desc, &error);
        if (!buffers_[i]) {
            SDL_Log("Counter sample buffer %u failed: %s", i,
                    error ? error->localizedDescription()->utf8String() : "unknown");
            desc->release();
            shutdown();
            return false;
        }
    }
    desc->release();

    device_->sampleTimestamps(&calibCpu_, &calibGpu_);
    supported_ = true;
    return true;
}

void GpuTimer::recalibrate() {
    MTL::Timestamp cpu = 0;
    MTL::Timestamp gpu = 0;
    device_->sampleTimestamps(&cpu, &gpu);

    const std::uint64_t cpuDelta = cpu - calibCpu_;
    const std::uint64_t gpuDelta = gpu - calibGpu_;

    // GUARD AGAINST DEGENERATE DELTA. SILENT BAD SCALE IS WORSE THAN ZERO.
    if (gpuDelta > 1000 && cpuDelta > 1000) {
        tickToNs_ = static_cast<double>(cpuDelta) / static_cast<double>(gpuDelta);
        calibCpu_ = cpu;
        calibGpu_ = gpu;
    }
}

void GpuTimer::beginFrame(std::uint32_t slot) {
    currentSlot_  = slot % kFramesInFlight;
    pendingCount_ = 0;

    if (!supported_) {
        return;
    }

    // SEMAPHORE ALREADY GUARANTEED COMMAND BUFFER FINISHED.
    resolve(currentSlot_);

    if (++framesSinceCal_ >= kRecalibrateInterval) {
        framesSinceCal_ = 0;
        recalibrate();
    }
}

bool GpuTimer::attachCompute(MTL::ComputePassDescriptor* desc, const char* label) {
    if (!supported_ || pendingCount_ >= kMaxPasses) {
        return false;
    }
    const std::uint32_t index = pendingCount_++;
    pendingLabels_[index] = label;

    auto* attachment = desc->sampleBufferAttachments()->object(0);
    attachment->setSampleBuffer(buffers_[currentSlot_]);
    attachment->setStartOfEncoderSampleIndex(index * 2);
    attachment->setEndOfEncoderSampleIndex(index * 2 + 1);

    slotCount_[currentSlot_]         = pendingCount_;
    slotLabels_[currentSlot_][index] = label;
    return true;
}

bool GpuTimer::attachRender(MTL::RenderPassDescriptor* desc, const char* label) {
    if (!supported_ || pendingCount_ >= kMaxPasses) {
        return false;
    }
    const std::uint32_t index = pendingCount_++;
    pendingLabels_[index] = label;

    // BRACKET FRAGMENT ONLY. VERTEX QUEUES MILES AHEAD ON TILE GPU.
    auto* attachment = desc->sampleBufferAttachments()->object(0);
    attachment->setSampleBuffer(buffers_[currentSlot_]);
    attachment->setStartOfFragmentSampleIndex(index * 2);
    attachment->setEndOfFragmentSampleIndex(index * 2 + 1);

    slotCount_[currentSlot_]         = pendingCount_;
    slotLabels_[currentSlot_][index] = label;
    return true;
}

void GpuTimer::resolve(std::uint32_t slot) {
    const std::uint32_t count = slotCount_[slot];
    if (count == 0) {
        return;
    }

    NS::Data* data = buffers_[slot]->resolveCounterRange(NS::Range(0, count * 2));
    if (!data) {
        slotCount_[slot] = 0;
        return;
    }

    const auto* samples = static_cast<const MTL::CounterResultTimestamp*>(data->mutableBytes());
    const std::uint32_t available =
        static_cast<std::uint32_t>(data->length() / sizeof(MTL::CounterResultTimestamp));

    resolvedCount_ = 0;
    std::uint64_t unionStart = ~0ULL;
    std::uint64_t unionEnd   = 0;

    for (std::uint32_t i = 0; i < count && (i * 2 + 1) < available; ++i) {
        const std::uint64_t start = samples[i * 2].timestamp;
        const std::uint64_t end   = samples[i * 2 + 1].timestamp;

        // METAL ERROR VALUE OR UNORDERED PAIR GETS SKIPPED.
        if (start == MTL::CounterErrorValue || end == MTL::CounterErrorValue || end < start) {
            continue;
        }

        const std::uint32_t out = resolvedCount_++;
        resolvedLabels_[out] = slotLabels_[slot][i];
        resolvedMs_[out]     = static_cast<double>(end - start) * tickToNs_ * 1.0e-6;

        unionStart = std::min(unionStart, start);
        unionEnd   = std::max(unionEnd, end);
    }

    unionMs_ = (resolvedCount_ > 0)
                 ? static_cast<double>(unionEnd - unionStart) * tickToNs_ * 1.0e-6
                 : 0.0;

    slotCount_[slot] = 0;
}

void GpuTimer::shutdown() {
    for (auto*& buffer : buffers_) {
        if (buffer) {
            buffer->release();
            buffer = nullptr;
        }
    }
    supported_ = false;
}

} // NAMESPACE BHS
