#pragma once

#include <cstdint>
#include <numbers>

namespace bhs {

inline constexpr int         kWindowWidth  = 1280;
inline constexpr int         kWindowHeight = 800;
inline constexpr const char* kWindowTitle  = "Still Second to HER EYES.";

// GEOMETRIC UNITS. G = C = 1. M IS LENGTH. ALL RADII IN M.
inline constexpr double kBlackHoleMass = 1.0;

// SPIN A/M = 0.99. NEAR EXTREMAL. MOVIE USED 0.6 FOR WEAK AUDIENCE.
inline constexpr double kSpinParameter = 0.99;

inline constexpr double kPi = std::numbers::pi;

// CLAMP DT. WINDOW DRAG OR DEBUGGER HITCH MUST NOT BLOW UP WORLD.
inline constexpr double kMaxFrameTime = 1.0 / 15.0;

} // NAMESPACE BHS

