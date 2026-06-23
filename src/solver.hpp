#pragma once

#include <glm/glm.hpp>

namespace bhs {

struct CameraState {
    glm::vec3 position{0.0f, 0.0f, 10.0f};
    glm::mat4 viewMatrix{1.0f};
    glm::mat4 projMatrix{1.0f};   // renamed from invProjMatrix - see correction above
};

struct DiskState {
    float innerRadius = 3.0f;
    float outerRadius = 12.0f;
    float spin        = 0.6f;
};

class Solver {
public:
    void init();
    void update(double dt);

    const CameraState& cameraState()   const { return camera_; }
    const DiskState&   diskState()     const { return disk_; }
    double              elapsedTime()  const { return elapsedTime_; }

private:
    CameraState camera_;
    DiskState   disk_;
    double      elapsedTime_ = 0.0;
};

} // namespace bhs
