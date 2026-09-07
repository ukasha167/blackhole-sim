#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string>

#include "app/window.hpp"
#include "defines.hpp"
#include "gfx/device.hpp"
#include "gfx/renderer.hpp"
#include "physics/freefall.hpp"
#include "physics/luts.hpp"
#include "tools/validate.hpp"

namespace {

std::string metallibPath() {
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "assets/shaders.metallib";
}

// --FRAMES N: BENCHMARK MODE. RUNS FIXED FRAMES, DUMPS STATS, EXITS.
const char* parseOption(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], flag) == 0) {
            return argv[i + 1];
        }
    }
    return nullptr;
}

long parseFrameLimit(int argc, char* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0) {
            return std::strtol(argv[i + 1], nullptr, 10);
        }
    }
    return 0;
}

} // NAMESPACE

// APPLY --SET CLI OVERRIDES TO HUD TUNABLES.
void applyOverrides(int argc, char* argv[], bhs::Renderer& renderer) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--set") != 0) {
            continue;
        }
        const std::string assignment = argv[i + 1];
        const std::size_t split = assignment.find('=');
        if (split == std::string::npos) {
            SDL_Log("--set wants name=value, got '%s'", assignment.c_str());
            continue;
        }
        const std::string name = assignment.substr(0, split);
        const float value = std::strtof(assignment.c_str() + split + 1, nullptr);
        if (!renderer.setTunable(name.c_str(), value)) {
            SDL_Log("--set: no tunable named '%s'", name.c_str());
        }
    }
}

