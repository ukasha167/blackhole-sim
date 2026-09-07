#pragma once

// SHARED MSL AND C++. ALL STRUCT MEMBERS 16 BYTES. NEVER USE FLOAT3.

#ifdef __METAL_VERSION__
    #define BHS_FLOAT4 float4
    #define BHS_UINT   uint
#else
    #include <cstdint>
    #include <simd/simd.h>
    #define BHS_FLOAT4 simd_float4
    #define BHS_UINT   std::uint32_t
#endif

struct SceneUniforms {
    BHS_FLOAT4 resolution;  // XY: TARGET SIZE PX, ZW: 1/SIZE.
    BHS_FLOAT4 timing;      // X: ELAPSED SECONDS.
    BHS_FLOAT4 camera;      // X: R, Y: THETA, Z: PHI, W: TAN(FOVY/2).
    BHS_FLOAT4 blackHole;   // X: A, Y: M, Z: ESCAPE RADIUS, W: TOLERANCE.
    BHS_FLOAT4 integrator;  // X: MAX STEPS, Y: ASPECT, Z: ROW OFFSET.
    BHS_FLOAT4 boost;       // XYZ: CAMERA VELOCITY, W: GAMMA.
};

struct DiskUniforms {
    BHS_FLOAT4 radii;     // X: INNER, Y: OUTER, Z: SCALE HEIGHT H.
    BHS_FLOAT4 emission;  // X: PEAK TEMP K, Y: BRIGHTNESS, Z: OPACITY, W: TIME.
    BHS_FLOAT4 noise;     // X: RADIAL SCALE, Y: STRENGTH, Z: SHEAR, W: DIAG.
    BHS_FLOAT4 detail;    // X: AZIMUTH SCALE, Y: RIDGE, Z: HOTSPOTS, W: GLOW.
    BHS_FLOAT4 flow;      // X: WARP, Y: VOID CUT, Z: INFLOW, W: SKY GLOW.
    BHS_FLOAT4 sky;       // X: TILT DEG, Y: YAW DEG, Z: CORE DEG, W: DUST.
    BHS_FLOAT4 galaxy;    // X: BAND WIDTH, Y: BULGE, Z: CONTRAST.
};

struct BloomUniforms {
    BHS_FLOAT4 params;  // XY: DEST SIZE PX, Z: KARIS FLAG, W: TENT RADIUS.
};

#define BHS_MAX_CROSSINGS 3
#define BHS_BLOOM_LEVELS  6

struct CompositeUniforms {
    BHS_FLOAT4 drawable;    // XY: DRAWABLE PX, ZW: 1/SIZE.
    BHS_FLOAT4 hudRect;     // XY: ORIGIN PX, ZW: CELL SIZE PX.
    BHS_FLOAT4 hudGrid;     // XY: GRID COLS/ROWS, ZW: ATLAS COLS/ROWS.
    BHS_FLOAT4 tonemap;     // X: EXPOSURE, Y: HUD OPACITY, Z: SAT, W: POWER.
    BHS_FLOAT4 grade;       // X: BLOOM, Y: VIGNETTE, Z: GRAIN, W: TIME.
};

// UINT PER CELL FOR DIRECT MSL BUFFER INDEXING.
#define BHS_HUD_COLUMNS 58
#define BHS_HUD_ROWS    16

// PRINTABLE ASCII (0X20 TO 0X7E). 16 GLYPHS PER ROW IN ATLAS.
#define BHS_GLYPH_FIRST 32
#define BHS_GLYPH_COUNT 95
#define BHS_ATLAS_COLS  16
#define BHS_ATLAS_ROWS  6

