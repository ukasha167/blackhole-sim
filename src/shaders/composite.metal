#include <metal_stdlib>
#include "common.h"
#include "physics/tonemap.h"

using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

// FULLSCREEN TRIANGLE. NO VERTEX BUFFER.
vertex VertexOut composite_vertex(uint vid [[vertex_id]]) {
    const float2 uv = float2((vid << 1) & 2, vid & 2);
    VertexOut out;
    out.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    out.uv       = uv;
    return out;
}

// FAST PSEUDO-RANDOM NOISE HASH.
static float grain_hash(float2 p, float t) {
    float3 q = float3(p, t);
    q = fract(q * 0.1031f);
    q += dot(q, q.yzx + 33.33f);
    return fract((q.x + q.y) * q.z);
}

fragment float4 composite_fragment(VertexOut                in       [[stage_in]],
                                   texture2d<float>         hdr      [[texture(0)]],
                                   texture2d<float>         font     [[texture(1)]],
                                   texture2d<float>         bloom    [[texture(2)]],
                                   constant CompositeUniforms& u     [[buffer(0)]],
                                   constant uint*           hudCells [[buffer(1)]])
{
    constexpr sampler linearSampler(filter::linear, address::clamp_to_edge);
    constexpr sampler glyphSampler(filter::linear, address::clamp_to_edge);

    float3 color = hdr.sample(linearSampler, in.uv).rgb;
    color += bloom.sample(linearSampler, in.uv).rgb * u.grade.x;

    // LINEAR SPACE VIGNETTE FALLOFF.
    const float2 centred = (in.uv - 0.5f) * float2(2.0f, 2.0f);
    const float vignette = 1.0f - u.grade.y * dot(centred, centred) * 0.25f;
    color *= max(vignette, 0.0f);

    const TmRgb mapped = tm_agx(tm_make(color.r, color.g, color.b),
                                u.tonemap.x, u.tonemap.z, u.tonemap.w);
    color = float3(mapped.r, mapped.g, mapped.b);

    // FILM GRAIN APPLIED IN DISPLAY SPACE.
    if (u.grade.z > 0.0f) {
        const float n = grain_hash(in.uv * u.drawable.xy, u.grade.w);
        color += (n - 0.5f) * u.grade.z;
        color = saturate(color);
    }

    // HUD TEXT OVERLAY.
    const float2 pixel    = in.uv * u.drawable.xy;
    const float2 cellSize = u.hudRect.zw;
    const float2 local    = pixel - u.hudRect.xy;
    const float2 gridSize = u.hudGrid.xy * cellSize;

    if (all(local >= 0.0) && all(local < gridSize)) {
        const uint2 cell  = uint2(local / cellSize);
        const uint  index = cell.y * uint(u.hudGrid.x) + cell.x;
        const uint  ch    = hudCells[index];

        // DARK BACKING PLATE FOR TEXT CONTRAST.
        color = mix(color, float3(0.0), 0.55 * u.tonemap.y);

        if (ch >= uint(BHS_GLYPH_FIRST) && ch < uint(BHS_GLYPH_FIRST + BHS_GLYPH_COUNT)) {
            const uint  glyph    = ch - uint(BHS_GLYPH_FIRST);
            const float2 atlasXY = float2(glyph % uint(u.hudGrid.z),
                                          glyph / uint(u.hudGrid.z));
            const float2 within  = fract(local / cellSize);
            const float2 atlasUV = (atlasXY + within) / u.hudGrid.zw;

            const float coverage = font.sample(glyphSampler, atlasUV).r;
            color = mix(color, float3(0.85, 0.92, 1.0), coverage * u.tonemap.y);
        }
    }

    return float4(color, 1.0);
}
