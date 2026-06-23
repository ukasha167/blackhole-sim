#pragma once
#include <glm/glm.hpp>

namespace bhs {

// Layout shared with shaders/geodesic.metal's `FrameUniforms`.
// Every field is vec4/mat4-sized specifically to dodge Metal's float3
// alignment trap described above — don't "optimize" this back down to
// vec3, it will reintroduce the bug.
struct alignas(16) FrameUniforms {
    glm::mat4 invViewProj{1.0f};
    glm::vec4 cameraPos{0.0f};      // .xyz used, .w padding
    glm::vec4 diskParams{0.0f};     // x=innerRadius, y=outerRadius, z=spin, w=unused
    glm::vec4 renderParams{0.0f};   // x=width, y=height, z=time, w=unused
};
static_assert(sizeof(FrameUniforms) % 16 == 0, "UBO must be 16-byte aligned for Metal");

} // namespace bhs
