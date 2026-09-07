#pragma once

// AGX TONE MAPPING. PRESERVES HUE IN EXTREME HIGHLIGHTS. SHARED MSL AND C++.

#ifdef __METAL_VERSION__
    #include <metal_stdlib>
    using namespace metal;
    #define TM_FN static inline
#else
    #include <cmath>
    #define TM_FN static inline
    using std::log2; using std::pow; using std::fmin; using std::fmax;
#endif

struct TmRgb {
    float r, g, b;
};

TM_FN TmRgb tm_make(float r, float g, float b) {
    TmRgb c; c.r = r; c.g = g; c.b = b; return c;
}

TM_FN float tm_clamp(float x, float lo, float hi) {
    return fmin(fmax(x, lo), hi);
}

TM_FN float tm_luma(TmRgb c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// REC.709 TO/FROM AGX WORKING COLOR SPACE.
TM_FN TmRgb tm_agx_forward(TmRgb c) {
    return tm_make(
        0.842479062253094f * c.r + 0.0784335999999992f * c.g + 0.0792237451477643f * c.b,
        0.0423282422610123f * c.r + 0.878468636469772f * c.g + 0.0791661274605434f * c.b,
        0.0423756549057051f * c.r + 0.0784336f * c.g + 0.879142973793104f * c.b);
}

TM_FN TmRgb tm_agx_inverse(TmRgb c) {
    return tm_make(
         1.19687900512017f * c.r - 0.0980208811401368f * c.g - 0.0990297440797205f * c.b,
        -0.0528968517574562f * c.r + 1.15190312990417f * c.g - 0.0989611768448433f * c.b,
        -0.0529716355144438f * c.r - 0.0989629776925405f * c.g + 1.15107367264116f * c.b);
}

// SIXTH-ORDER POLYNOMIAL FIT TO AGX CONTRAST CURVE.
TM_FN float tm_agx_contrast(float x) {
    const float x2 = x * x;
    const float x4 = x2 * x2;
    return 15.5f * x4 * x2
         - 40.14f * x4 * x
         + 31.96f * x4
         - 6.868f * x2 * x
         + 0.4298f * x2
         + 0.1191f * x
         - 0.00232f;
}

// COLOR GRADE IN AGX SPACE. SATURATION PRESERVES DOPPLER RED LIMB.
TM_FN TmRgb tm_agx_look(TmRgb c, float saturation, float power) {
    const float lum = tm_luma(c);
    TmRgb graded = tm_make(pow(fmax(c.r, 0.0f), power),
                           pow(fmax(c.g, 0.0f), power),
                           pow(fmax(c.b, 0.0f), power));
    return tm_make(lum + saturation * (graded.r - lum),
                   lum + saturation * (graded.g - lum),
                   lum + saturation * (graded.b - lum));
}

TM_FN TmRgb tm_agx(TmRgb color, float exposure, float saturation, float power) {
    const float minEv = -12.47393f;
    const float maxEv = 4.026069f;

    TmRgb c = tm_make(fmax(color.r * exposure, 0.0f),
                      fmax(color.g * exposure, 0.0f),
                      fmax(color.b * exposure, 0.0f));

    c = tm_agx_forward(c);

    // LOG2 ENCODE WITH SAFETY FLOOR.
    const float floorValue = 1.0e-10f;
    c = tm_make(
        (tm_clamp(log2(fmax(c.r, floorValue)), minEv, maxEv) - minEv) / (maxEv - minEv),
        (tm_clamp(log2(fmax(c.g, floorValue)), minEv, maxEv) - minEv) / (maxEv - minEv),
        (tm_clamp(log2(fmax(c.b, floorValue)), minEv, maxEv) - minEv) / (maxEv - minEv));

    c = tm_make(tm_agx_contrast(c.r), tm_agx_contrast(c.g), tm_agx_contrast(c.b));
    c = tm_agx_look(c, saturation, power);
    c = tm_agx_inverse(c);

    c = tm_make(tm_clamp(c.r, 0.0f, 1.0f),
                tm_clamp(c.g, 0.0f, 1.0f),
                tm_clamp(c.b, 0.0f, 1.0f));

    // CONTRAST CURVE IS DISPLAY ENCODED. APPLY 2.2 GAMMA EOTF BACK TO LINEAR.
    return tm_make(pow(c.r, 2.2f), pow(c.g, 2.2f), pow(c.b, 2.2f));
}
