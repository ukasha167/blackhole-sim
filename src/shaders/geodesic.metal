#include <metal_stdlib>
using namespace metal;

struct FrameUniforms {
    float4x4 invViewProj;
    float4   cameraPos;
    float4   diskParams;
    float4   renderParams; // x=width, y=height, z=time, w=unused
};

// VERIFY: binding indices. With our pipeline createinfo (0 samplers,
// 0 read-only textures/buffers, 1 read-write texture, 0 read-write
// buffers, 1 uniform buffer), I'm assuming SDL_GPU's Metal backend places
// the storage texture at texture(0) and the uniform buffer at buffer(0).
// If the dispatch compiles but writes nothing visible, this binding
// mapping is the first thing to check against SDL_GPU's actual convention.
kernel void geodesic_main(
    texture2d<float, access::write> outTexture [[texture(0)]],
    constant FrameUniforms& uniforms           [[buffer(0)]],
    uint2 gid                                  [[thread_position_in_grid]])
{
    const uint w = uint(uniforms.renderParams.x);
    const uint h = uint(uniforms.renderParams.y);
    if (gid.x >= w || gid.y >= h) return;

    // Pure pipeline-validation pattern - proves dispatch/bindings/blit
    // work end to end. Gets fully replaced by ray generation from
    // uniforms.invViewProj + the Kerr geodesic integrator next.
    float2 uv = float2(gid) / float2(w, h);
    float pulse = 0.5 + 0.5 * sin(uniforms.renderParams.z);
    outTexture.write(float4(uv.x, uv.y, pulse, 1.0), gid);
}
