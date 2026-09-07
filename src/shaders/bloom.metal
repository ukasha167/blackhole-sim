#include <metal_stdlib>

#include "common.h"

using namespace metal;

// DOWNSAMPLE PYRAMID + TENT UPSAMPLE CHAIN. WIDE SOFT FALLOFF.

constexpr sampler bloomSampler(filter::linear, address::clamp_to_edge);

// KARIS LUMA WEIGHTING 1/(1+LUMA). KILLS PHOTON RING FIREFLIES.
static float3 karis_average(float3 a, float3 b, float3 c, float3 d) {
    const float wa = 1.0f / (1.0f + dot(a, float3(0.2126f, 0.7152f, 0.0722f)));
    const float wb = 1.0f / (1.0f + dot(b, float3(0.2126f, 0.7152f, 0.0722f)));
    const float wc = 1.0f / (1.0f + dot(c, float3(0.2126f, 0.7152f, 0.0722f)));
    const float wd = 1.0f / (1.0f + dot(d, float3(0.2126f, 0.7152f, 0.0722f)));
    return (a * wa + b * wb + c * wc + d * wd) / max(wa + wb + wc + wd, 1e-6f);
}

// 13-TAP DOWNSAMPLE (4 OVERLAPPING QUADS + CENTER QUAD).
kernel void bloom_downsample(texture2d<float>                source [[texture(0)]],
                             texture2d<float, access::write> dest   [[texture(1)]],
                             constant BloomUniforms&         u      [[buffer(0)]],
                             uint2                           gid    [[thread_position_in_grid]])
{
    const uint2 size = uint2(u.params.xy);
    if (gid.x >= size.x || gid.y >= size.y) {
        return;
    }

    const float2 uv = (float2(gid) + 0.5f) / float2(size);
    const float2 texel = 1.0f / float2(source.get_width(), source.get_height());
    const float2 t = texel;

    const float3 a = source.sample(bloomSampler, uv + float2(-2, 2) * t).rgb;
    const float3 b = source.sample(bloomSampler, uv + float2( 0, 2) * t).rgb;
    const float3 c = source.sample(bloomSampler, uv + float2( 2, 2) * t).rgb;
    const float3 d = source.sample(bloomSampler, uv + float2(-2, 0) * t).rgb;
    const float3 e = source.sample(bloomSampler, uv).rgb;
    const float3 f = source.sample(bloomSampler, uv + float2( 2, 0) * t).rgb;
    const float3 g = source.sample(bloomSampler, uv + float2(-2,-2) * t).rgb;
    const float3 h = source.sample(bloomSampler, uv + float2( 0,-2) * t).rgb;
    const float3 i = source.sample(bloomSampler, uv + float2( 2,-2) * t).rgb;
    const float3 j = source.sample(bloomSampler, uv + float2(-1, 1) * t).rgb;
    const float3 k = source.sample(bloomSampler, uv + float2( 1, 1) * t).rgb;
    const float3 l = source.sample(bloomSampler, uv + float2(-1,-1) * t).rgb;
    const float3 m = source.sample(bloomSampler, uv + float2( 1,-1) * t).rgb;

    float3 result;
    if (u.params.z > 0.5f) {
        // FIRST PASS USES KARIS AVERAGE DIRECT OFF HDR.
        result = karis_average(j, k, l, m) * 0.5f
               + karis_average(a, b, d, e) * 0.125f
               + karis_average(b, c, e, f) * 0.125f
               + karis_average(d, e, g, h) * 0.125f
               + karis_average(e, f, h, i) * 0.125f;
    } else {
        result = e * 0.125f
               + (a + c + g + i) * 0.03125f
               + (b + d + f + h) * 0.0625f
               + (j + k + l + m) * 0.125f;
    }

    dest.write(float4(max(result, 0.0f), 1.0f), gid);
}

// 9-TAP TENT FILTER ACCUMULATED ONTO DESTINATION.
kernel void bloom_upsample(texture2d<float>                source   [[texture(0)]],
                           texture2d<float>                previous [[texture(1)]],
                           texture2d<float, access::write> dest     [[texture(2)]],
                           constant BloomUniforms&         u        [[buffer(0)]],
                           uint2                           gid      [[thread_position_in_grid]])
{
    const uint2 size = uint2(u.params.xy);
    if (gid.x >= size.x || gid.y >= size.y) {
        return;
    }

    const float2 uv = (float2(gid) + 0.5f) / float2(size);
    const float r = u.params.w / float2(size).x;
    const float2 t = float2(r, r * float2(size).x / float2(size).y);

    float3 sum = source.sample(bloomSampler, uv + float2(-1,  1) * t).rgb * 1.0f;
    sum += source.sample(bloomSampler, uv + float2( 0,  1) * t).rgb * 2.0f;
    sum += source.sample(bloomSampler, uv + float2( 1,  1) * t).rgb * 1.0f;
    sum += source.sample(bloomSampler, uv + float2(-1,  0) * t).rgb * 2.0f;
    sum += source.sample(bloomSampler, uv).rgb * 4.0f;
    sum += source.sample(bloomSampler, uv + float2( 1,  0) * t).rgb * 2.0f;
    sum += source.sample(bloomSampler, uv + float2(-1, -1) * t).rgb * 1.0f;
    sum += source.sample(bloomSampler, uv + float2( 0, -1) * t).rgb * 2.0f;
    sum += source.sample(bloomSampler, uv + float2( 1, -1) * t).rgb * 1.0f;
    sum *= 1.0f / 16.0f;

    const float3 existing = previous.sample(bloomSampler, uv).rgb;
    dest.write(float4(max(existing + sum, 0.0f), 1.0f), gid);
}
