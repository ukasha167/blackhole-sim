#pragma once

#include <cstdint>
#include <numbers>

namespace bhs {

inline constexpr int         kWindowWidth  = 1280;
inline constexpr int         kWindowHeight = 800;
inline constexpr const char* kWindowTitle  = "Still Second to HER EYES. ";

inline constexpr int    kTargetFPS       = 60;
inline constexpr double kTargetFrameTime = 1.0 / kTargetFPS;
inline constexpr double kMaxFrameTime    = 1.0 / 30.0; // clamp dt on hitches/debugger pauses

// --- Kerr metric (geometric units, G = c = 1) ---
inline constexpr double kBlackHoleMass = 1.0;
inline constexpr double kSpinParameter = 0.6;          // a / M, in [-1, 1]
inline constexpr double kPi            = std::numbers::pi;

// --- Integration: heuristic adaptive RK4, NOT embedded RK45.
// See architecture notes: true RK45's stage count + divergent per-thread
// step counts kill SIMD-group occupancy on Apple GPUs at this fidelity.
inline constexpr double kMinStepSize          = 1.0e-4;
inline constexpr double kMaxStepSize          = 0.5;
inline constexpr int    kMaxGeodesicSteps     = 512;   // hard ceiling: bounds worst-case ALU/pixel

inline constexpr uint32_t kThreadGroupSizeX = 16;
inline constexpr uint32_t kThreadGroupSizeY = 16;

}
