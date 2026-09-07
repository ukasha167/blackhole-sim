#pragma once

#include <cstdint>
#include <vector>

namespace bhs {

// TABLES BUILT ONCE ON CPU AT STARTUP. 1D TEXTURES. NO CAMERA DEPENDENCY.

inline constexpr std::uint32_t kBlackbodySamples = 256;
inline constexpr float         kBlackbodyMinK    = 700.0f;
inline constexpr float         kBlackbodyMaxK    = 40000.0f;

inline constexpr std::uint32_t kDiskProfileSamples = 512;

// BLACKBODY LINEAR SRGB TABLE. UNIT LUMINANCE, LOG-TEMP INDEXED.
std::vector<float> buildBlackbodyTable();

// CIE 1931 BLACKBODY CHROMATICITY FOR TEST COMPARISON WITH PLANCKIAN LOCUS.
void blackbodyChromaticity(double kelvin, double& x, double& y);

struct DiskProfile {
    // NORMALISED PAGE-THORNE TEMPERATURE PROFILE. PEAK AT 1.0.
    std::vector<float> temperature;
    float innerRadius = 0.0f;   // ISCO.
    float outerRadius = 0.0f;
    float peakRadius  = 0.0f;   // RADIUS OF MAXIMUM FLUX.
};

DiskProfile buildDiskProfile(float spin, float mass, float outerRadius);

} // NAMESPACE BHS