// PRINT TRAJECTORY WITHOUT RENDERING. DISK PENETRATION CHECK.
int dumpFallPath(const bhs::Renderer::Scene& scene) {
    const bhs::DiskProfile profile = bhs::buildDiskProfile(scene.spin, scene.mass,
                                                           scene.diskOuter);
    bhs::FreeFallState s = bhs::freeFallFromVelocity(scene.spin, scene.mass,
                                                     scene.cameraRadius, scene.fallTheta,
                                                     -scene.fallInward,
                                                     scene.fallThetaRate, 0.0);
    const double a = scene.spin * scene.mass;
    const double horizon = scene.mass + std::sqrt(std::max(scene.mass * scene.mass - a * a, 0.0));

    std::printf("disk %.3f to %.1f M, horizon %.4f M, %.1f tau per wall second\n",
                double(profile.innerRadius), double(scene.diskOuter), horizon,
                double(scene.fallSpeed));
    std::printf("  tau        r    theta         z    seconds\n");

    double lastZ = s.r * std::cos(s.theta);
    bool crossed = false;

    for (int i = 0; i < 20000; ++i) {
        const double z = s.r * std::cos(s.theta);
        const char* note = "";
        if (lastZ * z < 0.0) {
            note = (s.r >= profile.innerRadius && s.r <= scene.diskOuter)
                 ? "  <== THROUGH THE DISK" : "  <== plane, outside the disk";
            crossed = true;
        }
        if (i % 40 == 0 || note[0] != '\0') {
            std::printf("%7.2f  %7.3f  %7.4f  %+8.3f  %7.2f%s\n",
                        s.properTime, s.r, s.theta, z,
                        s.properTime / std::max(double(scene.fallSpeed), 1e-3), note);
        }
        lastZ = z;
        if (!bhs::freeFallAdvance(s, scene.spin, scene.mass, 0.05)) {
            std::printf("horizon reached at tau %.2f, %.2f seconds in\n",
                        s.properTime, s.properTime / std::max(double(scene.fallSpeed), 1e-3));
            break;
        }
    }
    if (!crossed) {
        std::printf("NEVER crossed the equatorial plane.\n");
    }
    return 0;
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--validate") == 0) {
            return bhs::runValidation();
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fall-path") == 0) {
            bhs::Renderer offline;
            applyOverrides(argc, argv, offline);
            return dumpFallPath(offline.scene());
        }
    }

    const long  frameLimit     = parseFrameLimit(argc, argv);
    const char* screenshotPath = parseOption(argc, argv, "--screenshot");
    // START CLOCK OFFSET FOR SCREENSHOT.
    const char* timeOption     = parseOption(argc, argv, "--time");
    const double startTime     = timeOption ? std::strtod(timeOption, nullptr) : 0.0;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    bhs::Window window;
    if (!window.init(bhs::kWindowTitle, bhs::kWindowWidth, bhs::kWindowHeight)) {
        SDL_Quit();
        return 1;
    }

    std::uint32_t widthPx = 0;
    std::uint32_t heightPx = 0;
    window.drawableSize(widthPx, heightPx);

    bhs::Device device;
    if (!device.init(window.metalLayer(), metallibPath().c_str())) {
        window.shutdown();
        SDL_Quit();
        return 1;
    }
    device.resize(widthPx, heightPx);

    bhs::Renderer renderer;
    if (!renderer.init(device, widthPx, heightPx)) {
        device.shutdown();
        window.shutdown();
        SDL_Quit();
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fall") == 0) {
            renderer.startFall();
        }
    }

    applyOverrides(argc, argv, renderer);

    // ADVANCE FALL TAU AND FREEZE FOR REPRODUCIBLE SCREENSHOT.
    if (const char* tauOption = parseOption(argc, argv, "--fall-tau")) {
        renderer.startFall();
        renderer.advanceFall(std::strtod(tauOption, nullptr));
        renderer.scene().fallSpeed = 0.0f;
    }

    SDL_Log("Metal device: %s", device.name());
    SDL_Log("Drawable: %u x %u", widthPx, heightPx);

    bhs::FrameStats stats;
    double   elapsed     = startTime;
    double   fpsAccum    = 0.0;
    int      fpsFrames   = 0;
    uint64_t lastTicks   = SDL_GetTicksNS();

    while (window.pumpEvents()) {
        const uint64_t frameStart = SDL_GetTicksNS();

        double dt = static_cast<double>(frameStart - lastTicks) / 1.0e9;
        lastTicks = frameStart;
        dt = std::min(dt, bhs::kMaxFrameTime);
        elapsed += dt;

        for (int key : window.keysPressed()) {
            renderer.handleKey(key);
        }

        if (window.resized()) {
            window.drawableSize(widthPx, heightPx);
            renderer.resize(widthPx, heightPx);
        }

        stats.frameMs    = dt * 1000.0;
        stats.frameIndex = stats.frameIndex + 1;

        renderer.render(elapsed, stats);

        // CPU TIME INCLUDES SETUP PLUS SEMAPHORE WAIT.
        stats.cpuMs = static_cast<double>(SDL_GetTicksNS() - frameStart) / 1.0e6;

        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 0.25) {
            stats.fps = fpsFrames / fpsAccum;
            fpsAccum  = 0.0;
            fpsFrames = 0;
        }

        const long shotFrame = frameLimit > 0 ? frameLimit : 10;
        if (screenshotPath && static_cast<long>(stats.frameIndex) >= shotFrame) {
            if (renderer.capture(screenshotPath, elapsed, stats)) {
                std::printf("wrote %s\n", screenshotPath);
            }
            break;
        }

        if (frameLimit > 0 && static_cast<long>(stats.frameIndex) >= frameLimit) {
            break;
        }
    }

    if (frameLimit > 0) {
        const bhs::GpuTimer& timer = renderer.timer();
        std::printf("\n--- %ld frames at %u x %u ---\n", frameLimit, widthPx, heightPx);
        std::printf("device        %s\n", device.name());
        std::printf("fps           %.1f\n", stats.fps);
        std::printf("frame         %.2f ms\n", stats.frameMs);
        std::printf("cpu           %.2f ms\n", stats.cpuMs);
        if (timer.supported()) {
            std::printf("gpu span      %.3f ms  (counters, first start to last end)\n",
                        timer.totalMs());
            std::printf("gpu cmdbuf    %.3f ms  (independent cross-check)\n",
                        renderer.commandBufferGpuMs());
            for (std::uint32_t i = 0; i < timer.passCount(); ++i) {
                std::printf("  %-11s %.3f ms (span, overlaps neighbours)\n",
                            timer.passLabel(i), timer.passMs(i));
            }
        } else {
            std::printf("gpu           counter sampling unavailable\n");
        }
    }

    renderer.shutdown();
    device.shutdown();
    window.shutdown();
    SDL_Quit();
    return 0;
}
