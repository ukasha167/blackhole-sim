#pragma once

#include <Metal/Metal.hpp>

#include <cstdint>

#include "shaders/common.h"

namespace bhs {

// FIXED CHAR GRID OVERLAY IN COMPOSITE. CORETEXT RASTERISES MONO ATLAS AT STARTUP.
class Hud {
public:
    static constexpr std::uint32_t kFramesInFlight = 3;
    static constexpr std::uint32_t kColumns        = BHS_HUD_COLUMNS;
    static constexpr std::uint32_t kRows           = BHS_HUD_ROWS;
    static constexpr std::uint32_t kCellCount      = kColumns * kRows;

    bool init(MTL::Device* device);
    void shutdown();

    void clear();
    void print(std::uint32_t col, std::uint32_t row, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));

    // COPIES CHAR GRID INTO CURRENT FRAME BUFFER.
    void upload(std::uint32_t slot);

    MTL::Texture* atlas()                      const { return atlas_; }
    MTL::Buffer*  cells(std::uint32_t slot)    const { return cells_[slot % kFramesInFlight]; }

    float cellWidth()  const { return cellWidth_; }
    float cellHeight() const { return cellHeight_; }

private:
    bool buildAtlas(MTL::Device* device);

    MTL::Texture* atlas_ = nullptr;
    MTL::Buffer*  cells_[kFramesInFlight]{};

    std::uint32_t grid_[kCellCount]{};
    float         cellWidth_  = 0.0f;
    float         cellHeight_ = 0.0f;
};

} // NAMESPACE BHS

