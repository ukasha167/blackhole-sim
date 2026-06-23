#include "solver.hpp"
#include "defines.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace bhs {

void Solver::init() {
    disk_.spin = static_cast<float>(kSpinParameter);
}

void Solver::update(double dt) {
    elapsedTime_ += dt;

    // Placeholder establishing orbit so the camera matrices are
    // non-trivial while we validate the render pipeline. Real Kerr-metric
    // camera dynamics (and any GR-correct motion, if you want the camera
    // itself geodesic) come later — don't read physics meaning into this.
    constexpr float orbitRadius = 15.0f;
    constexpr float orbitSpeed  = 0.1f; // rad/sec
    const float angle = static_cast<float>(elapsedTime_) * orbitSpeed;

    camera_.position = glm::vec3(orbitRadius * std::cos(angle),
                                  4.0f,
                                  orbitRadius * std::sin(angle));
    camera_.viewMatrix = glm::lookAt(camera_.position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    constexpr float fovYDegrees = 60.0f;
    constexpr float nearPlane   = 0.1f;
    constexpr float farPlane    = 1000.0f;
    const float aspect = static_cast<float>(kWindowWidth) / static_cast<float>(kWindowHeight);
    camera_.projMatrix = glm::perspective(glm::radians(fovYDegrees), aspect, nearPlane, farPlane);
}

} // namespace bhs
