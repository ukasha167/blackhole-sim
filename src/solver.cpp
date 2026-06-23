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

    // Orbit in the X-Y plane (equator).
    // Kerr metric dictates spin is around the Z-axis.
    constexpr float orbitRadius = 15.0f;
    constexpr float orbitSpeed  = 0.1f; // rad/sec
    const float angle = 0.0f;
    // const float angle = static_cast<float>(elapsedTime_) * orbitSpeed;

    // Micro-offset on Y and slight Z elevation to avoid theta=0 singularity
    camera_.position = glm::vec3(orbitRadius * std::cos(angle),
                                 0.001f + orbitRadius * std::sin(angle),
                                 1.5f);

    // Explicitly set Z as the UP vector (0, 0, 1)
    camera_.viewMatrix = glm::lookAt(
        camera_.position,
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    constexpr float fovYDegrees = 60.0f;
    constexpr float nearPlane   = 0.1f;
    constexpr float farPlane    = 1000.0f;
    const float aspect = static_cast<float>(kWindowWidth) / static_cast<float>(kWindowHeight);

    camera_.projMatrix = glm::perspective(glm::radians(fovYDegrees), aspect, nearPlane, farPlane);
}
}
